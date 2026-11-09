"""Development-only start/stop check of the on-board classifier over serial."""
import json
import time
import serial

with serial.Serial(port=None,baudrate=115200,timeout=.3) as link:
    link.port="COM3";link.dtr=False;link.rts=False;link.open();link.reset_input_buffer()
    try:
        link.write(b"s");deadline=time.monotonic()+8;last=0;valid=[];samples=[]
        while time.monotonic()<deadline:
            if time.monotonic()-last>.4:link.write(b"?");last=time.monotonic()
            line=link.readline().decode("utf-8",errors="replace").strip()
            if not line.startswith("{"):continue
            state=json.loads(line);d=state.get("detection",{});samples.append((time.monotonic(),state["seq"]))
            if d.get("state") in (2,3,5,6):
                assert state["ok"] and state["age"]<500
                assert abs(sum(d["probabilities"])-1)<.001
                valid.append(d)
        assert valid,"No on-board classification after warm-up"
        print(json.dumps(valid[-1],indent=2))
        if len(samples)>2:
            rate=(samples[-1][1]-samples[0][1])/(samples[-1][0]-samples[0][0])
            print(f"Observed sensor rate during classification: {rate:.1f} Hz")
            assert 90<rate<110,"Unexpected sensor rate while classification is active"
        print("PASS: on-board four-state model runs and emits normalized probabilities.")
    finally:
        link.write(b"x")
