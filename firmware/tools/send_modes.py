"""Send a mode lineup from the simulator to the puck.

Usage: python send_modes.py lineup.json [COM5]

lineup.json is either the serial line itself ({"t":"modes",...}) or the simulator's
puck/config document ({"version", "line", "modes", ...}). The puck saves it and answers
with modes_ok (and any skipped mode ids) or modes_err.
"""
import json
import sys
import time

import serial

path = sys.argv[1]
port = sys.argv[2] if len(sys.argv) > 2 else "COM5"

with open(path, encoding="utf-8") as f:
    data = json.load(f)
if "line" in data:  # puck/config document
    data = json.loads(data["line"])
if data.get("t") != "modes":
    sys.exit("Not a mode lineup: expected {\"t\":\"modes\",...}")

line = json.dumps(data, separators=(",", ":")).encode() + b"\n"

# Leave DTR/RTS low: toggling RTS resets the ESP32-S3 over USB-Serial/JTAG.
s = serial.Serial()
s.port, s.baudrate, s.timeout, s.dtr, s.rts = port, 115200, 0.2, False, False
s.open()
s.write(line)
print(f"Sent {len(data['modes'])} modes ({len(line)} bytes), version {data.get('version')}.")

end = time.time() + 5
while time.time() < end:
    raw = s.readline()
    if not raw:
        continue
    try:
        msg = json.loads(raw)
    except ValueError:
        continue
    if msg.get("evt") == "modes_ok":
        print(f"Puck loaded {msg['count']} modes.", end=" ")
        print(f"Skipped (no screen for them yet): {', '.join(msg['skipped'])}" if msg.get("skipped") else "")
        if msg.get("warning"):
            print("Warning:", msg["warning"])
        sys.exit(0)
    if msg.get("evt") == "modes_err":
        sys.exit(f"Puck rejected the lineup: {msg.get('error')}")
sys.exit("No answer from the puck.")
