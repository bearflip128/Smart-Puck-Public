"""Drive the robot arm (armctl) from the puck's "Robot arm" mode.

Usage: python arm_bridge.py [COM5] [--url http://127.0.0.1:7002] [--no-switch]
                            [--idle-disarm 120] [--log FILE] [--save-login]
Needs pyserial and requests (keyring optional). Ctrl+C to quit.

Credentials are armctl's dashboard login. They come from ARMCTL_USERNAME /
ARMCTL_PASSWORD (the same variables armctl itself reads), else from Windows
Credential Manager (saved once with --save-login, needs `pip install keyring`),
else it asks. They are never written to a file.

Runs unattended too: tools/install_arm_bridge.ps1 starts it at logon with
pythonw and --log. It keeps going if armctl, the tunnel or the puck drops, and
picks the puck up again when it is plugged back in.

What it does:
  - Turning the knob in Robot arm mode sends absolute joint targets to armctl's
    POST /api/goal. Turns are coalesced to at most --hz requests a second, and each
    send moves a joint at most MAX_JUMP degrees past where the arm already is, so a
    fast spin or a stale screen can't fling the arm. armctl's own trajectory
    generator then shapes the motion as it does for the dashboard sliders.
  - Pressing the knob picks the next joint (handled on the puck, nothing is sent).
  - The Safety layer (first on the arm screen) arms the arm when turned right and
    disarms it when turned left. The bridge disarms it again after --idle-disarm
    seconds without a knob turn (0 turns that off).
  - It polls GET /api/telemetry and pushes the arm's pose and state to the puck, so
    the screen matches the arm. armctl refuses motion until the arm is armed; type
    "arm" here (or use the dashboard) first.

Commands you can type while it runs: arm, disarm, home, stop (emergency stop),
reset (clear the emergency stop), sync (re-read the pose).
"""
import argparse
import getpass
import json
import os
import socket
import sys
import threading
import time

import requests
import serial

try:
    import keyring
except ImportError:
    keyring = None
KEYRING_SERVICE = "smart-puck-armctl"

# armctl joint names (armctl/config/arm.yaml); the puck uses the same names.
JOINTS = {"shoulder_pan", "shoulder_lift", "elbow_flex", "wrist_flex", "wrist_roll", "gripper"}
MAX_JUMP = 30.0  # degrees (or gripper %) one send may lead the arm by
POLL_S = 0.5

ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
ap.add_argument("port", nargs="?", default="COM5")
ap.add_argument("--url", default=os.environ.get("ARMCTL_URL", "http://127.0.0.1:7002"),
                help="armctl server (default: local; a public https URL also works)")
ap.add_argument("--hz", type=float, default=10.0, help="max goal requests per second")
ap.add_argument("--no-switch", action="store_true", help="don't switch the puck to Robot arm mode")
ap.add_argument("--idle-disarm", type=float, default=120.0,
                help="disarm after this many seconds without a knob turn (0 = never)")
ap.add_argument("--share", type=int, default=7077,
                help="localhost TCP port other tools can use to talk to the puck while this runs "
                     "(pyserial URL socket://127.0.0.1:PORT; 0 = off)")
ap.add_argument("--log", help="append output to this file (for running in the background)")
ap.add_argument("--save-login", action="store_true",
                help="ask for the login, save it in Windows Credential Manager and quit")
args = ap.parse_args()
url = args.url.rstrip("/")

if args.log or sys.stdout is None:  # pythonw has no console
    log = open(args.log or os.devnull, "a", buffering=1, encoding="utf-8")
    sys.stdout = sys.stderr = log

lock = threading.Lock()
pending = {}       # armctl joint -> target, waiting to be sent
arm_pose = {}      # armctl joint -> where the arm is going (goal) or is (observed)
synced = False     # no goals are sent until the pose has been read once
status = "offline"

last_turn = 0.0    # when the knob last moved a joint, for --idle-disarm


def stamp(text):
    print(time.strftime("%H:%M:%S ") + text, flush=True)


def saved_login():
    if keyring is None:
        return None, None
    try:
        user = keyring.get_password(KEYRING_SERVICE, "username")
        return user, user and keyring.get_password(KEYRING_SERVICE, user)
    except Exception:
        return None, None


if args.save_login:
    if keyring is None:
        sys.exit("Install keyring first: python -m pip install keyring")
    u = input("armctl username: ").strip()
    keyring.set_password(KEYRING_SERVICE, "username", u)
    keyring.set_password(KEYRING_SERVICE, u, getpass.getpass("armctl password: "))
    sys.exit("Saved in Windows Credential Manager.")

