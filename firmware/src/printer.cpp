#include "printer.h"

#include "ble.h"
#include "evlog.h"
#include "net.h"
#include "spotify.h"
#include "ui.h"
#include "touch.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

extern uint32_t diagLoopAvg, diagLoopMax;  // main.cpp

namespace printer {

// No defaults: the printer's IP, serial number and LAN access code are entered on the puck's web page.
static const char *DEFAULT_IP = "";
static const char *DEFAULT_SERIAL = "";

struct Config {
  String ip, serial, code;
  bool ready() const { return ip.length() && serial.length() && code.length(); }
};

// Printer fields the screen uses, merged from full and partial reports.
struct Status {
  String state, file;
  float percent = NAN, remaining = NAN, nozzle = NAN, bed = NAN;
  int layer = -1, totalLayers = -1;
};

static Config cfg;
static Status status;
static SemaphoreHandle_t lock;
static volatile bool changed = false, reconnect = false;
static volatile const char *state = "unset";
static String lastError;
static WebServer server(80);
static bool serverStarted = false;

static void loadConfig() {
  Preferences p;
  p.begin("printer", true);
  cfg.ip = p.getString("ip", DEFAULT_IP);
  cfg.serial = p.getString("serial", DEFAULT_SERIAL);
  cfg.code = p.getString("code", "");
  p.end();
}

static void saveConfig(const Config &c) {
  Preferences p;
  p.begin("printer", false);
  p.putString("ip", c.ip);
  p.putString("serial", c.serial);
  p.putString("code", c.code);
  p.end();
}

// ---------------------------------------------------------------- MQTT task

static void onMessage(char *, byte *payload, unsigned int len) {
  JsonDocument filter;
  JsonObject f = filter["print"].to<JsonObject>();
  for (const char *k : {"gcode_state", "mc_percent", "mc_remaining_time", "layer_num", "total_layer_num",
                        "nozzle_temper", "bed_temper", "subtask_name"})
    f[k] = true;
  JsonDocument doc;
  if (deserializeJson(doc, payload, len, DeserializationOption::Filter(filter))) return;
  JsonObjectConst p = doc["print"];
  if (p.isNull()) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (p["gcode_state"].is<const char *>()) status.state = p["gcode_state"].as<const char *>();
  if (p["subtask_name"].is<const char *>()) status.file = p["subtask_name"].as<const char *>();
  if (p["mc_percent"].is<float>()) status.percent = p["mc_percent"];
  if (p["mc_remaining_time"].is<float>()) status.remaining = p["mc_remaining_time"];
  if (p["nozzle_temper"].is<float>()) status.nozzle = p["nozzle_temper"];
  if (p["bed_temper"].is<float>()) status.bed = p["bed_temper"];
  if (p["layer_num"].is<int>()) status.layer = p["layer_num"];
  if (p["total_layer_num"].is<int>()) status.totalLayers = p["total_layer_num"];
  changed = true;
  xSemaphoreGive(lock);
}

static void mqttTask(void *) {
  WiFiClientSecure tls;
  tls.setInsecure();  // the printer uses a self-signed certificate
  PubSubClient mqtt(tls);
  mqtt.setBufferSize(24 * 1024);  // a full status report is ~10-20 KB
  mqtt.setCallback(onMessage);
  mqtt.setKeepAlive(30);
  uint32_t lastTry = 0, lastPush = 0;
  static Config c;  // this task's copy; PubSubClient keeps a pointer to the host string
  for (;;) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (reconnect || !mqtt.connected()) c = cfg;
    xSemaphoreGive(lock);
    if (reconnect) {
      reconnect = false;
      xSemaphoreTake(lock, portMAX_DELAY);
      c = cfg;
      xSemaphoreGive(lock);
      mqtt.disconnect();
      lastTry = 0;
    }
    if (!c.ready()) {
      state = "unset";
    } else if (WiFi.status() != WL_CONNECTED) {
      state = "connecting";
    } else if (!mqtt.connected()) {
      if (!lastTry || millis() - lastTry > 20000) {
        lastTry = millis();
        state = "connecting";
        mqtt.setServer(c.ip.c_str(), 8883);
        String id = "smart-puck-" + String((uint32_t)ESP.getEfuseMac(), HEX);
        if (mqtt.connect(id.c_str(), "bblp", c.code.c_str())) {
          mqtt.subscribe(("device/" + c.serial + "/report").c_str());
          lastPush = 0;
          state = "connected";
          xSemaphoreTake(lock, portMAX_DELAY);
          lastError = "";
          xSemaphoreGive(lock);
        } else {
          state = "error";
          int rc = mqtt.state();
          String e = rc == 4 || rc == 5 ? "wrong access code" : rc == -2 ? "printer not reachable" : "MQTT error " + String(rc);
          xSemaphoreTake(lock, portMAX_DELAY);
          lastError = e;
          xSemaphoreGive(lock);
        }
      }
    } else {
      // Ask for a full report on connect and every 5 minutes; the printer sends changes in between.
      if (!lastPush || millis() - lastPush > 5 * 60 * 1000) {
        lastPush = millis();
        mqtt.publish(("device/" + c.serial + "/request").c_str(),
                     "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}");
      }
      mqtt.loop();
      state = "connected";
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ---------------------------------------------------------------- settings page

static String esc(const String &s) {
  String o;
  for (char c : s) o += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '"' ? "&quot;" : c == '&' ? "&amp;" : String(c);
  return o;
}

// Wi-Fi, clock and Spotify state as JSON, for checking on the puck from a computer.
static void statusJson(JsonDocument &doc) {
  net::describe(doc);
  spotify::describe(doc);
  doc["printer"] = (const char *)state;
  doc["ble_connected"] = ble::connectedCount();
  doc["uptime_s"] = millis() / 1000;
  doc["heap_free"] = ESP.getFreeHeap();
  doc["heap_min"] = ESP.getMinFreeHeap();
  String boot = evlog::last("boot"), drop = evlog::last("wifi drop");
  if (boot.length()) doc["last_boot"] = boot;
  if (drop.length()) doc["last_wifi_drop"] = drop;
}

static void handleLog() { server.send(200, "text/plain; charset=utf-8", evlog::read()); }

static void handleStatus() {
  JsonDocument doc;
  statusJson(doc);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleRoot() {
  xSemaphoreTake(lock, portMAX_DELAY);
  String err = lastError;
  Config cfg = printer::cfg;
  xSemaphoreGive(lock);
  String page =
      "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><title>Smart Puck</title>"
      "<style>body{font:16px system-ui;max-width:420px;margin:24px auto;padding:0 16px}"
      "label{display:block;margin:14px 0 4px}input{width:100%;padding:8px;font:inherit;box-sizing:border-box}"
      "button{margin-top:18px;padding:10px 16px;font:inherit}.s{color:#555}</style>"
      "<h1>Smart Puck</h1><h2>3D printer</h2><p class=s>Status: " +
      String((const char *)state) + (err.length() ? " (" + esc(err) + ")" : "") +
      "<br>Touch: " + String(touch::status()) + "<br>Loop: " + String(diagLoopAvg) + " ms average, " + String(diagLoopMax) + " ms worst" + "</p><form method=post action=/printer>"
      "<label>Printer IP address</label><input name=ip value=\"" + esc(cfg.ip) + "\">"
      "<label>Serial number</label><input name=serial value=\"" + esc(cfg.serial) + "\">"
      "<label>LAN access code</label><input name=code type=password placeholder=\"" +
      (cfg.code.length() ? "saved; leave blank to keep" : "on the printer: Settings, WLAN") + "\">"
      "<button>Save</button></form>";
  {
    JsonDocument st;
    statusJson(st);
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char clock[32];
    strftime(clock, sizeof clock, "%a %b %d %H:%M", &tm);
    page += "<h2>Status</h2><p class=s>Clock: " + String(clock) + " (" +
            (st["time_synced"].as<bool>() ? "set from " + String(st["time_source"].as<const char *>()) : String("not set from the internet yet")) +
            ")<br>Wi-Fi: " + esc(st["wifi"].as<String>()) + " " + esc(st["ssid"] | "") + ", DNS " + esc(st["dns"] | "") +
            "<br>Spotify: " + esc(st["spotify"].as<String>());
    if (st["spotify_error"].is<const char *>()) page += " (" + esc(st["spotify_error"].as<String>()) + ")";
    if (st["spotify_detail"].is<const char *>()) page += "<br>Last Spotify failure: " + esc(st["spotify_detail"].as<String>());
    page += "<br>Memory free: " + String(ESP.getFreeHeap() / 1024) + " KB (lowest " + String(ESP.getMinFreeHeap() / 1024) +
            " KB)</p>";
  }
  page += "<h2>Bluetooth computers</h2><p class=s>Name each computer you paired. A name with \"Mac\" in it makes the "
          "puck send Cmd instead of Ctrl for shortcuts.</p>";
  page += "<p class=s>Connected now: ";
  if (ble::connectedCount() == 0) page += "none";
  for (int i = 0; i < ble::connectedCount(); i++)
    page += String(i ? ", " : "") + esc(ble::connectedName(i)) + (ble::connectedIsMac(i) ? " (Mac)" : " (PC)") +
            (ble::connectedIsTarget(i) ? " - receiving keys" : "");
  page += "</p>";
  if (ble::bondCount() == 0) page += "<p class=s>None paired yet. On the puck: Settings, Bluetooth, Pair a new computer.</p>";
  else {
    page += "<form method=post action=/names>";
    for (int i = 0; i < ble::bondCount(); i++)
      page += "<label>Computer " + String(i + 1) + "</label><input name=n" + String(i) + " value=\"" + esc(ble::bondName(i)) + "\">";
    page += "<button>Save names</button></form>";
  }
  server.send(200, "text/html", page);
}

// The companion on the computer posts the front app here over Wi-Fi: {"t":"ctx","app":"vscode"}.
// Only messages about the computer's context are accepted (not firmware or lineup changes).
static void handleCtx() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return server.send(400, "text/plain", "bad JSON");
  const char *t = doc["t"] | "";
  if (strcmp(t, "ctx") && strcmp(t, "sidekick") && strcmp(t, "mic") && strcmp(t, "active")) return server.send(400, "text/plain", "not allowed");
  ui::host(doc.as<JsonObjectConst>());
  server.send(200, "text/plain", "ok");
}

static void handleNames() {
  for (int i = 0; i < ble::bondCount(); i++)
    if (server.hasArg("n" + String(i))) ble::setBondName(i, server.arg("n" + String(i)).c_str());
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleSave() {
  Config c = cfg;
  c.ip = server.arg("ip");
  c.ip.trim();
  c.serial = server.arg("serial");
  c.serial.trim();
  if (server.arg("code").length()) c.code = server.arg("code");
  saveConfig(c);
  xSemaphoreTake(lock, portMAX_DELAY);
  cfg = c;
  reconnect = true;
  xSemaphoreGive(lock);
  server.sendHeader("Location", "/");
  server.send(303);
}

// ---------------------------------------------------------------- main-loop side

void begin() {
  lock = xSemaphoreCreateMutex();
  loadConfig();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/printer", HTTP_POST, handleSave);
  server.on("/names", HTTP_POST, handleNames);
  server.on("/ctx", HTTP_POST, handleCtx);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/log", HTTP_GET, handleLog);
  xTaskCreatePinnedToCore(mqttTask, "printer", 12 * 1024, nullptr, 1, nullptr, 0);
}

bool poll(JsonDocument &msg) {
  if (!serverStarted && WiFi.status() == WL_CONNECTED && WiFi.getMode() == WIFI_STA) {
    server.begin();  // also at http://smart-puck.local/ (mDNS is started by ota.cpp)
    serverStarted = true;
  }
  if (serverStarted) server.handleClient();
  if (!changed) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  changed = false;
  msg["t"] = "printer";
  msg["state"] = status.state;
  msg["file"] = status.file;
  if (!isnan(status.percent)) msg["percent"] = status.percent;
  if (!isnan(status.remaining)) msg["remaining_min"] = status.remaining;
  if (!isnan(status.nozzle)) msg["nozzle"] = status.nozzle;
  if (!isnan(status.bed)) msg["bed"] = status.bed;
  msg["layer"] = status.layer;
  msg["total_layers"] = status.totalLayers;
  xSemaphoreGive(lock);
  return true;
}

void describe(JsonDocument &doc) {
  doc["printer"] = (const char *)state;
  xSemaphoreTake(lock, portMAX_DELAY);
  doc["printer_ip"] = cfg.ip;
  if (lastError.length()) doc["printer_error"] = lastError;
  xSemaphoreGive(lock);
}

}  // namespace printer
