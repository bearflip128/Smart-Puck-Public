#include "spotify.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp32s3/rom/tjpgd.h>

#include <vector>

#include "net.h"
#include "spotify_roots.h"

namespace spotify {

static String clientId, refreshToken, accessToken;
static uint32_t tokenExpiresAt = 0;  // millis()
static SemaphoreHandle_t lock;

// What the screen needs, filled by the task.
struct Now {
  String state = "idle";  // playing, paused, idle
  String track, artist, device;
  int progressMs = 0, durationMs = 0, volume = -1;
  String kind;  // track, episode, ad
  String artUrl;      // smallest album cover (64 px)
  int32_t art = -1;   // its average color, 0xRRGGBB, or -1 while unknown
};
static Now now;
static volatile bool nowChanged = false;
static String eventState, eventError;
static volatile bool eventPending = false;
static String lastReportedState;

static volatile bool isLinked = false, fast = false;
static volatile bool linkRequested = false, unlinkRequested = false;
static volatile bool pollNow = false;
static String pendingCmd;  // play_pause, next, prev
static int volTarget = -1;
static uint32_t volAt = 0;

static void saveCreds() {
  Preferences p;
  p.begin("spotify", false);
  p.putString("client", clientId);
  p.putString("refresh", refreshToken);
  p.end();
}

static void loadCreds() {
  Preferences p;
  p.begin("spotify", true);
  clientId = p.getString("client", "");
  refreshToken = p.getString("refresh", "");
  p.end();
  isLinked = clientId.length() && refreshToken.length();
}

static void event(const char *state, const char *error = nullptr) {
  static String lastKey;  // repeated identical errors (e.g. every poll while offline) are reported once
  String key = String(state) + "/" + (error ? error : "");
  if (key == lastKey && !strcmp(state, "error")) return;
  lastKey = key;
  xSemaphoreTake(lock, portMAX_DELAY);
  eventState = state;
  eventError = error ? error : "";
  eventPending = true;
  xSemaphoreGive(lock);
}

// ---------------------------------------------------------------- HTTP

// TLS checks certificate dates, so wait for the clock to be set from the internet (the clock from
// the firmware build can be older than Spotify's current certificate); try anyway after a minute.
static bool timeOk() { return time(nullptr) > 1700000000 && (net::timeSynced() || millis() > 60000); }

static String lastDetail;  // why the last request failed, for the status page

static int request(const char *method, const String &url, const String &body, String *out, const char *contentType,
                   bool bearer, int *retryAfter = nullptr) {
  WiFiClientSecure tls;
  tls.setCACert(SPOTIFY_ROOTS);
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(8000);
  const char *keys[] = {"Retry-After"};
  http.collectHeaders(keys, 1);
  if (!http.begin(tls, url)) {
    lastDetail = "bad URL";
    return -1;
  }
  if (bearer) http.addHeader("Authorization", "Bearer " + accessToken);
  if (contentType) http.addHeader("Content-Type", contentType);
  // PUT/POST with no body must say so: Spotify answers 411 "Length Required" without Content-Length: 0,
  // and HTTPClient only adds the header when there is a body.
  if (body.length() == 0 && (!strcmp(method, "PUT") || !strcmp(method, "POST"))) http.addHeader("Content-Length", "0");
  int code = http.sendRequest(method, (uint8_t *)body.c_str(), body.length());  // empty body sends Content-Length: 0
  if (code > 0 && out) *out = http.getString();
  if (retryAfter && http.hasHeader("Retry-After")) *retryAfter = http.header("Retry-After").toInt();
  if (code < 0) {
    char buf[96] = "";
    int tlsErr = tls.lastError(buf, sizeof buf);
    String why = tlsErr ? "TLS: " + String(buf) : HTTPClient::errorToString(code);
    xSemaphoreTake(lock, portMAX_DELAY);
    lastDetail = why;
    xSemaphoreGive(lock);
  }
  http.end();
  return code;
}

static String urlEncode(const String &s) {
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

// Returns true when there is a usable access token. Error strings: "relink" (Spotify refused the refresh token).
static bool ensureToken(String &error) {
  if (accessToken.length() && (int32_t)(tokenExpiresAt - millis()) > 60000) return true;
  String resp;
  String body = "grant_type=refresh_token&refresh_token=" + urlEncode(refreshToken) + "&client_id=" + urlEncode(clientId);
  int code = request("POST", "https://accounts.spotify.com/api/token", body, &resp, "application/x-www-form-urlencoded", false);
  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
      error = "bad token response";
      return false;
    }
    accessToken = doc["access_token"] | "";
    tokenExpiresAt = millis() + (doc["expires_in"] | 3600) * 1000UL;
    const char *rotated = doc["refresh_token"] | "";
    if (*rotated && refreshToken != rotated) {  // PKCE refresh tokens rotate: the old one stops working
      refreshToken = rotated;
      saveCreds();
    }
    return accessToken.length() > 0;
  }
  accessToken = "";
  if (code == 400 || code == 401) {
    JsonDocument doc;
    deserializeJson(doc, resp);
    String e = doc["error"] | "";
    error = e == "invalid_grant" || e == "invalid_client" ? "relink" : "refused: " + e;
  } else {
    error = code < 0 ? "no connection" : "HTTP " + String(code);
  }
  return false;
}

