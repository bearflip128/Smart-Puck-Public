"""Link the puck to the robot arm (armctl) so it drives the arm over Wi-Fi itself (one time).

After this the puck needs no laptop bridge: it signs in to armctl, reads the arm's pose and
sends each knob turn as a move. armctl and its tunnel still have to be running.

Usage:  python arm_link.py [COM5] [--url https://arm.example.com] [--lan http://192.168.1.20:7002]
        python arm_link.py [COM5] --clear      (forget the login; the bridge works again)
        python arm_link.py --wifi <puck IP> [--lan ...]   (over Wi-Fi, no cable; firmware 0.22+)

--lan is optional: the puck tries it first (faster at home) and falls back to --url.
It checks the login against armctl first, then asks for the password (or reads
ARMCTL_USERNAME / ARMCTL_PASSWORD) and sends it to the puck over USB. The puck keeps it in
its flash. Close anything else holding the port first (arm_bridge.py, puck_monitor.py),
or pass socket://127.0.0.1:7077 as the port while arm_bridge.py runs.
Needs: pip install pyserial requests
"""
import argparse
import getpass
import json
import os
import sys
import time

import requests
import serial

ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
ap.add_argument("port", nargs="?", default="COM5")
ap.add_argument("--url", default=os.environ.get("ARMCTL_URL", "http://127.0.0.1:7002"))
ap.add_argument("--lan", default="", help="armctl on the home network, e.g. http://192.168.1.20:7002")
ap.add_argument("--clear", action="store_true")
ap.add_argument("--wifi", metavar="PUCK", help="send it over Wi-Fi to this puck address instead of USB "
                "(uses the update password in firmware/ota_password.txt)")
args = ap.parse_args()

if args.clear:
    msg = {"t": "armnet", "clear": True}
else:
    user = os.environ.get("ARMCTL_USERNAME") or input("armctl username: ").strip()
    password = os.environ.get("ARMCTL_PASSWORD") or getpass.getpass("armctl password: ")
    for base in [b for b in (args.lan, args.url) if b]:
        try:
            r = requests.post(base.rstrip("/") + "/login", data={"username": user, "password": password},
                              allow_redirects=False, timeout=8)
            print(f"{base}: " + ("login ok" if r.status_code in (302, 303) else f"login refused (HTTP {r.status_code})"))
            if r.status_code not in (302, 303) and base == args.url:
                sys.exit("Check the username and password.")
        except requests.RequestException as exc:
            print(f"{base}: not reachable from here ({exc})")
    msg = {"t": "armnet", "url": args.url.rstrip("/"), "lan": args.lan.rstrip("/"), "user": user, "pass": password}

if args.wifi:
    key_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ota_password.txt")
    with open(key_file, encoding="utf-8") as f:
        msg["key"] = f.read().strip()
    r = requests.post(f"http://{args.wifi}/armlink", json=msg, timeout=10)
    if r.status_code != 200:
        sys.exit(f"puck refused: {r.status_code} {r.text}")
    print("Sent to the puck over Wi-Fi. Waiting for it to sign in...")
    state, end = "", time.time() + 30
    while time.time() < end:
        time.sleep(2)
        try:
            new = requests.get(f"http://{args.wifi}/status", timeout=5).json().get("armnet", "")
        except (requests.RequestException, ValueError):
            continue
        if new != state:
            print("puck:", new)
            state = new
        if args.clear or new in ("armed", "disarmed", "e-stop", "no servos", "login failed"):
            break
    sys.exit(0)

port = serial.serial_for_url(args.port, do_not_open=True)
port.baudrate, port.timeout = 115200, 0.5
if hasattr(port, "dtr"):
    port.dtr = port.rts = False  # toggling RTS resets the ESP32-S3
port.open()
port.write(json.dumps(msg).encode() + b"\n")
print("Sent to the puck. Waiting for it to sign in...")
end = time.time() + 30
while time.time() < end:
    line = port.readline()
    try:
        evt = json.loads(line)
    except ValueError:
        continue
    if evt.get("evt") == "armnet":
        print("puck:", evt.get("msg"))
        if str(evt.get("msg", "")).startswith(("linked", "unlinked", "error login")):
            break
