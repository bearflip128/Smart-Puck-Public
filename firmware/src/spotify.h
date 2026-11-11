// Spotify now-playing and controls over Wi-Fi (Spotify Web API, PKCE refresh token).
//
// The account is linked once from a computer with tools/spotify_link.py, which sends
// {"t":"spotify","client_id","refresh_token"} over USB. The refresh token is kept in the
// puck's flash and rewritten whenever Spotify rotates it. All network work runs in its own
// task, so slow requests never stall the screen.
#pragma once
#include <ArduinoJson.h>

namespace spotify {

void begin();
// Handles {"t":"spotify",...}: link (client_id + refresh_token) or {"unlink":true}.
void handleHost(JsonObjectConst msg);
// Poll fast (3 s) while the Media screen is showing, every 6 s otherwise.
void setFast(bool fast);
// A control from the knob: "play_pause", "next", "prev", "vol_up" or "vol_down".
void control(const char *action);
// Fills `msg` with {"t":"nowplaying",...} for ui::host when there is something new.
bool poll(JsonDocument &msg);
// Fills `evt` with {"evt":"spotify",...} (linked, error, playing/paused/idle changes).
bool pollEvent(JsonDocument &evt);
bool linked();
// Adds "spotify" (state), "spotify_error" and "spotify_detail" (why the last request failed).
void describe(JsonDocument &doc);

}  // namespace spotify
