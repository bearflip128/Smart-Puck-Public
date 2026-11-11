// CST816D touch controller on the 1.28" knob display (I2C: SDA 6, SCL 7, INT 5, RST 13).
// Only gestures are used: swipes skip tracks on the Media screen.
#pragma once

namespace touch {

enum Gesture { NONE = 0, SWIPE_UP = 1, SWIPE_DOWN = 2, SWIPE_LEFT = 3, SWIPE_RIGHT = 4, TAP = 5 };

void begin();
// Returns a gesture once per touch (0 when there is none).
int poll();
// "ok" if the controller answered, otherwise "not found".
const char *status();

}  // namespace touch
