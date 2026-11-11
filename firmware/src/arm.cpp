#include "arm.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <map>

#include "arm_roots.h"

namespace arm {

// Same limits as tools/arm_bridge.py.
constexpr float MAX_JUMP = 30;              // degrees (or gripper %) one goal may lead the arm by
constexpr uint32_t GOAL_EVERY_MS = 100;     // at most 10 goal requests a second
constexpr uint32_t POLL_EVERY_MS = 500;     // telemetry
constexpr uint32_t IDLE_DISARM_MS = 120000; // disarm after 2 minutes without a knob turn

static SemaphoreHandle_t lock;
static String url, lan, user, pass;  // saved login
static volatile bool isLinked = false, relink = false, clearRequested = false;

// Knob side, guarded by `lock`.
static std::map<String, float> pending;  // joint -> latest target
static int armRequest = -1;              // -1 none, 0 disarm, 1 arm
static uint32_t lastTurn = 0;            // millis() of the last knob move or arm request; 0 = none

// Arm side, filled by the task, guarded by `lock`.
static std::map<String, float> pose;
static String status = "off";  // off, no wifi, connecting, login failed, offline, armed, disarmed, e-stop, no servos
static volatile bool poseChanged = false;
static String eventText;
static volatile bool eventPending = false;

static void event(const String &text) {
  xSemaphoreTake(lock, portMAX_DELAY);
  eventText = text;
  eventPending = true;
  xSemaphoreGive(lock);
}

static void setStatus(const char *s) {
  xSemaphoreTake(lock, portMAX_DELAY);
  bool changed = status != s;
  status = s;
  if (changed) poseChanged = true;
  xSemaphoreGive(lock);
  if (changed) event(String("status ") + s);
}

static void loadLogin() {
  Preferences p;
  p.begin("armnet", true);
  url = p.getString("url", ""), lan = p.getString("lan", ""), user = p.getString("user", ""), pass = p.getString("pass", "");
  p.end();
  isLinked = url.length() && user.length() && pass.length();
}

// ---------------------------------------------------------------- HTTP

static WiFiClientSecure tls;
static WiFiClient plain;
static HTTPClient http;
static String base;    // the server in use: lan or url
static std::map<String, String> cookies;  // armctl's session cookie (and Cloudflare's, if it sets one)

// Takes the cookies out of Set-Cookie. HTTPClient joins repeated headers with commas, so split on
// both ';' and ',' and keep only name=value pairs that aren't attributes (Path, Expires, ...).
static void storeCookies(const String &header) {
  int start = 0;
  while (start < (int)header.length()) {
    int end = start;
    while (end < (int)header.length() && header[end] != ';' && header[end] != ',') end++;
    String part = header.substring(start, end);
    part.trim();
    int eq = part.indexOf('=');
    if (eq > 0) {
      String name = part.substring(0, eq), lower = name;
      lower.toLowerCase();
      if (lower != "path" && lower != "expires" && lower != "domain" && lower != "max-age" && lower != "samesite")
        cookies[name] = part.substring(eq + 1);
    }
    start = end + 1;
  }
}

static String cookieHeader() {
  String h;
  for (auto &kv : cookies) h += (h.length() ? "; " : "") + kv.first + "=" + kv.second;
  return h;
}

static bool timeOk() { return time(nullptr) > 1700000000; }  // TLS needs a real clock

// One request to armctl. Keeps the connection open between calls (a TLS handshake costs ~1 s).
static int request(const char *method, const char *path, const String &body, String *out,
                   const char *contentType = "application/json") {
  WiFiClient &client = base.startsWith("https://") ? (WiFiClient &)tls : plain;
  http.setReuse(true);
  http.setTimeout(4000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char *keys[] = {"Set-Cookie"};
  http.collectHeaders(keys, 1);
  if (!http.begin(client, base + path)) return -1;
  if (!cookies.empty()) http.addHeader("Cookie", cookieHeader());
  if (body.length()) http.addHeader("Content-Type", contentType);
  else if (!strcmp(method, "POST")) http.addHeader("Content-Length", "0");
  int code = http.sendRequest(method, (uint8_t *)body.c_str(), body.length());
  if (code > 0) {
    if (http.hasHeader("Set-Cookie")) storeCookies(http.header("Set-Cookie"));
    String resp = http.getString();  // always read the body, or a kept-alive connection is left mid-reply
    if (out) *out = resp;
  }
  http.end();  // with setReuse the connection stays open for the next call (unless it broke)
  return code;
}

static String formEncode(const String &s) {
  String o;
  for (char c : s) {
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') o += c;
    else {
      char b[4];
      snprintf(b, sizeof b, "%%%02X", (unsigned char)c);
      o += b;
    }
  }
  return o;
}

// Picks the home-network address when it answers, else the public one.
static void pickBase() {
  tls.stop(), plain.stop();  // start from fresh connections
  if (lan.length()) {
    base = lan;
    if (request("GET", "/healthz", "", nullptr) == 200) return;
  }
  base = url;
}

// Returns 1 signed in, 0 wrong login, -1 server unreachable.
static int login() {
  cookies.clear();
  int code = request("POST", "/login", "username=" + formEncode(user) + "&password=" + formEncode(pass), nullptr,
                     "application/x-www-form-urlencoded");
  if (code == 302 || code == 303) return cookies.empty() ? 0 : 1;  // armctl redirects to the dashboard on success
  return code < 0 || code >= 500 ? -1 : 0;
}

// Calls armctl, signing in again once if the session expired.
static int call(const char *method, const char *path, const String &body, String *out) {
  int code = request(method, path, body, out);
  if (code == 401 && login() == 1) code = request(method, path, body, out);
  return code;
}

static bool readTelemetry() {
  String resp;
  int code = call("GET", "/api/telemetry", "", &resp);
  if (code != 200) {
    setStatus(code == 401 ? "login failed" : "offline");
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, resp)) return false;
  bool armed = doc["armed"] | false;
  setStatus(doc["estopped"] | false ? "e-stop" : !(doc["connected"] | true) ? "no servos" : armed ? "armed" : "disarmed");
  std::map<String, float> p;
  for (JsonObjectConst j : doc["joints"].as<JsonArrayConst>()) {
    // While armed the goal is where the arm is headed; when not, only the servo reading means anything.
    JsonVariantConst v = armed ? j["goal"] : j["observed"];
    if (v.isNull()) v = j["setpoint"];
    if (!v.isNull()) p[j["name"] | ""] = v.as<float>();
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  for (auto &kv : p) {
    if (!pose.count(kv.first) || lroundf(pose[kv.first]) != lroundf(kv.second)) poseChanged = true;
    pose[kv.first] = kv.second;
  }
  xSemaphoreGive(lock);
  return true;
}

static void sendGoals(std::map<String, float> &goals) {
  JsonDocument doc;
  xSemaphoreTake(lock, portMAX_DELAY);
  for (auto &kv : goals) {
    float here = pose.count(kv.first) ? pose[kv.first] : kv.second;
    doc["joints"][kv.first] = constrain(kv.second, here - MAX_JUMP, here + MAX_JUMP);
  }
  xSemaphoreGive(lock);
  doc["source"] = "puck";
  String body, resp;
  serializeJson(doc, body);
  int code = call("POST", "/api/goal", body, &resp);
  if (code == 200) {
    JsonDocument r;
    if (!deserializeJson(r, resp)) {
      xSemaphoreTake(lock, portMAX_DELAY);
      for (JsonPairConst kv : r["accepted"].as<JsonObjectConst>()) pose[kv.key().c_str()] = kv.value().as<float>();
      xSemaphoreGive(lock);
    }
  } else {
    // Refused (disarmed, e-stop) or lost: put the screen back where the arm actually is.
    readTelemetry();
    xSemaphoreTake(lock, portMAX_DELAY);
    poseChanged = true;
    xSemaphoreGive(lock);
  }
}

static void task(void *) {
  bool signedIn = false, armedByPuck = false, wasOffline = false;
  uint32_t lastPoll = 0, lastGoal = 0, waitUntil = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(20));
    if (clearRequested) {
      clearRequested = false;
      Preferences p;
      p.begin("armnet", false);
      p.clear();
      p.end();
      url = lan = user = pass = "";
      cookies.clear();
      isLinked = signedIn = false;
      http.end();
      setStatus("off");
      event("unlinked");
      continue;
    }
    if (!isLinked) continue;
    if (relink) {
      relink = false;
      signedIn = false;
      http.end();
    }
    if (WiFi.status() != WL_CONNECTED) {
      setStatus("no wifi");
      wasOffline = true;
      continue;
    }
    if (!timeOk() || (int32_t)(millis() - waitUntil) < 0) continue;

    if (!signedIn) {
      setStatus("connecting");
      tls.setCACert(ARM_ROOTS);
      pickBase();
      int r = login();
      if (r != 1) {
        setStatus(r == 0 ? "login failed" : "offline");
        event(r == 0 ? "error login refused" : "error can't reach " + base);
        http.end();
        waitUntil = millis() + (r == 0 ? 60000 : 5000);
        wasOffline = true;
        continue;
      }
      signedIn = true;
      event("linked " + base);
      lastPoll = 0;
    }

    // Arm/disarm first, so the Safety layer feels immediate.
    int want;
    std::map<String, float> goals;
    uint32_t turned;
    xSemaphoreTake(lock, portMAX_DELAY);
    want = armRequest, armRequest = -1;
    if (millis() - lastGoal >= GOAL_EVERY_MS) goals.swap(pending);
    turned = lastTurn;
    String st = status;
    xSemaphoreGive(lock);

    if (want >= 0) {
      int code = call("POST", want ? "/api/arm" : "/api/disarm", "", nullptr);
      if (code == 200) armedByPuck = want == 1;
      else event(String("error ") + (want ? "arm" : "disarm") + " HTTP " + code);
      lastPoll = 0;
    }
    // A move sent while disarmed is refused by armctl; don't send until the pose is known.
    if (!goals.empty()) {
      lastGoal = millis();
      bool havePose;
      xSemaphoreTake(lock, portMAX_DELAY);
      havePose = !pose.empty();
      xSemaphoreGive(lock);
      if (havePose) sendGoals(goals);
    }
    if (st == "armed" && armedByPuck && turned && millis() - turned > IDLE_DISARM_MS) {
      if (call("POST", "/api/disarm", "", nullptr) == 200) event("idle disarm");
      armedByPuck = false;
      lastPoll = 0;
    }
    if (!lastPoll || millis() - lastPoll >= POLL_EVERY_MS) {
      lastPoll = millis();
      bool ok = readTelemetry();
      if (!ok) {
        // Lost the server (or the session): sign in again, re-picking home vs. public address.
        signedIn = false;
        http.end();
        wasOffline = true;
        waitUntil = millis() + 2000;
      } else if (wasOffline) {
        wasOffline = false;
        // Back after a dropout: if the puck had armed the arm, don't leave it armed unattended.
        if (armedByPuck && call("POST", "/api/disarm", "", nullptr) == 200) event("disarmed after reconnect");
        armedByPuck = false;
      }
    }
  }
}