static void publish(const Now &n) {
  xSemaphoreTake(lock, portMAX_DELAY);
  now = n;
  nowChanged = true;
  xSemaphoreGive(lock);
  if (lastReportedState != n.state) {
    lastReportedState = n.state;
    event(n.state.c_str());
  }
}

// GET /v1/me/player. Returns the wait before the next poll in ms, or -1 for "normal".
static int doPoll(String &error) {
  JsonDocument filter;
  filter["is_playing"] = true;
  filter["progress_ms"] = true;
  filter["currently_playing_type"] = true;
  filter["device"]["name"] = true;
  filter["device"]["volume_percent"] = true;
  filter["item"]["name"] = true;
  filter["item"]["duration_ms"] = true;
  filter["item"]["artists"][0]["name"] = true;
  filter["item"]["show"]["name"] = true;
  filter["item"]["album"]["images"][0]["url"] = true;
  String resp;
  int retry = 0;
  int code = request("GET", "https://api.spotify.com/v1/me/player", "", &resp, nullptr, true, &retry);
  if (code == 204) {
    Now n;
    publish(n);
    return -1;
  }
  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, resp, DeserializationOption::Filter(filter))) {
      error = "bad response";
      return -1;
    }
    Now n;
    n.state = doc["is_playing"] | false ? "playing" : "paused";
    n.kind = doc["currently_playing_type"] | "track";
    n.progressMs = doc["progress_ms"] | 0;
    n.durationMs = doc["item"]["duration_ms"] | 0;
    n.device = doc["device"]["name"] | "";
    n.volume = doc["device"]["volume_percent"] | -1;
    n.track = doc["item"]["name"] | (n.kind == "ad" ? "Advertisement" : "");
    n.artist = doc["item"]["artists"][0]["name"] | doc["item"]["show"]["name"] | "";
    JsonArrayConst imgs = doc["item"]["album"]["images"];
    if (imgs.size()) n.artUrl = imgs[imgs.size() - 1]["url"] | "";  // Spotify lists them largest first
    xSemaphoreTake(lock, portMAX_DELAY);
    if (n.artUrl == now.artUrl) n.art = now.art;  // same cover: keep its color
    xSemaphoreGive(lock);
    publish(n);
    return -1;
  }
  if (code == 401) {
    accessToken = "";  // refresh and try again on the next pass
    return 500;
  }
  if (code == 429) return (retry > 0 ? retry : 5) * 1000;
  error = code < 0 ? "no connection" : "HTTP " + String(code);
  return 5000;
}

// Control requests. On 403 Spotify wants Premium.
static void doControl(const String &cmd, int volume) {
  String path, method = "PUT";
  if (cmd == "play_pause") {
    path = now.state == "playing" ? "/v1/me/player/pause" : "/v1/me/player/play";
  } else if (cmd == "next") {
    path = "/v1/me/player/next", method = "POST";
  } else if (cmd == "prev") {
    path = "/v1/me/player/previous", method = "POST";
  } else if (cmd == "volume") {
    path = "/v1/me/player/volume?volume_percent=" + String(volume);
  } else {
    return;
  }
  String resp;
  int code = request(method.c_str(), "https://api.spotify.com" + path, "", &resp, nullptr, true);
  if (code == 403) {
    // 403 isn't always Premium: phones and some speakers refuse remote volume (VOLUME_CONTROL_DISALLOW).
    JsonDocument doc;
    deserializeJson(doc, resp);
    String reason = doc["error"]["reason"] | "";
    if (reason == "PREMIUM_REQUIRED") event("error", "premium");
    else if (reason == "VOLUME_CONTROL_DISALLOW") event("error", "volume");
    else event("error", ("denied " + reason).c_str());
  }
  else if (code == 404) event("error", "no active device");
  else if (code == 401) accessToken = "";
  else if (code < 200 || code >= 300) event("error", code < 0 ? "no connection" : ("HTTP " + String(code)).c_str());
  pollNow = true;
}

