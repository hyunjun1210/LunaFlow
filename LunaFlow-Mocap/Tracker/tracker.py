"""Local-only webcam pose worker. Stdout is versioned NDJSON, never image bytes."""
import argparse
import json
import math
import sys
import threading
import time
from pathlib import Path


def emit(packet):
    print(json.dumps({"version": 1, **packet}, allow_nan=False, separators=(",", ":")), flush=True)


def packet(result, sequence, timestamp_ms):
    world = result.pose_world_landmarks
    image = result.pose_landmarks
    points = []
    if world and image and len(world[0]) == len(image[0]) == 33:
        for w, p in zip(world[0], image[0]):
            row = [w.x, w.y, w.z, min(w.visibility, p.visibility),
                   min(w.presence, p.presence), p.x, p.y]
            if not all(math.isfinite(v) for v in row):
                points = []
                break
            points.append(row)
    return {"type": "frame", "seq": sequence, "time_ms": timestamp_ms,
            "detected": len(points) == 33, "points": points}


class LatestCamera:
    """One camera-owned read thread and one latest frame slot: bounded latency/memory."""
    def __init__(self, cv2, index, stop):
        self.cv2, self.index, self.stop = cv2, index, stop
        self.condition = threading.Condition()
        self.latest = None
        self.error = None
        self.thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        camera = None
        try:
            camera = self.cv2.VideoCapture(self.index, self.cv2.CAP_DSHOW)
            if not camera.isOpened():
                raise RuntimeError("Cannot open webcam. Check Windows camera privacy, index, and other apps.")
            camera.set(self.cv2.CAP_PROP_FRAME_WIDTH, 640)
            camera.set(self.cv2.CAP_PROP_FRAME_HEIGHT, 480)
            camera.set(self.cv2.CAP_PROP_FPS, 30)
            camera.set(self.cv2.CAP_PROP_BUFFERSIZE, 1)
            start = time.monotonic_ns()
            last_ms = -1
            while not self.stop.is_set():
                ok, frame = camera.read()
                if not ok or frame is None:
                    raise RuntimeError("Webcam disconnected or frame read failed.")
                timestamp = max(last_ms + 1, (time.monotonic_ns() - start) // 1_000_000)
                last_ms = timestamp
                with self.condition:
                    self.latest = (timestamp, frame)
                    self.condition.notify()
        except Exception as exc:
            with self.condition:
                self.error = str(exc)
                self.condition.notify()
        finally:
            if camera is not None:
                camera.release()

    def take(self):
        with self.condition:
            self.condition.wait_for(
                lambda: self.latest is not None or self.error or self.stop.is_set(), timeout=.1)
            if self.error:
                raise RuntimeError(self.error)
            result, self.latest = self.latest, None
            return result

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        if self.thread.is_alive():
            raise RuntimeError("Camera driver is blocked; native owner will force process cleanup.")


def control_input(stop):
    # EOF also stops capture when the native editor closes its pipe/crashes.
    for line in sys.stdin:
        if line.strip() == "stop":
            break
    stop.set()


def run(args):
    import cv2
    import mediapipe as mp
    import numpy as np

    # Bytes avoid native model-path Unicode limitations on some Windows wheels.
    options = mp.tasks.vision.PoseLandmarkerOptions(
        base_options=mp.tasks.BaseOptions(model_asset_buffer=Path(args.model).read_bytes()),
        running_mode=mp.tasks.vision.RunningMode.VIDEO,
        num_poses=1, min_pose_detection_confidence=.6,
        min_pose_presence_confidence=.6, min_tracking_confidence=.6,
        output_segmentation_masks=False)
    with mp.tasks.vision.PoseLandmarker.create_from_options(options) as detector:
        if args.smoke_test:
            # Real inference, but NOT evidence of human tracking accuracy.
            result = detector.detect_for_video(
                mp.Image(image_format=mp.ImageFormat.SRGB, data=np.zeros((480, 640, 3), dtype=np.uint8)), 0)
            emit({"type": "status", "state": "ready", "message": "Model inference smoke test (blank image; no webcam)."})
            emit(packet(result, 1, 0))
            emit({"type": "status", "state": "stopped", "message": "Smoke inference complete."})
            return
        stop = threading.Event()
        threading.Thread(target=control_input, args=(stop,), daemon=True).start()
        camera = LatestCamera(cv2, args.camera, stop)
        camera.thread.start()
        try:
            sequence = 0
            while not stop.is_set():
                item = camera.take()
                if item is None:
                    continue
                timestamp, bgr = item
                if sequence == 0:
                    emit({"type": "status", "state": "ready", "message": "Webcam open. Raw video stays in memory; no upload or recording."})
                rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
                result = detector.detect_for_video(mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb), timestamp)
                sequence += 1
                emit(packet(result, sequence, timestamp))
                stop.wait(1 / 60)
        finally:
            camera.close()
    emit({"type": "status", "state": "stopped", "message": "Webcam released."})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True)
    parser.add_argument("--camera", type=int, default=0, choices=range(16))
    parser.add_argument("--smoke-test", action="store_true")
    args = parser.parse_args()
    try:
        run(args)
    except Exception as exc:
        emit({"type": "status", "state": "error", "message": str(exc)[:2048]})
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