// ---------------------------------------------------------------- main-loop side

void begin() {
  lock = xSemaphoreCreateMutex();
  loadLogin();
  if (isLinked) status = "connecting";
  xTaskCreatePinnedToCore(task, "arm", 12 * 1024, nullptr, 1, nullptr, 0);
}

void handleHost(JsonObjectConst msg) {
  if (msg["clear"] | false) {
    clearRequested = true;
    return;
  }
  const char *u = msg["url"] | "", *us = msg["user"] | "", *pw = msg["pass"] | "";
  if (!*u || !*us || !*pw) {
    event("error missing url, user or pass");
    return;
  }
  Preferences p;
  p.begin("armnet", false);
  p.putString("url", u);
  p.putString("lan", msg["lan"] | "");
  p.putString("user", us);
  p.putString("pass", pw);
  p.end();
  loadLogin();
  relink = true;
}

bool linked() { return isLinked; }

void moveJoint(const char *joint, float value) {
  if (!isLinked) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  pending[joint] = value;
  lastTurn = millis();
  xSemaphoreGive(lock);
}

void setArmed(bool armed) {
  if (!isLinked) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (!(armed && status == "armed")) armRequest = armed;  // more clicks right while armed do nothing
  lastTurn = millis();
  xSemaphoreGive(lock);
}

bool poll(JsonDocument &msg) {
  if (!poseChanged || !isLinked) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  poseChanged = false;
  msg["t"] = "arm";
  msg["status"] = status;
  for (auto &kv : pose) msg["joints"][kv.first] = kv.second;
  xSemaphoreGive(lock);
  return true;
}

bool pollEvent(JsonDocument &evt) {
  if (!eventPending) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  eventPending = false;
  evt["evt"] = "armnet";
  evt["msg"] = eventText;
  xSemaphoreGive(lock);
  return true;
}

void describe(JsonDocument &doc) {
  xSemaphoreTake(lock, portMAX_DELAY);
  doc["armnet"] = isLinked ? status : String("off");
  xSemaphoreGive(lock);
}

}  // namespace arm