http = requests.Session()
user, password = os.environ.get("ARMCTL_USERNAME"), os.environ.get("ARMCTL_PASSWORD")
if not (user and password):
    user, password = saved_login()
if not (user and password):
    if not (sys.stdin and sys.stdin.isatty()):
        sys.exit("No armctl login: set ARMCTL_USERNAME / ARMCTL_PASSWORD or run with --save-login.")
    user = input("armctl username: ")
    password = getpass.getpass("armctl password: ")


def login():
    """Sign in. A wrong password quits; an unreachable server is retried by the caller."""
    r = http.post(f"{url}/login", data={"username": user, "password": password},
                  allow_redirects=False, timeout=5)
    if r.status_code != 302:
        sys.exit(f"armctl login failed (HTTP {r.status_code}). Check the armctl username and password.")


def api(method, path, body=None):
    """Call armctl, signing in again if the session expired. Returns (status, json)."""
    for _ in range(2):
        r = http.request(method, f"{url}{path}", json=body, timeout=5)
        if r.status_code != 401:
            break
        login()
    try:
        return r.status_code, r.json()
    except ValueError:
        return r.status_code, {}


# Leave DTR/RTS low: toggling RTS resets the ESP32-S3 over USB-Serial/JTAG.
s = serial.Serial()
s.port, s.baudrate, s.timeout, s.dtr, s.rts = args.port, 115200, 0.2, False, False
write_lock = threading.Lock()


def send_raw(data):
    with write_lock:
        if not s.is_open:
            return
        try:
            s.write(data)
        except serial.SerialException:
            pass  # unplugged; puck_reader reopens it


def send_puck(obj):
    send_raw(json.dumps(obj).encode() + b"\n")


# Sharing the puck: the bridge holds COM5, so other tools (puck_monitor.py, the
# simulator's lineup loader) connect to 127.0.0.1:--share instead. Lines they send go
# to the puck as is; every line from the puck is copied to them.
clients = []
clients_lock = threading.Lock()


def share_server():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        srv.bind(("127.0.0.1", args.share))
    except OSError as exc:
        stamp(f"can't share the puck on port {args.share}: {exc}")
        return
    srv.listen()
    stamp(f"sharing the puck at socket://127.0.0.1:{args.share}")
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=share_client, args=(conn,), daemon=True).start()


def share_client(conn):
    with clients_lock:
        clients.append(conn)
    buf = b""
    try:
        while True:
            data = conn.recv(65536)
            if not data:
                break
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                send_raw(line + b"\n")
    except OSError:
        pass
    finally:
        with clients_lock:
            clients.remove(conn)
        conn.close()


def copy_to_clients(line):
    with clients_lock:
        for c in list(clients):
            try:
                c.sendall(line if line.endswith(b"\n") else line + b"\n")
            except OSError:
                pass


def greet_puck():
    send_puck({"t": "hello"})
    send_puck({"t": "time", "epoch": int(time.time() + time.localtime().tm_gmtoff)})
    if not args.no_switch:
        send_puck({"t": "ctx", "app": "robot arm", "mode": "arm"})
    push_pose()


def open_puck():
    """Open the puck's port, waiting for it to be plugged in (or freed by another program)."""
    said = False
    while True:
        try:
            with write_lock:
                s.open()
            stamp(f"puck connected on {args.port}")
            greet_puck()
            return
        except serial.SerialException as exc:
            if not said:
                stamp(f"waiting for the puck on {args.port} ({exc})")
                said = True
            time.sleep(2)


def push_pose():
    with lock:
        joints = {k: v for k, v in arm_pose.items() if k in JOINTS}
    send_puck({"t": "arm", "status": status, "joints": joints})


def arm_command(path):
    try:
        code, r = api("POST", path)
    except requests.RequestException as exc:
        return f"{path} failed: {exc}"
    return "ok" if r.get("ok") else f"{path} failed (HTTP {code}): {r.get('error')}"


