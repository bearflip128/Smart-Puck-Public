// The puck's screens, ported from puck-ui/puck-sim.html: the same modes, templates,
// watch faces and hold-for-mode menu, driven by the real knob.
#pragma once
#include <ArduinoJson.h>
#include <lvgl.h>

#include <memory>

namespace ui {

using SendFn = void (*)(JsonDocument &doc);

void begin(SendFn send);
void rotate(int d);
void press();
void hold();
// A message from the computer: ctx, mic or time.
void host(JsonObjectConst msg);
// Replaces the mode table with a {"t":"modes",...} lineup from the simulator. On success the
// puck keeps `doc` (the modes point into it). Fills `reply` with modes_ok or modes_err.
bool applyModes(std::unique_ptr<JsonDocument> doc, JsonDocument &reply);
// POSIX time zone rules for the clock (from Idle's settings.tz in the lineup; Chicago by default).
const char *timeZone();
// Settings mode hooks: backlight control (5-100 %), the version shown in About, and loading the
// saved time zone and brightness (call after the lineup is loaded).
void setBacklight(void (*fn)(int pct));
void setFirmwareVersion(const char *v);
void loadPrefs();
// Goes back to the built-in mode table.
void resetModes();
// Forwards "spotify.*" actions (play_pause, next, prev, vol_up, vol_down) to the Spotify module.
void setSpotifyControl(void (*fn)(const char *action));
// True while the Media screen is showing (Spotify polls faster then).
bool mediaOnScreen();
// A touch gesture (touch.h): swipe left = next track, right = previous, on the Media screen.
bool gesture(int g);
// Call every loop: shows whatever has activity (music playing, a print running), cycling every
// 10 s when several do, and falls back to Home. Returns true when the screen changed.
bool autoSelect();
// The startup animation (Segments, 2.6 s over Home). Any turn or press skips it. bootRunning() also
// ends it when its time is up.
void startBoot();
bool bootRunning();
// Draws the current screen onto an LVGL layer (called from the screen object's draw event).
void draw(lv_layer_t *layer);

}  // namespace ui
