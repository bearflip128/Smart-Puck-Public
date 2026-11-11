"""Print the puck's JSON events and let you send it messages.

Usage: python puck_monitor.py [COM5]
Needs pyserial (installed with esptool or PlatformIO). Ctrl+C to quit.

On connect it sends the computer's local time so the clock faces are right.
Type a JSON line to send it as is, or just an app name (zoom, spotify, vscode,
premiere, figma, powerpoint, slack, obs, desktop) to pretend that app came to the front.
"""
import json
import sys
import threading
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
# Leave DTR/RTS low: toggling RTS resets the ESP32-S3 over USB-Serial/JTAG.
s = serial.Serial()
s.port, s.baudrate, s.timeout, s.dtr, s.rts = port, 115200, 0.2, False, False
s.open()


def send(obj):
    s.write(json.dumps(obj).encode() + b"\n")


def reader():
    while True:
        line = s.readline()
        if line:
            print("puck>", line.decode(errors="replace").strip(), flush=True)


threading.Thread(target=reader, daemon=True).start()
send({"t": "hello"})
send({"t": "time", "utc": int(time.time())})
print(f"Connected to {port}. Turn or press the knob, or type an app name or a JSON line.")
try:
    for text in sys.stdin:
        text = text.strip()
        if not text:
            continue
        if text.startswith("{"):
            s.write(text.encode() + b"\n")
        else:
            send({"t": "ctx", "app": text})
except KeyboardInterrupt:
    pass
