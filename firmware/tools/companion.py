"""Smart Puck companion for Windows: tells the puck which app is in front, over Wi-Fi.

The puck's Sidekick mode then gives the dial the right tools for that app (scroll, word jump,
undo in an editor; slides in PowerPoint; volume and track in Spotify...). When a text box has
focus, the dial moves the text cursor instead. When you step away
(no keyboard or mouse for 2 minutes, or the screen locked) it sends "desktop" and the puck goes Home.

Usage:  python companion.py [puck address]     (default: smart-puck.local)
No extra packages needed. Leave it running; Ctrl+C to stop.
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import subprocess
import sys
import time
import urllib.request

PUCK = sys.argv[1] if len(sys.argv) > 1 else "smart-puck.local"
AWAY_AFTER = 120  # seconds without input

# Program file name -> the app ids Sidekick knows (see RULES in firmware/src/ui.cpp).
APPS = {
    "code.exe": ("vscode", "VS Code"), "winword.exe": ("word", "Word"), "spotify.exe": ("spotify", "Spotify"),
    "adobe premiere pro.exe": ("premiere", "Premiere Pro"), "figma.exe": ("figma", "Figma"),
    "powerpnt.exe": ("powerpoint", "PowerPoint"), "slack.exe": ("slack", "Slack"), "obs64.exe": ("obs", "OBS"),
    "zoom.exe": ("zoom", "Zoom"), "ms-teams.exe": ("teams", "Teams"), "teams.exe": ("teams", "Teams"),
    "bambu-studio.exe": ("bambustudio", "Bambu Studio"), "chrome.exe": ("chrome", "Chrome"), "msedge.exe": ("chrome", "Edge"), "firefox.exe": ("chrome", "Firefox"),
}

user32, kernel32 = ctypes.windll.user32, ctypes.windll.kernel32


class LASTINPUTINFO(ctypes.Structure):
    _fields_ = [("cbSize", wt.UINT), ("dwTime", wt.DWORD)]


def idle_seconds():
    info = LASTINPUTINFO(ctypes.sizeof(LASTINPUTINFO))
    user32.GetLastInputInfo(ctypes.byref(info))
    return (kernel32.GetTickCount() - info.dwTime) / 1000.0


class GUITHREADINFO(ctypes.Structure):
    _fields_ = [("cbSize", wt.DWORD), ("flags", wt.DWORD), ("hwndActive", wt.HWND), ("hwndFocus", wt.HWND),
                ("hwndCapture", wt.HWND), ("hwndMenuOwner", wt.HWND), ("hwndMoveSize", wt.HWND),
                ("hwndCaret", wt.HWND), ("rcCaret", wt.RECT)]


def typing():
    """True when the front window has a text cursor (a text box has focus). Classic and Office
    controls, Chromium and Electron apps (Chrome, Edge, VS Code, Slack) all show a system caret."""
    hwnd = user32.GetForegroundWindow()
    if not hwnd:
        return False
    info = GUITHREADINFO(ctypes.sizeof(GUITHREADINFO))
    tid = user32.GetWindowThreadProcessId(hwnd, None)
    if not user32.GetGUIThreadInfo(tid, ctypes.byref(info)):
        return False
    return bool(info.hwndCaret) or bool(info.flags & 0x1)  # GUI_CARETBLINKING


def front_app():
    hwnd = user32.GetForegroundWindow()
    if not hwnd:
        return None, ""
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    title = ctypes.create_unicode_buffer(512)
    user32.GetWindowTextW(hwnd, title, 512)
    h = kernel32.OpenProcess(0x1000, False, pid.value)  # PROCESS_QUERY_LIMITED_INFORMATION
    exe = ""
    if h:
        buf, size = ctypes.create_unicode_buffer(520), wt.DWORD(520)
        if kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
            exe = os.path.basename(buf.value).lower()
        kernel32.CloseHandle(h)
    return exe, title.value


EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def window_titles():
    """Titles of all visible top-level windows (for spotting a call that isn't in front)."""
    titles = []

    def cb(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, buf, 256)
            if buf.value:
                titles.append(buf.value)
        return True

    user32.EnumWindows(EnumWindowsProc(cb), 0)
    return titles


def zoom_meeting():
    """Zoom runs CptHost.exe only while you're in a meeting (checked every 3 s; tasklist is slow)."""
    global _zoom_at, _zoom_on
    if time.time() - _zoom_at > 3:
        _zoom_at = time.time()
        try:
            out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq CptHost.exe", "/NH"], capture_output=True, text=True,
                                 timeout=5, creationflags=0x08000000).stdout  # CREATE_NO_WINDOW
            _zoom_on = "cpthost.exe" in out.lower()
        except (OSError, subprocess.SubprocessError):
            _zoom_on = False
    return _zoom_on


