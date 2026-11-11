"""Link the puck to your Spotify account (one time).

The puck then asks Spotify itself, over Wi-Fi, what's playing on any of your devices
(phone, laptop, speaker), so it works even when this computer is off.

Before running: create a Spotify app at https://developer.spotify.com/dashboard
  - Redirect URI: http://127.0.0.1:8888/callback   (exactly this)
  - API: Web API
  - Copy its Client ID. No client secret is needed (this uses PKCE).

Usage:  python spotify_link.py CLIENT_ID [PORT]   (PORT defaults to COM5; on Mac/Linux pass /dev/tty...)
Opens your browser to sign in, checks what's playing, then sends the link to the puck
over USB. Close anything else holding the port first (arm_bridge.py, puck_monitor.py).
Needs: pip install pyserial
"""
import base64
import hashlib
import http.server
import json
import secrets
import sys
import time
import urllib.parse
import urllib.request
import webbrowser

REDIRECT = "http://127.0.0.1:8888/callback"
SCOPES = "user-read-playback-state user-read-currently-playing user-modify-playback-state"

args = [a for a in sys.argv[1:] if not a.upper().startswith("COM") and not a.startswith("/dev/")]
ports = [a for a in sys.argv[1:] if a not in args]
if not args:
    sys.exit(__doc__)
client_id = args[0]
port = ports[0] if ports else "COM5"

verifier = secrets.token_urlsafe(64)
challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
state = secrets.token_urlsafe(16)
auth_url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode({
    "client_id": client_id, "response_type": "code", "redirect_uri": REDIRECT, "scope": SCOPES,
    "code_challenge_method": "S256", "code_challenge": challenge, "state": state})

result = {}


class Callback(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        if q.get("state", [""])[0] == state:
            result.update({k: v[0] for k, v in q.items()})
        ok = "code" in result
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(("<h2>Linked. You can close this tab.</h2>" if ok else
                          f"<h2>Not linked: {result.get('error', 'no code')}</h2>").encode())

    def log_message(self, *a):
        pass


server = http.server.HTTPServer(("127.0.0.1", 8888), Callback)
print("Opening Spotify sign-in in your browser. If it doesn't open, visit:\n" + auth_url)
webbrowser.open(auth_url)
while "code" not in result and "error" not in result:
    server.handle_request()
if "error" in result:
    sys.exit(f"Spotify said: {result['error']}")


def post(url, fields):
    req = urllib.request.Request(url, data=urllib.parse.urlencode(fields).encode(),
                                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    with urllib.request.urlopen(req) as r:
        return json.load(r)


tok = post("https://accounts.spotify.com/api/token", {
    "grant_type": "authorization_code", "code": result["code"], "redirect_uri": REDIRECT,
    "client_id": client_id, "code_verifier": verifier})

req = urllib.request.Request("https://api.spotify.com/v1/me/player",
                             headers={"Authorization": "Bearer " + tok["access_token"]})
with urllib.request.urlopen(req) as r:
    body = r.read()
if body:
    p = json.loads(body)
    item = p.get("item") or {}
    who = ", ".join(a["name"] for a in item.get("artists", [])) or (item.get("show") or {}).get("name", "")
    print(f"Spotify sees: {item.get('name', '?')} by {who} on {(p.get('device') or {}).get('name', '?')}"
          f" ({'playing' if p.get('is_playing') else 'paused'})")
else:
    print("Spotify sees nothing playing right now (that's fine).")

# The puck keeps the refresh token and renews it itself. Spotify rotates refresh tokens,
# so the puck is the only copy that stays valid; nothing is saved on this computer.
line = json.dumps({"t": "spotify", "client_id": client_id, "refresh_token": tok["refresh_token"]},
                  separators=(",", ":")).encode() + b"\n"
import serial  # noqa: E402

s = serial.Serial()
# Leave DTR/RTS low: toggling RTS resets the ESP32-S3 over USB-Serial/JTAG.
s.port, s.baudrate, s.timeout, s.dtr, s.rts = port, 115200, 0.2, False, False
s.open()
s.write(line)
end = time.time() + 8
while time.time() < end:
    raw = s.readline()
    try:
        msg = json.loads(raw) if raw else {}
    except ValueError:
        continue
    if msg.get("evt") == "spotify":
        print("Puck:", msg.get("state"), msg.get("error", ""))
        sys.exit(0 if msg.get("state") != "error" else 1)
sys.exit("Sent, but the puck didn't answer. Its firmware may not have Spotify support yet.")
