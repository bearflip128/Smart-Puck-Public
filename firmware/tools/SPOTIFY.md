# Spotify on the puck

The puck shows what's playing on your Spotify account, on any device (phone, computer, speaker), and can control playback. It talks to the Spotify Web API itself over Wi-Fi, so it works with your computer off.

## Link your account (one time)

1. Create an app at https://developer.spotify.com/dashboard.
   - Redirect URI: `http://127.0.0.1:8888/callback` (exactly this; Spotify no longer allows `localhost`)
   - API: Web API
   - Copy its **Client ID**. No client secret is needed, because the link uses PKCE.
2. Plug the puck into your computer with its original USB cable and close anything else using the serial port.
3. Run `python tools/spotify_link.py <CLIENT_ID> COM5` (use your port; on Mac or Linux it looks like `/dev/tty.usbmodem...`). It opens your browser to sign in, checks what Spotify sees, and sends the link to the puck.

Reading what's playing works on any account. Play/pause, skip and volume need Spotify Premium.

## How the firmware does it
- `{"t":"spotify","client_id","refresh_token"}`: save to NVS, refresh once, answer `{"evt":"spotify","state":"linked"}` or `{"evt":"spotify","state":"error","error":"..."}`. `{"t":"spotify","unlink":true}` clears it.
- Token: POST `https://accounts.spotify.com/api/token`, form `grant_type=refresh_token&refresh_token=..&client_id=..`. Access token lasts 1 h (`expires_in`). The response may carry a new `refresh_token`: persist it every time (PKCE refresh tokens rotate; the old one stops working). On 400 invalid_grant, report `state:"error","error":"relink"`.
- Poll GET `https://api.spotify.com/v1/me/player` (Bearer). 200 = JSON, 204 = nothing playing. Every 3 s while Media is on screen, every 15 s otherwise. On 429 wait `Retry-After` seconds. Use an ArduinoJson filter: `is_playing`, `progress_ms`, `currently_playing_type`, `device.name`, `device.volume_percent`, `item.name`, `item.duration_ms`, `item.artists[0].name`, `item.show.name` (podcasts).
- Media screen: track, artist, progress ring counting locally between polls, play/pause icon, and a line `Spotify · <device name>`. Nothing playing: "Nothing playing".
- Controls (Premium; on 403 show "Needs Spotify Premium"): `spotify.play_pause` PUT `/v1/me/player/pause` or `/play`; `spotify.next` POST `/v1/me/player/next`; `spotify.prev` POST `/previous`; `spotify.vol_up/vol_down` PUT `/v1/me/player/volume?volume_percent=N` (±5, debounce ~300 ms so a fast turn sends one request). Requests with no body need `Content-Length: 0`.
- Events: `{"evt":"spotify","state":"playing|paused|idle|linked|error",...}` on change, for the log.
- The existing `ctx` track/artist from the computer still works; live Spotify data wins when linked.
