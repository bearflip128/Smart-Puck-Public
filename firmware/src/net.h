// Wi-Fi and internet time.
//
// On first boot (or after {"t":"wifi_reset"}) the puck opens a setup network called
// "Smart Puck setup". Join it from a phone or laptop and a page opens to pick your Wi-Fi and
// type its password; the puck saves it and joins on every boot after that.
// Once connected it sets its clock from the internet (NTP) in its time zone.
#pragma once
#include <ArduinoJson.h>

namespace net {

constexpr const char *SETUP_AP = "Smart Puck setup";

void begin(const char *posixTz);
void loop();
// Forget the saved Wi-Fi and open the setup network again.
void reset();
void setTimeZone(const char *posixTz);
// Adds "wifi" (setup/connecting/connected/offline), "ssid", "ip", "dns" and the clock's sync state to a message.
void describe(JsonDocument &doc);
// One line for the screen while Wi-Fi needs attention, or nullptr when connected.
const char *screenHint();
// True once the clock has been set from the internet (NTP, or a web server's Date header).
bool timeSynced();

}  // namespace net
