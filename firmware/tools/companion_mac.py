"""Smart Puck companion for macOS: tells the puck which app is in front, over Wi-Fi.

Same job as companion.py on Windows. While you use the Mac, the puck sends its keys to the Mac
and Sidekick gets that app's tools; when the Mac is idle for 2 minutes the keys go back to the PC.

Usage (on the Mac):  python3 companion_mac.py [puck address]     (default: smart-puck.local)
No extra packages. The first run asks to let Terminal control "System Events": allow it.
"""
import ctypes
import json
import re
import subprocess
import sys
import time
import urllib.request

PUCK = sys.argv[1] if len(sys.argv) > 1 else "smart-puck.local"
AWAY_AFTER = 120  # seconds without input

# App name (as macOS shows it) -> the app ids Sidekick knows.
APPS = {
    "Code": ("vscode", "VS Code"), "Visual Studio Code": ("vscode", "VS Code"), "Microsoft Word": ("word", "Word"),
    "Spotify": ("spotify", "Spotify"), "Adobe Premiere Pro": ("premiere", "Premiere Pro"), "Figma": ("figma", "Figma"),
    "Microsoft PowerPoint": ("powerpoint", "PowerPoint"), "Keynote": ("powerpoint", "Keynote"), "Slack": ("slack", "Slack"),
    "OBS": ("obs", "OBS"), "zoom.us": ("zoom", "Zoom"), "Microsoft Teams": ("teams", "Teams"),
    "Google Chrome": ("chrome", "Chrome"), "Safari": ("chrome", "Safari"), "Arc": ("chrome", "Arc"),
    "Firefox": ("chrome", "Firefox"), "BambuStudio": ("bambustudio", "Bambu Studio"),
    "Bambu Studio": ("bambustudio", "Bambu Studio"),
}


def front_app():
    try:
        out = subprocess.run(["osascript", "-e", 'tell application "System Events" to get name of first '
                              'application process whose frontmost is true'], capture_output=True, text=True, timeout=3)
        return out.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""


# ---------------------------------------------------------------- calls
#
# A call is found without needing any macOS permission where possible:
# - Zoom runs a helper process, CptHost, only while you're in a meeting.
# - Meet is a meet.google.com/abc-defg-hij tab in Chrome, Arc or Edge (reading tab addresses needs
#   the one-time Automation prompt), and counts only while a microphone is in use, so a Meet tab
#   left open after the call, or the lobby, doesn't.
# - "Microphone in use" is CoreAudio's "is running somewhere" flag on a device that can record:
#   the same thing behind the orange dot in the menu bar.

class _Addr(ctypes.Structure):
    _fields_ = [("selector", ctypes.c_uint32), ("scope", ctypes.c_uint32), ("element", ctypes.c_uint32)]


def _cc(code):
    return int.from_bytes(code.encode(), "big")


try:
    _ca = ctypes.cdll.LoadLibrary("/System/Library/Frameworks/CoreAudio.framework/CoreAudio")
except OSError:
    _ca = None