// ---------------------------------------------------------------- album color

// The Spotify watch face colors the record's label with the album cover's average color. The 64 px
// cover is a small JPEG; the ROM's TJpgDec decodes it in blocks and we add up the pixels.
struct ArtJob {
  const uint8_t *data;
  size_t len, pos;
  uint32_t r, g, b, n;
};

static UINT artIn(JDEC *jd, BYTE *buf, UINT len) {
  ArtJob *j = (ArtJob *)jd->device;
  if (j->pos + len > j->len) len = j->len - j->pos;
  if (buf) memcpy(buf, j->data + j->pos, len);
  j->pos += len;
  return len;
}

static UINT artOut(JDEC *jd, void *bitmap, JRECT *rect) {
  ArtJob *j = (ArtJob *)jd->device;
  const BYTE *px = (const BYTE *)bitmap;  // RGB888
  int count = (rect->right - rect->left + 1) * (rect->bottom - rect->top + 1);
  for (int i = 0; i < count; i++) j->r += px[3 * i], j->g += px[3 * i + 1], j->b += px[3 * i + 2];
  j->n += count;
  return 1;
}

// Returns 0xRRGGBB, or -1 if the cover couldn't be fetched or decoded.
static int32_t albumColor(const String &url) {
  WiFiClientSecure tls;
  tls.setCACert(SPOTIFY_ROOTS);
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(tls, url)) return -1;
  int code = http.GET();
  std::vector<uint8_t> jpg;
  if (code == 200) {
    int size = http.getSize();  // -1 when chunked
    WiFiClient *st = http.getStreamPtr();
    uint32_t until = millis() + 8000;
    while (http.connected() && (size < 0 || (int)jpg.size() < size) && jpg.size() < 64 * 1024 && millis() < until) {
      int avail = st->available();
      if (!avail) {
        delay(5);
        continue;
      }
      uint8_t buf[512];
      int got = st->readBytes(buf, min(avail, (int)sizeof buf));
      jpg.insert(jpg.end(), buf, buf + got);
    }
  }
  http.end();
  if (jpg.size() < 100) return -1;
  ArtJob job = {jpg.data(), jpg.size(), 0, 0, 0, 0, 0};
  const size_t POOL = 4096;
  void *pool = heap_caps_malloc(POOL, MALLOC_CAP_8BIT);
  if (!pool) return -1;
  JDEC jd;
  int32_t color = -1;
  if (jd_prepare(&jd, artIn, pool, POOL, &job) == JDR_OK && jd_decomp(&jd, artOut, 0) == JDR_OK && job.n) {
    float r = job.r / (float)job.n, g = job.g / (float)job.n, b = job.b / (float)job.n;
    // Very dark covers would vanish on the black record: lift them so the brightest channel is at least 120.
    float mx = fmaxf(r, fmaxf(g, b));
    if (mx < 120) {
      if (mx < 8) r = g = b = mx = 8;
      float k = 120 / mx;
      r *= k, g *= k, b *= k;
    }
    color = (int32_t)((uint32_t)lroundf(fminf(r, 255)) << 16 | (uint32_t)lroundf(fminf(g, 255)) << 8 | (uint32_t)lroundf(fminf(b, 255)));
  }
  free(pool);
  return color;
}

// ---------------------------------------------------------------- task

