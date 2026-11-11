// Over-the-air updates through Wi-Fi (ArduinoOTA, password-protected, smart-puck.local).
// Upload with: python -m platformio run -e ota -t upload
#pragma once

namespace ota {

// `progress` is called with 0-100 while an update is written, and -1 if it fails.
void begin(void (*progress)(int pct));
void loop();

}  // namespace ota