_zoom_at, _zoom_on = 0.0, False


def call_in_progress():
    """A meeting is open somewhere: a Zoom meeting, a Teams call, or a Google Meet tab
    showing in a browser window. Returns (app id, label) or None."""
    if zoom_meeting():
        return "zoom", "Zoom"
    for t in window_titles():
        tl = t.lower()
        if t == "Zoom Meeting" or tl.startswith("zoom meeting"):
            return "zoom", "Zoom"
        if "meet - " in tl or tl.startswith("meet ") or "meet.google.com" in tl:
            return "meet", "Google Meet"
        if "microsoft teams" in tl and ("meeting" in tl or "call" in tl):
            return "teams", "Teams"
    return None


def classify(exe, title):
    if exe in APPS:
        app, label = APPS[exe]
        if app == "chrome":  # a few web apps get their own rules
            t = title.lower()
            if "youtube" in t:
                return "youtube", "YouTube"
            if "meet.google.com" in t or t.startswith("meet -"):
                return "meet", "Google Meet"
        return app, label
    if not exe or exe in ("explorer.exe", "lockapp.exe", "searchhost.exe", "shellexperiencehost.exe"):
        return "desktop", "Desktop"
    return exe.removesuffix(".exe"), exe.removesuffix(".exe").capitalize()  # Sidekick's default tools


def post(msg):
    body = json.dumps(msg).encode()
    req = urllib.request.Request(f"http://{PUCK}/ctx", data=body, headers={"Content-Type": "application/json"})
    try:
        urllib.request.urlopen(req, timeout=3).read()
        return True
    except OSError:
        return False


def send(app, label, text):
    if app == "bambustudio":
        # Bambu Studio's 3D view: the dial turns the build plate (spin and tilt) instead of the app tools.
        msg = {"t": "sidekick", "app": label, "why": "3D view", "view3d": True}
    else:
        msg = {"t": "ctx", "app": app, "label": label, "text": text}
    body = json.dumps(msg).encode()
    req = urllib.request.Request(f"http://{PUCK}/ctx", data=body, headers={"Content-Type": "application/json"})
    try:
        urllib.request.urlopen(req, timeout=3).read()
        return True
    except OSError as e:
        print(f"puck not reachable ({e}); retrying", flush=True)
        return False


last = None
active_sent = 0.0
print(f"Telling {PUCK} which app is in front. Ctrl+C to stop.")
while True:
    # Typing or moving the mouse here: the puck's keys come to this computer right away
    # (at most one message every 2 s while you keep going).
    if idle_seconds() < 1.0 and time.time() - active_sent > 2:
        if post({"t": "active", "host": "pc"}):
            active_sent = time.time()
    exe, title = front_app()
    call = call_in_progress()
    if call:
        app, label = call  # a call wins over the front app: the dial mutes and sets call volume
    else:
        app, label = ("desktop", "Desktop") if idle_seconds() > AWAY_AFTER else classify(exe, title)
    # In a text box, Sidekick's dial becomes the arrow keys (by letter, double-press for by word).
    text = app not in ("desktop", "bambustudio") and typing()
    if (app, text) != last and send(app, label, text):
        print(f"{label} ({app}){' - typing' if text else ''}", flush=True)
        last = (app, text)
    time.sleep(0.5)
