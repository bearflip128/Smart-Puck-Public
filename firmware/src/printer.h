// Live status from a Bambu printer over its local MQTT (TLS, port 8883, user "bblp",
// password = the printer's LAN access code, topic device/<serial>/report).
//
// The connection details are entered on the puck's own web page (http://<puck ip>/), so the
// access code is typed by you and stays on the puck. The MQTT connection runs in its own task
// so a slow TLS handshake never stalls the screen.
#pragma once
#include <ArduinoJson.h>

namespace printer {

void begin();
// Call from the main loop: serves the settings page and, when the printer reported something
// new, fills `msg` with {"t":"printer",...} for ui::host and returns true.
bool poll(JsonDocument &msg);
// Adds "printer" (unset/connecting/connected/error), "printer_ip" and "printer_error".
void describe(JsonDocument &doc);

}  // namespace printer
