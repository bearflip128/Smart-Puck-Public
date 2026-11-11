// Robot arm over Wi-Fi: the puck talks to an armctl-compatible arm server itself, with no
// computer bridge.
//
// Linked once from a computer with tools/arm_link.py, which sends
// {"t":"armnet","url":"https://arm.example.com","lan":"http://192.168.1.20:7002",
//  "user":"admin","pass":"..."} over USB. The login is kept in the puck's flash. "lan" is optional:
// when set, the puck tries it first (fast, at home) and falls back to "url". {"t":"armnet","clear":true}
// forgets it all. All network work runs in its own task, so slow requests never stall the screen.
#pragma once
#include <ArduinoJson.h>

namespace arm {

void begin();
// Handles {"t":"armnet",...}: link, or {"clear":true}. Answers with {"evt":"armnet",...} via poll.
void handleHost(JsonObjectConst msg);
// True once a login is saved; the bridge leaves the arm to the puck then.
bool linked();
// A knob event from Robot arm mode: a joint target ("shoulder_pan", degrees), or arm/disarm.
void moveJoint(const char *joint, float value);
void setArmed(bool armed);
// Fills `msg` with {"t":"arm","joints":{...},"status":...} for ui::host when there is something new.
bool poll(JsonDocument &msg);
// Fills `evt` with {"evt":"armnet",...} (linked, status changes, errors) for the computer.
bool pollEvent(JsonDocument &evt);
// Adds "armnet" (off / the current status) to a message.
void describe(JsonDocument &doc);

}  // namespace arm
