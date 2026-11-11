// Bluetooth LE keyboard for the puck (the ESP32-S3 has BLE only, which is what PCs and Macs use
// for keyboards). The puck appears as "Smart Puck", a keyboard + media keys + mouse wheel, and
// can stay paired with two computers at once; the settings pick which one receives the keys.
//
// "usb.*" actions (volume, mute, play/pause, next/prev, scroll, key combos) go out over Bluetooth
// when a computer is connected. A host named with "Mac" gets Cmd where a PC gets Ctrl.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ble {

void begin();
void loop();
// Stops Bluetooth until the next restart so a firmware update gets the whole radio.
void suspend();
// Undoes suspend() (a firmware update that failed).
void resume();
// When (millis) a computer last connected but couldn't encrypt: it needs to be paired again. 0 = never.
uint32_t stalePairingAt();

// Sends a "usb.*" action (without the "usb." prefix, e.g. "volume_up" or "key" + "ctrl+z").
// Returns false if no computer is connected.
bool send(const char *action, const char *arg);

int connectedCount();
// Name of connected computer `i` (0..connectedCount()-1) and whether it is the one receiving keys.
const char *connectedName(int i);
bool connectedIsTarget(int i);
void setTarget(int i);
bool connectedIsMac(int i);
// Sends keys to the connected Mac (true) or the connected PC (false), if there is one.
void preferMac(bool mac);
// A companion saw keyboard or mouse use on that computer just now: its keys go there, and for 30 s a
// different computer connecting doesn't take them.
void hostActive(bool mac);
// Counts paired computers connecting (each connection once it is encrypted); goes up by one each time.
uint32_t connectCount();
// True if a computer tried to pair outside the pairing window after `since` (millis).
bool refusedSince(uint32_t since);

// Pairing: a new computer can pair only while this is on (2 minutes, or until one pairs).
void startPairing();
bool pairing();
int pairingSecondsLeft();
void forgetAll();

// Friendly names for paired computers (e.g. "PC", "Mac"), by pairing order; saved in flash.
int bondCount();
const char *bondName(int i);
void setBondName(int i, const char *name);

}  // namespace ble
