"""Run the real tracker through native IPC without opening any camera."""
from pathlib import Path
import runpy
import sys

sys.argv.append("--smoke-test")
runpy.run_path(str(Path(__file__).parents[1] / "Tracker" / "tracker.py"), run_name="__main__")
