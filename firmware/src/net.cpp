#include "net.h"

#include "evlog.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_sntp.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <lwip/dns.h>
#include <time.h>

namespace net {

static WiFiManager wm;
static bool portalOpen = false;
static String tz;

static volatile bool synced = false;
static const char *volatile timeSource = "build";  // build, ntp, http
static volatile time_t syncedAt = 0;
static uint32_t nextTimeTry = 0;  // millis() of the next fallback attempt while the clock isn't set

// A router that hands out no DNS server (or the setup network's own address, left over after the
// setup page was used) breaks every name lookup: NTP and Spotify. Keep the
// router's server and add public ones behind it.
static void fixDns() {
  IPAddress d0 = WiFi.dnsIP(0);
  if (d0 == IPAddress(0, 0, 0, 0) || d0 == IPAddress(192, 168, 4, 1)) {
    ip_addr_t gw = IPADDR4_INIT((uint32_t)WiFi.gatewayIP());
    dns_setserver(0, &gw);
  }
  ip_addr_t cf = IPADDR4_INIT_BYTES(1, 1, 1, 1);
  dns_setserver(1, &cf);
}

static void startTime() {
  nextTimeTry = millis() + 20000;
  fixDns();
#if LWIP_DHCP_GET_NTP_SRV
  sntp_servermode_dhcp(0);  // a router's NTP server (often one it doesn't run) would replace ours
#endif
  sntp_set_time_sync_notification_cb([](struct timeval *) {
    synced = true;
    timeSource = "ntp";
    syncedAt = time(nullptr);
  });
  // The last server is time.google.com by address, so the clock still sets if name lookups fail.
  configTzTime(tz.c_str(), "pool.ntp.org", "time.google.com", "216.239.35.4");
}

// If NTP (UDP port 123) is blocked or slow, read the time from a web server's Date header instead.
// Runs once in its own task; HTTP takes up to a few seconds.
static int monthIndex(const char *m) {
  static const char *names = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char *p = strstr(names, m);
  return p && strlen(m) == 3 ? (p - names) / 3 : -1;
}

static time_t utcFromFields(int y, int mon, int d, int h, int mi, int sec) {
  // Days from 1970-01-01 to y-mon-d (mon 0-11), no time zone involved.
  static const int before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  long days = (y - 1970) * 365L + (y - 1969) / 4 - (y - 1901) / 100 + (y - 1601) / 400 + before[mon] + d - 1;
  if (mon > 1 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) days++;
  return (time_t)days * 86400 + h * 3600 + mi * 60 + sec;
}

static volatile bool httpTimeBusy = false;

static void httpTimeTask(void *) {
  static const char *urls[] = {"http://www.google.com/generate_204", "http://captive.apple.com/"};
  for (const char *url : urls) {
    if (synced) break;
    WiFiClient c;
    HTTPClient http;
    http.setTimeout(5000);
    const char *keys[] = {"Date"};
    http.collectHeaders(keys, 1);
    if (!http.begin(c, url)) continue;
    int code = http.GET();
    String date = code > 0 ? http.header("Date") : "";
    http.end();
    // "Tue, 03 Nov 2026 04:10:00 GMT"
    char wd[4], mon[4];
    int d, y, h, mi, sec;
    if (date.length() && sscanf(date.c_str(), "%3s, %d %3s %d %d:%d:%d", wd, &d, mon, &y, &h, &mi, &sec) == 7 &&
        monthIndex(mon) >= 0 && y >= 2024 && !synced) {
      struct timeval tv = {utcFromFields(y, monthIndex(mon), d, h, mi, sec), 0};
      settimeofday(&tv, nullptr);
      synced = true;
      timeSource = "http";
      syncedAt = tv.tv_sec;
      Serial.printf("{\"evt\":\"time\",\"source\":\"http\",\"utc\":%ld}\n", (long)tv.tv_sec);
      break;
    }
  }
  httpTimeBusy = false;
  vTaskDelete(nullptr);
}

bool timeSynced() { return synced; }

void setTimeZone(const char *posixTz) {
  tz = posixTz;
  setenv("TZ", posixTz, 1);
  tzset();
  if (WiFi.status() == WL_CONNECTED) startTime();
}

// Why the router dropped us (esp_wifi's reason codes), in words for the log.
static const char *dropReason(int r) {
  switch (r) {
    case 2: return "auth expired";
    case 3: return "router kicked us";
    case 4: return "idle too long";
    case 8: return "we left";
    case 15: return "handshake timeout";
    case 200: return "lost the router's beacon";
    case 201: return "network not found";
    case 202: return "wrong password";
    case 203: return "join refused";
    case 204: return "handshake timeout";
    default: return "";
  }
}

static Preferences prefs;  // a copy of the Wi-Fi name and password, in case the radio's own copy is lost
static char savedSsid[33] = "";
static bool haveSaved = false;
static uint32_t lostAt = 0, nextTry = 0;
static int attempt = 0;
static volatile int lastDrop = -1;

// The network the radio remembers (WiFiManager saves it there), or our own copy of it.
static void loadSaved() {
  wifi_config_t conf = {};
  esp_wifi_get_config(WIFI_IF_STA, &conf);
  String ssid = (const char *)conf.sta.ssid;
  prefs.begin("net", false);
  String bSsid = prefs.getString("ssid", ""), bPass = prefs.getString("pass", "");
  if (!ssid.length() && bSsid.length()) {
    evlog::add("wifi restored saved network %s from backup", bSsid.c_str());
    WiFi.persistent(true);
    WiFi.begin(bSsid.c_str(), bPass.c_str());  // also puts it back in the radio's memory
    ssid = bSsid;
  } else if (ssid.length()) {
    WiFi.begin();
  }
  snprintf(savedSsid, sizeof savedSsid, "%s", ssid.c_str());
  haveSaved = ssid.length() > 0;
}

// Once joined, keep our copy in step with what WiFiManager saved.
static void backupSaved() {
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK || !conf.sta.ssid[0]) return;
  String ssid = (const char *)conf.sta.ssid, pass = String((const char *)conf.sta.password).substring(0, 64);
  if (prefs.getString("ssid", "") != ssid || prefs.getString("pass", "") != pass) {
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
  }
  snprintf(savedSsid, sizeof savedSsid, "%s", ssid.c_str());
  haveSaved = true;
}