def mic_in_use():
    if not _ca:
        return False
    addr, size = _Addr(_cc("dev#"), _cc("glob"), 0), ctypes.c_uint32(0)
    if _ca.AudioObjectGetPropertyDataSize(1, ctypes.byref(addr), 0, None, ctypes.byref(size)):
        return False
    devs = (ctypes.c_uint32 * (size.value // 4))()
    if _ca.AudioObjectGetPropertyData(1, ctypes.byref(addr), 0, None, ctypes.byref(size), devs):
        return False
    for dev in devs:
        streams, n = _Addr(_cc("stm#"), _cc("inpt"), 0), ctypes.c_uint32(0)
        if _ca.AudioObjectGetPropertyDataSize(dev, ctypes.byref(streams), 0, None, ctypes.byref(n)) or not n.value:
            continue  # can't record
        running, v, vs = _Addr(_cc("gone"), _cc("glob"), 0), ctypes.c_uint32(0), ctypes.c_uint32(4)
        if not _ca.AudioObjectGetPropertyData(dev, ctypes.byref(running), 0, None, ctypes.byref(vs), ctypes.byref(v)) and v.value:
            return True
    return False


def zoom_meeting():
    return subprocess.run(["pgrep", "-x", "CptHost"], capture_output=True).returncode == 0


MEET_SCRIPT = """
set found to "no"
repeat with b in {"Google Chrome", "Arc", "Microsoft Edge"}
  if application b is running then
    tell application b
      repeat with w in windows
        repeat with t in tabs of w
          if (URL of t) contains "meet.google.com/" and (URL of t) does not end with "meet.google.com/" and (URL of t) does not contain "/landing" then set found to "yes"
        end repeat
      end repeat
    end tell
  end if
end repeat
return found
"""

TEAMS_SCRIPT = """
tell application "System Events"
  if exists process "Microsoft Teams" then
    repeat with w in (name of every window of process "Microsoft Teams")
      if w contains "Meeting" or w contains "Call" then return "yes"
    end repeat
  end if
end tell
return "no"
"""

warned = set()


def osa(name, script):
    """Runs an AppleScript; returns "yes", "no", or None if macOS refused (permission not given)."""
    try:
        out = subprocess.run(["osascript", "-e", script], capture_output=True, text=True, timeout=5)
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        if name not in warned:
            warned.add(name)
            print(f"Can't check {name}: {out.stderr.strip()[:120]}\n  Allow it in System Settings > Privacy & Security > "
                  f"{'Automation' if name == 'Meet' else 'Accessibility'}, for Terminal (or Python).", flush=True)
        return None
    return out.stdout.strip()


def browser_running():
    return any(subprocess.run(["pgrep", "-x", n], capture_output=True).returncode == 0
               for n in ("Google Chrome", "Arc", "Microsoft Edge"))


def call_in_progress():
    """A meeting is on somewhere, even if another app is in front. Returns (app id, label) or None."""
    if zoom_meeting():
        return "zoom", "Zoom"
    if mic_in_use():
        meet = osa("Meet", MEET_SCRIPT)
        # No permission to read the tabs: a browser recording from the mic is most likely Meet.
        if meet == "yes" or (meet is None and browser_running()):
            return "meet", "Google Meet"
    if osa("Teams", TEAMS_SCRIPT) == "yes":
        return "teams", "Teams"
    return None


def idle_seconds():
    out = subprocess.run(["ioreg", "-c", "IOHIDSystem"], capture_output=True, text=True).stdout
    m = re.search(r'"HIDIdleTime" = (\d+)', out)
    return int(m.group(1)) / 1e9 if m else 0


def classify(name):
    if not name or name in ("Finder", "loginwindow", "ScreenSaverEngine"):
        return "desktop", "Desktop"
    return APPS.get(name, (name.lower().replace(" ", ""), name))


def post(msg):
    req = urllib.request.Request(f"http://{PUCK}/ctx", data=json.dumps(msg).encode(),
                                 headers={"Content-Type": "application/json"})
    try:
        urllib.request.urlopen(req, timeout=3).read()
        return True
    except OSError:
        return False


def send(app, label):
    if app == "bambustudio":
        msg = {"t": "sidekick", "app": label, "why": "3D view", "view3d": True, "host": "mac"}
    else:
        msg = {"t": "ctx", "app": app, "label": label, "host": "mac"}
    req = urllib.request.Request(f"http://{PUCK}/ctx", data=json.dumps(msg).encode(),
                                 headers={"Content-Type": "application/json"})
    try:
        urllib.request.urlopen(req, timeout=3).read()
        return True
    except OSError as e:
        print(f"puck not reachable ({e}); retrying", flush=True)
        return False


last = None
active_sent = 0.0
print(f"Telling {PUCK} which app is in front. Ctrl+C to stop.")
call, call_seen, call_checked = None, 0.0, 0.0
while True:
    # Typing or moving the mouse here: the puck's keys come to the Mac right away
    # (at most one message every 2 s while you keep going).
    if idle_seconds() < 2.0 and time.time() - active_sent > 2:  # each pass takes about a second here
        if post({"t": "active", "host": "mac"}):
            active_sent = time.time()
    # Look for a call every 2 s; it ends 5 s after the signs go away (a brief blip doesn't drop it).
    if time.time() - call_checked > 2:
        call_checked = time.time()
        found = call_in_progress()
        if found:
            call, call_seen = found, time.time()
        elif call and time.time() - call_seen > 5:
            call = None
    if call:
        app, label = call  # a call wins over the front app: the dial mutes and sets call volume
    else:
        app, label = ("desktop", "Desktop") if idle_seconds() > AWAY_AFTER else classify(front_app())
    if app != last and send(app, label):
        print(f"{label} ({app})", flush=True)
        last = app
    time.sleep(0.7)
