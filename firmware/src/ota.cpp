#include "ota.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "ble.h"
#include "evlog.h"

#ifndef OTA_PASSWORD
#error "OTA_PASSWORD is set by scripts/ota_secret.py"
#endif

namespace ota {

static void (*progressFn)(int) = nullptr;
static bool started = false;

void begin(void (*progress)(int pct)) { progressFn = progress; }

// Starts once the puck is on Wi-Fi; mDNS (smart-puck.local) comes up with it.
void loop() {
  if (!started) {
    if (WiFi.status() != WL_CONNECTED || WiFi.getMode() != WIFI_STA) return;
    ArduinoOTA.setHostname("smart-puck");
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([] {
      evlog::add("ota start");
      evlog::flush();
      ble::suspend();  // Wi-Fi and Bluetooth share one radio; an update needs all of it
      if (progressFn) progressFn(0);
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
      static int last = -1;
      int pct = total ? done * 100 / total : 0;
      if (pct != last && progressFn) progressFn(pct);
      last = pct;
    });
    ArduinoOTA.onError([](ota_error_t e) {
      evlog::add("ota failed error=%d", (int)e);
      ble::resume();  // Bluetooth stayed off after a failed update until the next restart
      if (progressFn) progressFn(-1);
    });
    ArduinoOTA.begin();
    MDNS.addService("http", "tcp", 80);  // the printer settings page
    started = true;
  }
  ArduinoOTA.handle();
}

}  // namespace ota
