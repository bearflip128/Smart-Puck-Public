# Smart Puck firmware

Firmware for the 1.28" ESP32-S3 round knob display (ESP32-S3R8 with 8 MB PSRAM and 16 MB flash, GC9A01 240×240 round LCD, CST816D touch, rotary knob with push, 5 WS2812 RGB LEDs). It is written for PlatformIO with the Arduino framework and LVGL 9.

## Build and flash

You need Python 3 and PlatformIO:

```
python -m pip install platformio
```

The first flash goes over USB. Lift the display out of the case (or flash it before assembly), plug in the display's original USB cable, and run:

```
cd firmware
python -m platformio run -t upload
```

The board uses the ESP32-S3's built-in USB serial, so no driver is needed. PlatformIO finds the port on its own; add `--upload-port COM5` (Windows) or `--upload-port /dev/tty.usbmodem...` (Mac) if you have more than one.

The first build generates a random over-the-air update password in `firmware/ota_password.txt`. That file is git-ignored. Keep it: every later update needs the same password.

> **Back up the stock firmware first** if you might want it back. With the board plugged in:
> `python -m esptool --port COM5 read-flash 0 0x1000000 stock_16MB.bin`
> Restore it later with `python -m esptool --port COM5 write-flash 0 stock_16MB.bin`.

## First setup

1. **Wi-Fi.** On first boot the puck opens a Wi-Fi network called **Smart Puck setup**. Join it from your phone or computer; a page opens (or go to `http://192.168.4.1`). Pick your Wi-Fi (2.4 GHz) and type its password. The puck saves it and joins on every boot after that. It sets its clock from the internet.
2. **Time zone.** Hold the dial to open the menu, pick **Settings**, then **Time zone**.
3. **Bluetooth.** In **Settings > Bluetooth**, choose **Pair a new computer**, then add a Bluetooth device on your computer and pick **Smart Puck**. It pairs as a keyboard with media keys and a scroll wheel, so it works on Windows and macOS with nothing installed. The puck remembers two computers and sends keys to the one you choose in Settings.
4. **Web page.** Once it's on Wi-Fi, open `http://smart-puck.local/` (or the IP shown in **Settings > Wi-Fi**) for the status page, printer setup and an event log.

## Using it

- **Turn** the dial to use the current tool.
- **Press** for the main action (play/pause, mute, select).
- **Double-press** for the next tool.
- **Hold** for 0.6 s to open the mode menu. Turn to pick and press to choose. **Auto** follows your computer; anything else pins that mode. The menu picks the highlighted mode by itself after 5 s.

### Modes

- **Home**: watch faces (Atelier, Stack, Orbit and a Spotify face). Turning opens the mode menu.
- **Sidekick**: one adaptive mode that follows the app in front of you and hands the dial the right tools. The dial switches tabs first in every app (Ctrl+Tab); the app's own tools follow on double-press: scroll, word jump and cursor in editors, slides in PowerPoint and Keynote, track and volume in Spotify, scenes in OBS, and view rotation in Bambu Studio. In a meeting it becomes a mute button. Sidekick needs the companion app (below) to know which app is in front; without it, the puck still works as a Bluetooth volume, media and scroll controller.
- **Media**: Spotify's now playing, with play/pause, skip and volume.
- **Settings**: Wi-Fi, Bluetooth, light glow, brightness, time zone and about.

Other modes from the simulator are included too (Focus timer, Window switcher, Presenting, Design zoom, Video editing, Streaming and more), and you can pin any of them from the menu.

Printer and Spotify changes pop up over whatever is on screen: a full-screen alert when a print pauses, fails or finishes, and a short banner when a print starts or the song changes. They wait while you're on a call.

## Optional features

### Companion app (for Sidekick)

The companion runs on your computer and tells the puck which app is in front, over Wi-Fi. It needs no extra packages.

- **Windows:** `python firmware/tools/companion.py` (or double-click `firmware/tools/start_companion.cmd`).
- **macOS:** `python3 firmware/tools/companion_mac.py`. The first run asks to let Terminal control System Events; allow it.

Both look for the puck at `smart-puck.local`; pass its IP as an argument if your network doesn't resolve that. When you step away (no input for 2 minutes, or the screen is locked) the puck goes to Home. If both computers run a companion, keys go to the one you're using.

