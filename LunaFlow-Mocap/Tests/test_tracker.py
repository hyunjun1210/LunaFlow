import importlib.util
import io
import json
from pathlib import Path
import threading
import time
import types
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("tracker", Path(__file__).parents[1] / "Tracker" / "tracker.py")
tracker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tracker)


class TrackerTests(unittest.TestCase):
    def landmark(self, **kw):
        return types.SimpleNamespace(**dict(x=.1, y=.2, z=.3, visibility=.9, presence=.8, **kw))

    def result(self):
        return types.SimpleNamespace(pose_world_landmarks=[[self.landmark() for _ in range(33)]],
                                     pose_landmarks=[[self.landmark() for _ in range(33)]])

    def test_protocol_shape_and_no_image(self):
        message = tracker.packet(self.result(), 3, 42)
        self.assertEqual(len(message["points"]), 33)
        self.assertEqual(message["points"][0], [.1, .2, .3, .9, .8, .1, .2])
        with patch("sys.stdout", new_callable=io.StringIO) as stream:
            tracker.emit(message)
            parsed = json.loads(stream.getvalue())
        self.assertEqual(set(parsed), {"version", "type", "seq", "time_ms", "detected", "points"})

    def test_no_pose_and_nan(self):
        result = self.result()
        result.pose_world_landmarks = []
        self.assertFalse(tracker.packet(result, 1, 0)["detected"])
        result = self.result()
        result.pose_world_landmarks[0][13].x = float("nan")
        self.assertEqual(tracker.packet(result, 2, 1)["points"], [])

    def test_minimum_confidence(self):
        result = self.result()
        result.pose_landmarks[0][0].visibility = .2
        self.assertEqual(tracker.packet(result, 1, 0)["points"][0][3], .2)

    def test_eof_and_stop_command(self):
        for text in ["", "stop\n", "ignored\nstop\n"]:
            stop = threading.Event()
            with patch("sys.stdin", io.StringIO(text)):
                tracker.control_input(stop)
            self.assertTrue(stop.is_set())

    def test_capture_release_on_disconnect(self):
        class Camera:
            released = False
            def isOpened(self): return True
            def set(self, *args): return False
            def read(self): return False, None
            def release(self): self.released = True
        camera = Camera()
        cv2 = types.SimpleNamespace(VideoCapture=lambda *args: camera, CAP_DSHOW=0,
            CAP_PROP_FRAME_WIDTH=1, CAP_PROP_FRAME_HEIGHT=2, CAP_PROP_FPS=3, CAP_PROP_BUFFERSIZE=4)
        capture = tracker.LatestCamera(cv2, 0, threading.Event())
        capture.thread.start()
        with self.assertRaisesRegex(RuntimeError, "disconnected"):
            capture.take()
        capture.close()
        self.assertTrue(camera.released)

    def test_latest_slot_and_monotonic_capture_clock(self):
        class Camera:
            released = False
            count = 0
            def isOpened(self): return True
            def set(self, *args): return True
            def read(self):
                time.sleep(.002)
                self.count += 1
                return True, self.count
            def release(self): self.released = True
        camera = Camera()
        cv2 = types.SimpleNamespace(VideoCapture=lambda *args: camera, CAP_DSHOW=0,
            CAP_PROP_FRAME_WIDTH=1, CAP_PROP_FRAME_HEIGHT=2, CAP_PROP_FPS=3, CAP_PROP_BUFFERSIZE=4)
        capture = tracker.LatestCamera(cv2, 0, threading.Event())
        capture.thread.start()
        first = capture.take()
        time.sleep(.04)
        second = capture.take()
        capture.close()
        self.assertGreater(second[0], first[0])
        self.assertGreater(second[1], first[1] + 1)
        self.assertTrue(camera.released)


if __name__ == "__main__":
    unittest.main()
