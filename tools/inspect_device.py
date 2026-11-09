"""Read a diagnostic snapshot without starting a continuous USB data stream."""
import argparse
import json
import time
import sys

import serial
sys.stdout.reconfigure(encoding="utf-8",errors="replace")

parser = argparse.ArgumentParser()
parser.add_argument("port", nargs="?", default="COM3")
parser.add_argument("--calibrate", action="store_true", help="Run stationary calibration and verify its result")
args = parser.parse_args()
with serial.Serial(port=None, baudrate=115200, timeout=0.3) as link:
    link.port = args.port
    link.dtr = False
    link.rts = False
    link.open()
    link.reset_input_buffer()
    if args.calibrate:
        link.write(b"c")
        time.sleep(0.2)
    deadline = time.monotonic() + (12 if args.calibrate else 8)
    next_request = 0
    previous_progress = -1
    while time.monotonic() < deadline:
        if time.monotonic() >= next_request:
            link.write(b"?")
            next_request = time.monotonic() + (0.2 if args.calibrate else 1)
        line = link.readline().decode("utf-8", errors="replace").strip()
        if line.startswith("{"):
            state = json.loads(line)
            if not state.get("ok") and state.get("seq",0)==0:
                continue  # Sensor setup retries start after the first second.
            if args.calibrate and state.get("calibration") in (0, 1):
                progress = state.get("progress", 0)
                if progress != previous_progress:
                    print(f"Calibration: {progress}%")
                    previous_progress = progress
                continue
            print(json.dumps(state, indent=2))
            if not state.get("ok"):
                raise SystemExit("Firmware responds, but the sensor is not available.")
            if state.get("age", 9999) > 500:
                raise SystemExit("Firmware responds, but the sensor sample is stale.")
            if args.calibrate and (state.get("calibration") != 2 or not state.get("calibrated")):
                raise SystemExit("Calibration was rejected; see the diagnostic measurements above.")
            if args.calibrate:
                print("PASS: stationary calibration succeeded on the connected device.")
            print("PASS: firmware responds and sensor measurements are fresh.")
            break
        if line:
            print(line)
    else:
        raise SystemExit("No diagnostic response received within eight seconds.")
