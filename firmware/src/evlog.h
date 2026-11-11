// A small event log in flash that survives restarts, for finding out why Wi-Fi or Bluetooth
// dropped. Each line gets the clock time, uptime, Wi-Fi signal, Bluetooth connection count and
// free memory. Read it at http://<puck>/log (newest last).
//
// add() is safe from any task or event callback: lines wait in RAM and flush() (called from the
// main loop) writes them to /log.txt, starting /log.old when it passes 24 KB.
#pragma once
#include <Arduino.h>

namespace evlog {

void begin(const char *fwVersion);
void add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void flush();
// The whole log (previous file, then the current one) as text.
String read();
// The most recent line that starts with `prefix` (e.g. "wifi drop"), or "".
String last(const char *prefix);

}  // namespace evlog
