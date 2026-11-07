"""Owned-process lifecycle fixture: never opens a webcam or imports ML libraries."""
import argparse
import json
import sys
import threading
import time

p = argparse.ArgumentParser()
p.add_argument("--model")
p.add_argument("--camera", type=int)
args = p.parse_args()


def emit(value):
    print(json.dumps({"version": 1, **value}), flush=True)


if args.camera == 3:
    print("fixture crash diagnostic", file=sys.stderr, flush=True)
    sys.exit(7)
emit({"type": "status", "state": "ready", "message": "Test fixture; no camera."})
if args.camera == 1:
    emit({"type": "frame", "seq": 0, "time_ms": 0, "detected": False, "points": []})
    time.sleep(10)
elif args.camera == 2:
    time.sleep(10)
else:
    stop = threading.Event()

    def control():
        sys.stdin.readline()
        stop.set()

    threading.Thread(target=control, daemon=True).start()
    sequence = 0
    while not stop.wait(.03):
        sequence += 1
        emit({"type": "frame", "seq": sequence, "time_ms": sequence * 30,
              "detected": False, "points": []})
    emit({"type": "status", "state": "stopped", "message": "Fixture released."})