To add an app, map its program name to one of Sidekick's app ids in the `APPS` table at the top of the companion, and to tools in `RULES` in `src/ui.cpp`.

### Spotify

The puck talks to the Spotify Web API itself, so it works with your computer off. You link it once with your own free Spotify developer app: see [tools/SPOTIFY.md](tools/SPOTIFY.md). Showing what's playing works on any account; controls need Premium.

### Bambu Lab printer status

The puck reads print progress straight from a Bambu Lab printer on your network over local MQTT (tested with an A1 mini). On the puck's web page, enter the printer's **IP address**, **serial number** and **LAN access code** (all shown on the printer's screen under network settings). The access code stays on the puck.

Bambu printers accept only one local MQTT connection at a time, so close any other local tool that connects to the printer the same way.

### Robot arm

Robot arm mode drives a 6-joint arm (Base, Shoulder, Elbow, Wrist, Roll, Gripper) through an armctl-compatible HTTP server (`POST /login`, `GET /api/telemetry`, `POST /api/goal`). Turning moves the current joint 5° per click; a Safety layer arms and disarms the arm, and it disarms itself after 2 minutes without a turn. `tools/arm_link.py` saves the server address and login on the puck so it drives the arm over Wi-Fi; `tools/arm_bridge.py` relays from a computer over USB instead. This is an advanced feature and needs your own arm server.

## Over-the-air updates

After the first USB flash, update over Wi-Fi:

```
python -m platformio run -e ota -t upload
```

The puck must be on the same network. On Windows, allow Python through the firewall when asked (the upload listens on port 47333). If `smart-puck.local` doesn't resolve, set `upload_port` in `platformio.ini` to the puck's IP.

The charge board in the case passes power only, so the USB-C port on the case can't flash the puck. If an update over Wi-Fi isn't possible, lift the display out and use its original USB cable.

## Serial protocol

Over USB the puck speaks one JSON object per line at 115200 baud. `tools/puck_monitor.py` prints its events and lets you type an app name to switch modes the way the companion would.

Puck to computer:
- `{"evt":"hello","fw":"0.22.0","dev":"crowpanel128"}` at boot and in reply to `{"t":"hello"}`
- `{"evt":"action","mode":"media","in":"turn_right","do":"spotify.vol_up"}` for each turn or press
- `{"evt":"pin","mode":"media"}` when a mode is pinned from the menu (`"mode":null` for Auto)

Computer to puck:
- `{"t":"ctx","app":"zoom","title":"Design review"}` when an app comes to the front
- `{"t":"mic","muted":true}`
- `{"t":"time","epoch":1790000000}`: local time as Unix seconds
- `{"t":"modes",...}`: a mode lineup exported from the simulator (`tools/send_modes.py lineup.json`)
- `{"t":"wifi_reset"}`: forget Wi-Fi and open the setup network again

## Code layout

| File | What it does |
|------|--------------|
| `src/main.cpp` | Display, knob, touch, LEDs and the serial link |
| `src/ui.cpp` | Modes, Sidekick rules, menu, settings and screens |
| `src/faces.cpp` | Watch faces |
| `src/draw.*` | Drawing helpers that mirror the simulator, scaled from its 412-unit layout to the 240 px panel |
| `src/net.*` | Wi-Fi setup and internet time |
| `src/ble.*` | Bluetooth keyboard, media keys and scroll for two computers |
| `src/spotify.*` | Spotify Web API client |
| `src/printer.*` | Bambu printer over local MQTT, plus the puck's web page and status JSON |
| `src/arm.*` | Robot arm client |
| `src/ota.*` | Over-the-air updates |
| `src/leds.*` | RGB glow |
| `src/evlog.*` | Event log shown at `/log` |
| `tools/` | Companion apps, Spotify link, monitor and arm tools |

## Pins

LCD SPI: SCLK 10, MOSI 11, CS 9, DC 3, RST 14, backlight 46, LCD power enables 1 and 2. Encoder: A 45, B 42, switch 41. Touch I2C: SDA 6, SCL 7, INT 5, RST 13. RGB LEDs: 5 × WS2812 on 48.