void begin(const char *posixTz) {
  setTimeZone(posixTz);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    int r = info.wifi_sta_disconnected.reason;
    if (r == lastDrop && lostAt) return;  // the same reason again while retrying: logged once
    lastDrop = r;
    evlog::add("wifi drop reason=%d %s", r, dropReason(r));
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
    evlog::add("wifi up ip=%s ch=%d", WiFi.localIP().toString().c_str(), WiFi.channel());
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  wm.setConfigPortalBlocking(false);  // the knob and screen keep working while the setup page is open
  wm.setConnectTimeout(30);
  wm.setTitle("Smart Puck");
  wm.setShowInfoUpdate(false);
  // With a saved network, never open the setup network on our own: a slow router after a power
  // cut used to land here and look like the setup was lost. Join in the background and keep trying.
  loadSaved();
  if (!haveSaved) {
    evlog::add("wifi nothing saved: setup network open");
    wm.startConfigPortal(SETUP_AP);
    portalOpen = true;
  }
}

void loop() {
  static bool wasConnected = false;
  uint32_t now = millis();
  if (portalOpen) {
    wm.process();
    if (WiFi.status() == WL_CONNECTED) {
      wm.stopConfigPortal();
      WiFi.mode(WIFI_STA);
      portalOpen = false;
      evlog::add("wifi setup done");
    }
  }
  bool connected = WiFi.status() == WL_CONNECTED;
  // (Wi-Fi modem sleep must stay on: with Bluetooth running, the radio coexistence code aborts without it.)
  if (connected && !wasConnected) {
    startTime();
    backupSaved();
    if (lostAt) evlog::add("wifi back after %lus, %d tries", (unsigned long)((now - lostAt) / 1000), attempt);
    lostAt = 0, attempt = 0, lastDrop = -1;
  }
  wasConnected = connected;
  // Still no time 20 s after joining: try a web server's clock, and start NTP over (every minute).
  if (connected && !synced && !httpTimeBusy && (int32_t)(now - nextTimeTry) >= 0) {
    httpTimeBusy = true;
    if (xTaskCreatePinnedToCore(httpTimeTask, "httptime", 6 * 1024, nullptr, 1, nullptr, 0) != pdPASS) httpTimeBusy = false;
    startTime();
    nextTimeTry = now + 60000;
  }
  // Reconnect with backoff: 2, 5, 10, 20, 40 s, then every minute, for as long as it takes. Every
  // fifth try restarts the radio in case it is stuck.
  static const uint16_t BACKOFF_S[] = {2, 5, 10, 20, 40, 60};
  if (connected || portalOpen || !haveSaved) return;
  if (!lostAt) {
    lostAt = now ? now : 1;
    nextTry = now + BACKOFF_S[0] * 1000;
    return;
  }
  if ((int32_t)(now - nextTry) < 0) return;
  attempt++;
  if (attempt % 5 == 0) {
    evlog::add("wifi try %d: restarting the radio", attempt);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_STA);
  } else {
    WiFi.disconnect();
  }
  WiFi.begin();
  nextTry = now + BACKOFF_S[attempt < 6 ? attempt : 5] * 1000;
}

void reset() {
  evlog::add("wifi reset from settings");
  wm.resetSettings();
  WiFi.disconnect(true, true);
  prefs.remove("ssid");
  prefs.remove("pass");
  haveSaved = false;
  savedSsid[0] = 0;
  wm.startConfigPortal(SETUP_AP);  // non-blocking: returns right away
  portalOpen = true;
}

void describe(JsonDocument &doc) {
  if (portalOpen) doc["wifi"] = "setup";
  else if (WiFi.status() == WL_CONNECTED) doc["wifi"] = "connected";
  else if (haveSaved) doc["wifi"] = "reconnecting";
  else doc["wifi"] = "offline";
  if (attempt) doc["wifi_tries"] = attempt;
  if (WiFi.status() == WL_CONNECTED) {
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["dns"] = WiFi.dnsIP(0).toString() + " " + WiFi.dnsIP(1).toString();
  }
  doc["time_synced"] = (bool)synced;
  doc["time_source"] = (const char *)timeSource;
  doc["time_utc"] = (long long)time(nullptr);
  if (syncedAt) doc["time_synced_at"] = (long long)syncedAt;
}

const char *screenHint() {
  static char buf[64];
  if (portalOpen) return "Wi-Fi setup: join \"Smart Puck setup\"";
  if (WiFi.status() == WL_CONNECTED) return nullptr;
  if (!haveSaved) return "Wi-Fi offline";
  if (attempt) snprintf(buf, sizeof buf, "Reconnecting to %s (try %d)", savedSsid, attempt);
  else snprintf(buf, sizeof buf, "Connecting to %s", savedSsid);
  return buf;
}

}  // namespace net