static void task(void *) {
  uint32_t lastPoll = 0, waitUntil = 0;
  String error;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));
    if (unlinkRequested) {
      unlinkRequested = false;
      Preferences p;
      p.begin("spotify", false);
      p.clear();
      p.end();
      clientId = refreshToken = accessToken = "";
      isLinked = false;
      Now empty;
      publish(empty);
      lastReportedState = "";
      event("unlinked");
      continue;
    }
    if (!isLinked || WiFi.status() != WL_CONNECTED || !timeOk()) continue;
    if ((int32_t)(millis() - waitUntil) < 0) continue;

    error = "";
    if (linkRequested) {
      linkRequested = false;
      accessToken = "";
      if (ensureToken(error)) event("linked");
      else {
        event("error", error.c_str());
        if (error == "relink") isLinked = false;  // stop polling with a dead token
        continue;
      }
      pollNow = true;
    }

    // Commands first, so a press feels immediate.
    String cmd;
    int vol = -1;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (pendingCmd.length()) cmd = pendingCmd, pendingCmd = "";
    if (volTarget >= 0 && millis() - volAt > 300) vol = volTarget, volTarget = -1;  // one request per quick spin
    xSemaphoreGive(lock);
    if (cmd.length() || vol >= 0) {
      if (!ensureToken(error)) {
        event("error", error.c_str());
        if (error == "relink") isLinked = false;
        waitUntil = millis() + 5000;
        continue;
      }
      if (cmd.length()) doControl(cmd, 0);
      if (vol >= 0) doControl("volume", vol);
      lastPoll = millis() - (fast ? 3000 : 6000) + 600;  // look again shortly
      continue;
    }

    uint32_t interval = fast ? 3000 : 6000;  // slow enough to be gentle, quick enough to notice music starting
    if (!pollNow && millis() - lastPoll < interval) continue;
    pollNow = false;
    lastPoll = millis();
    if (!ensureToken(error)) {
      event("error", error.c_str());
      if (error == "relink") isLinked = false;
      waitUntil = millis() + 10000;
      continue;
    }
    int wait = doPoll(error);
    if (wait > 0) waitUntil = millis() + wait;
    if (error.length()) event("error", error.c_str());
    // A new song's cover: work out its color once (the watch face shows it on the record).
    static String artTried;
    xSemaphoreTake(lock, portMAX_DELAY);
    String url = now.art < 0 ? now.artUrl : String();
    xSemaphoreGive(lock);
    if (url.length() && url != artTried) {
      artTried = url;
      int32_t c = albumColor(url);
      xSemaphoreTake(lock, portMAX_DELAY);
      if (now.artUrl == url) now.art = c >= 0 ? c : 0x1ed760, nowChanged = true;  // Spotify green if it failed
      xSemaphoreGive(lock);
    }
  }
}

// ---------------------------------------------------------------- main-loop side

void begin() {
  lock = xSemaphoreCreateMutex();
  loadCreds();
  linkRequested = isLinked;  // refresh once at boot so errors show up early
  xTaskCreatePinnedToCore(task, "spotify", 16 * 1024, nullptr, 1, nullptr, 0);
}

void handleHost(JsonObjectConst msg) {
  if (msg["unlink"] | false) {
    unlinkRequested = true;
    return;
  }
  const char *id = msg["client_id"] | "", *rt = msg["refresh_token"] | "";
  if (!*id || !*rt) {
    event("error", "missing client_id or refresh_token");
    return;
  }
  clientId = id;
  refreshToken = rt;
  saveCreds();
  isLinked = true;
  linkRequested = true;
}

void setFast(bool f) { fast = f; }
bool linked() { return isLinked; }

void describe(JsonDocument &doc) {
  xSemaphoreTake(lock, portMAX_DELAY);
  doc["spotify"] = isLinked ? (lastReportedState.length() ? lastReportedState : String("linked")) : String("not linked");
  if (eventError.length()) doc["spotify_error"] = eventError;
  if (lastDetail.length()) doc["spotify_detail"] = lastDetail;
  xSemaphoreGive(lock);
}

void control(const char *action) {
  if (!isLinked) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (!strcmp(action, "vol_up") || !strcmp(action, "vol_down")) {
    int cur = volTarget >= 0 ? volTarget : (now.volume >= 0 ? now.volume : 50);
    volTarget = constrain(cur + (!strcmp(action, "vol_up") ? 5 : -5), 0, 100);
    volAt = millis();
    now.volume = volTarget;  // the screen follows the knob right away
  } else {
    pendingCmd = action;
  }
  xSemaphoreGive(lock);
}

bool poll(JsonDocument &msg) {
  if (!nowChanged) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  nowChanged = false;
  msg["t"] = "nowplaying";
  msg["state"] = now.state;
  msg["track"] = now.track;
  msg["artist"] = now.artist;
  msg["device"] = now.device;
  msg["progress_ms"] = now.progressMs;
  msg["duration_ms"] = now.durationMs;
  msg["volume"] = now.volume;
  msg["kind"] = now.kind;
  if (now.art >= 0) msg["art"] = now.art;
  xSemaphoreGive(lock);
  return true;
}

bool pollEvent(JsonDocument &evt) {
  if (!eventPending) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  eventPending = false;
  evt["evt"] = "spotify";
  evt["state"] = eventState;
  if (eventError.length()) evt["error"] = eventError;
  xSemaphoreGive(lock);
  return true;
}

}  // namespace spotify