def puck_reader():
    global last_turn
    open_puck()
    while True:
        try:
            line = s.readline()
        except (serial.SerialException, OSError, TypeError):
            stamp("puck disconnected")
            with write_lock:
                s.close()
            open_puck()
            continue
        if not line:
            continue
        copy_to_clients(line)
        try:
            msg = json.loads(line)
        except ValueError:
            continue  # boot log or partial line
        if msg.get("evt") == "hello":
            greet_puck()  # the puck rebooted
            continue
        if msg.get("evt") != "action" or msg.get("mode") != "arm":
            continue
        if msg.get("direct"):
            continue  # the puck is linked (arm_link.py) and drives the arm over Wi-Fi itself
        if msg.get("do") == "puck.cycle":
            stamp(f"joint: {msg.get('layer')}")
        elif msg.get("do") in ("robotarm.arm", "robotarm.disarm"):
            arming = msg["do"] == "robotarm.arm"
            if arming != (status == "armed"):
                last_turn = time.time()
                stamp(("arm" if arming else "disarm") + " from the puck: " +
                      arm_command("/api/arm" if arming else "/api/disarm"))
        elif msg.get("joint") in JOINTS and isinstance(msg.get("value"), (int, float)):
            with lock:
                pending[msg["joint"]] = float(msg["value"])
            last_turn = time.time()


def poll_telemetry():
    global status, synced, last_turn
    last = None
    while True:
        try:
            code, t = api("GET", "/api/telemetry")
            if code != 200:
                raise RuntimeError(f"HTTP {code}")
            if t.get("estopped"):
                new = "e-stop"
            elif not t.get("connected"):
                new = "no servos"
            else:
                new = "armed" if t.get("armed") else "disarmed"
            pose = {}
            for j in t.get("joints", []):
                # While armed the goal is where the arm is headed; when not, only the
                # servo reading means anything (it may have been moved by hand).
                v = j.get("goal") if t.get("armed") else j.get("observed")
                if v is None:
                    v = j.get("setpoint")
                if v is not None:
                    pose[j["name"]] = float(v)
            with lock:
                arm_pose.update(pose)
            synced = bool(pose)
        except Exception as exc:  # server down, tunnel down, bad reply
            new = "offline"
            if status != "offline":
                print(f"armctl unreachable: {exc}", flush=True)
        if new != status:
            stamp(f"arm: {new}")
            status = new
        if (status == "armed" and args.idle_disarm > 0
                and time.time() - max(last_turn, 0) > args.idle_disarm and last_turn):
            stamp(f"no knob turns for {args.idle_disarm:.0f} s: " + arm_command("/api/disarm"))
            last_turn = 0.0
        snapshot = (status, tuple(sorted((k, round(v)) for k, v in arm_pose.items())))
        if snapshot != last:
            push_pose()
            last = snapshot
        time.sleep(POLL_S)


def send_goals():
    warned = None
    while True:
        time.sleep(1.0 / args.hz)
        with lock:
            if not pending or not synced:
                continue
            goals = {}
            for name, target in pending.items():
                here = arm_pose.get(name, target)
                goals[name] = max(here - MAX_JUMP, min(here + MAX_JUMP, target))
            pending.clear()
        try:
            code, r = api("POST", "/api/goal", {"joints": goals, "source": "puck"})
        except requests.RequestException as exc:
            print(f"goal not sent: {exc}", flush=True)
            continue
        if code == 200:
            with lock:
                arm_pose.update(r.get("accepted", {}))
            warned = None
            print("goal " + ", ".join(f"{k}={v:.0f}" for k, v in goals.items()), flush=True)
        elif r.get("error") != warned:
            warned = r.get("error")
            print(f"goal refused (HTTP {code}): {warned}", flush=True)
            push_pose()  # put the screen back where the arm actually is


COMMANDS = {"arm": "/api/arm", "disarm": "/api/disarm", "home": "/api/home",
            "stop": "/api/estop", "reset": "/api/reset"}

while True:  # armctl or the tunnel may still be starting
    try:
        login()
        break
    except requests.RequestException as exc:
        stamp(f"armctl not reachable yet ({exc}); retrying")
        time.sleep(5)
for fn in (puck_reader, poll_telemetry, send_goals) + ((share_server,) if args.share else ()):
    threading.Thread(target=fn, daemon=True).start()
stamp(f"Bridging {args.port} to {url}. Commands: {', '.join(COMMANDS)}, sync.")
try:
    if sys.stdin is None or not sys.stdin.isatty():
        while True:  # running in the background
            time.sleep(60)
    for text in sys.stdin:
        cmd = text.strip().lower()
        if cmd == "sync":
            push_pose()
        elif cmd in COMMANDS:
            print(arm_command(COMMANDS[cmd]), flush=True)
        elif cmd:
            print(f"Unknown command. Try: {', '.join(COMMANDS)}, sync.")
except KeyboardInterrupt:
    pass
