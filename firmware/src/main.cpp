// Smart Puck firmware for the 1.28" ESP32-S3 round knob display.
//
// Runs the screens from puck-ui/puck-sim.html (see ui.cpp) on the round LCD. The knob turns
// and presses; holding it for 600 ms opens the mode menu. Talks to the computer over USB serial,
// one JSON object per line:
//   puck -> computer: {"evt":"hello",...}, {"evt":"action","mode":"idle","in":"turn_right","do":"usb.volume_up"},
//                     {"evt":"pin","mode":"media"}, {"evt":"pong"}
//   computer -> puck: {"t":"ctx","app":"zoom","mode":"calls","title":"..."}, {"t":"mic","muted":true},
//                     {"t":"time","epoch":<local time as Unix seconds>}, {"t":"hello"}, {"t":"ping"},
//                     {"t":"modes","version":N,"modes":[...]} (lineup from the simulator; saved to /modes.json,
//                     answered with {"evt":"modes_ok",...} or {"evt":"modes_err","error":"..."}),
//                     {"t":"modes_reset"} (back to the built-in modes),
//                     {"t":"wifi"} (status), {"t":"wifi_reset"} (forget Wi-Fi, open the setup network),
//                     {"t":"time","utc":<Unix seconds>} (or "epoch" as local time, from older tools)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <mbedtls/platform.h>

#include <memory>

#include "ble.h"
#include "evlog.h"
#include "leds.h"
#include "net.h"
#include "ota.h"
#include "printer.h"
#include "spotify.h"
#include "touch.h"
#include "ui.h"

#define FW_VERSION "0.22.0"

// Pins from the display maker's examples.
constexpr int PIN_LCD_PWR_EN1 = 1;   // LCD 3V3 rail
constexpr int PIN_LCD_PWR_EN2 = 2;   // backlight (LEDA) rail
constexpr int PIN_LCD_DC = 3;
constexpr int PIN_LCD_CS = 9;
constexpr int PIN_LCD_SCLK = 10;
constexpr int PIN_LCD_MOSI = 11;
constexpr int PIN_LCD_RST = 14;
constexpr int PIN_LCD_BL = 46;
constexpr int PIN_ENC_A = 45;
constexpr int PIN_ENC_B = 42;
constexpr int PIN_ENC_SW = 41;

// Quadrature transitions per mechanical detent.
constexpr int ENC_STEPS_PER_DETENT = 4;
constexpr uint32_t HOLD_MS = 600;
constexpr uint32_t BTN_BOUNCE_MS = 6;  // edges closer together than this are contact bounce
constexpr uint32_t MIN_PRESS_MS = 10;  // shorter "presses" are noise
constexpr uint32_t FRAME_MS = 33;  // redraw rate for clocks and animations

constexpr int SCREEN = 240;
constexpr int BUF_LINES = 40;

Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI,
                                            GFX_NOT_DEFINED, FSPI, true);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, PIN_LCD_RST, 0 /* rotation */, true /* IPS */);

lv_obj_t *screenObj;

// ---------------------------------------------------------------- knob

volatile int32_t encSteps = 0;
volatile uint8_t encPrev = 0;

void IRAM_ATTR onEncoder() {
  // Index = (previous AB << 2) | current AB; invalid (bouncy) transitions count 0.
  static const int8_t table[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  uint8_t cur = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  encSteps += table[(encPrev << 2) | cur];
  encPrev = cur;
}

int32_t stepsUsed = 0;
bool pressed = false, held = false;
uint32_t pressStart = 0;

// The press switch is read by an interrupt that timestamps each edge. Polling it once per loop
// missed quick taps, because a loop (drawing a frame, Wi-Fi, Bluetooth) can take longer than a tap.
struct ButtonEvent {
  uint32_t t;
  bool down;
};
constexpr int BTN_QUEUE = 16;
volatile ButtonEvent btnQueue[BTN_QUEUE];
volatile uint8_t btnHead = 0, btnTail = 0;
volatile bool btnDown = false;    // state after the last accepted edge
volatile uint32_t btnEdgeAt = 0;  // when it was accepted

void IRAM_ATTR pushButton(uint32_t t, bool down) {
  uint8_t next = (btnHead + 1) % BTN_QUEUE;
  if (next == btnTail) return;  // full: drop
  btnQueue[btnHead].t = t;
  btnQueue[btnHead].down = down;
  btnHead = next;
  btnDown = down;
  btnEdgeAt = t;
}

void IRAM_ATTR onButton() {
  uint32_t now = millis();
  bool down = digitalRead(PIN_ENC_SW) == LOW;
  if (down == btnDown) return;                  // bounce back to the state we already have
  if (now - btnEdgeAt < BTN_BOUNCE_MS) return;  // too soon after the last edge: contact bounce
  pushButton(now, down);
}

bool pollEncoder() {
  noInterrupts();
  int32_t steps = encSteps;
  interrupts();
  int32_t delta = (steps - stepsUsed) / ENC_STEPS_PER_DETENT;
  if (delta == 0) return false;
  stepsUsed += delta * ENC_STEPS_PER_DETENT;
  for (int i = 0; i < abs(delta); i++) ui::rotate(delta > 0 ? 1 : -1);
  return true;
}

// Press fires on release (so a hold doesn't also press); a hold fires at 600 ms.
bool pollButton() {
  uint32_t now = millis();
  bool changed = false;
  // If a bounce swallowed the final edge, the pin and our state disagree for a while: catch up.
  noInterrupts();
  bool level = digitalRead(PIN_ENC_SW) == LOW;
  if (level != btnDown && now - btnEdgeAt > 25) pushButton(now, level);
  interrupts();
  while (btnTail != btnHead) {
    ButtonEvent e;
    noInterrupts();
    e.t = btnQueue[btnTail].t;
    e.down = btnQueue[btnTail].down;
    btnTail = (btnTail + 1) % BTN_QUEUE;
    interrupts();
    if (e.down) {
      pressed = true, held = false, pressStart = e.t;
    } else if (pressed) {
      if (!held && e.t - pressStart >= MIN_PRESS_MS) {
        ui::press();
        changed = true;
      }
      pressed = false;
    }
  }
  if (pressed && !held && now - pressStart >= HOLD_MS) {
    held = true;
    ui::hold();
    changed = true;
  }
  return changed;
}

// ---------------------------------------------------------------- serial

void send(JsonDocument &doc) {
  serializeJson(doc, Serial);
  Serial.println();
}

int lineupVersion = 0;

void sendHello() {
  JsonDocument doc;
  doc["evt"] = "hello";
  doc["fw"] = FW_VERSION;
  doc["dev"] = "crowpanel128";
  doc["modes_version"] = lineupVersion;  // 0 = built-in modes
  net::describe(doc);
  printer::describe(doc);
  doc["touch"] = touch::status();
  send(doc);
}

// ---------------------------------------------------------------- mode lineup

// Keeps big JSON documents (a full lineup is ~5 KB) in PSRAM.
struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
} psramAllocator;

constexpr const char *MODES_FILE = "/modes.json";

// Applies a {"t":"modes",...} line. From the computer it is saved and answered; at boot it is
// loaded quietly, and the built-in modes stay if it doesn't parse.
// `doc` is the parsed line, or null if it didn't parse.
bool applyLineup(std::unique_ptr<JsonDocument> doc, const char *line, size_t len, bool fromHost) {
  JsonDocument reply;
  bool ok = false;
  if (!doc) {
    reply["evt"] = "modes_err";
    reply["error"] = "bad JSON";
  } else {
    ok = ui::applyModes(std::move(doc), reply);
  }
  if (ok) lineupVersion = reply["version"] | 0;
  if (ok && fromHost) {
    File f = LittleFS.open(MODES_FILE, "w");
    if (!f || f.write((const uint8_t *)line, len) != len) reply["warning"] = "not saved";
    f.close();
  }
  if (fromHost || !ok) send(reply);
  return ok;
}

void loadSavedLineup() {
  File f = LittleFS.open(MODES_FILE, "r");
  if (!f) return;
  String line = f.readString();
  f.close();
  std::unique_ptr<JsonDocument> doc(new JsonDocument(&psramAllocator));
  if (deserializeJson(*doc, line)) doc.reset();
  applyLineup(std::move(doc), line.c_str(), line.length(), false);
}

// ---------------------------------------------------------------- messages from the computer

constexpr size_t MAX_LINE = 16384;
String hostLine;
bool lineTooLong = false;

bool pollHost() {
  bool changed = false;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (hostLine.length() < MAX_LINE) hostLine += c;
      else lineTooLong = true;
      continue;
    }
    if (hostLine.length() == 0) continue;
    if (lineTooLong) {
      JsonDocument reply;
      reply["evt"] = "modes_err";
      reply["error"] = "line too long";
      send(reply);
      hostLine = "";
      lineTooLong = false;
      continue;
    }
    std::unique_ptr<JsonDocument> parsed(new JsonDocument(&psramAllocator));
    if (deserializeJson(*parsed, hostLine) == DeserializationError::Ok) {
      JsonDocument &doc = *parsed;
      const char *t = doc["t"] | "";
      if (!strcmp(t, "modes")) {
        changed |= applyLineup(std::move(parsed), hostLine.c_str(), hostLine.length(), true);
      } else if (!strcmp(t, "modes_reset")) {
        LittleFS.remove(MODES_FILE);
        ui::resetModes();
        lineupVersion = 0;
        JsonDocument reply;
        reply["evt"] = "modes_ok";
        reply["version"] = 0;
        send(reply);
        changed = true;
      } else if (!strcmp(t, "wifi_reset")) {
        net::reset();
        JsonDocument reply;
        reply["evt"] = "wifi";
        net::describe(reply);
        send(reply);
        changed = true;
      } else if (!strcmp(t, "wifi")) {
        JsonDocument reply;
        reply["evt"] = "wifi";
        net::describe(reply);
        printer::describe(reply);
        send(reply);
      } else if (!strcmp(t, "ping")) {
        JsonDocument pong;
        pong["evt"] = "pong";
        send(pong);
      } else if (!strcmp(t, "leds")) {
        leds::setEnabled(doc["on"] | true);  // {"t":"leds","on":false} turns the glow off
      } else if (!strcmp(t, "spotify")) {
        spotify::handleHost(doc.as<JsonObjectConst>());
      } else if (!strcmp(t, "hello")) {
        sendHello();
      } else {
        ui::host(doc.as<JsonObjectConst>());
        changed = true;
      }
    } else if (hostLine.indexOf("\"modes\"") >= 0) {
      applyLineup(nullptr, nullptr, 0, true);  // answers modes_err
    }
    hostLine = "";
  }
  return changed;
}

// ---------------------------------------------------------------- display

constexpr int BL_CHANNEL = 0;

// Settings mode brightness, 5-100 %.
void setBacklight(int pct) { ledcWrite(BL_CHANNEL, map(constrain(pct, 5, 100), 0, 100, 0, 255)); }

// Drawn straight to the panel: LVGL doesn't get a turn while an update is being written.
void showUpdate(int pct) {
  static bool first = true;
  if (first || pct < 0) gfx->fillScreen(BLACK);
  first = false;
  gfx->setTextColor(pct < 0 ? RED : WHITE, BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(pct < 0 ? 60 : 72, 96);
  gfx->print(pct < 0 ? "Update failed" : "Updating");
  if (pct < 0) return;
  gfx->setTextSize(4);
  gfx->setCursor(pct < 10 ? 96 : pct < 100 ? 84 : 72, 126);
  gfx->printf("%d%%", pct);
  gfx->fillArc(120, 120, 118, 110, -90, -90 + 3.6f * pct, 0x05FF);
}

void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px, lv_area_get_width(area), lv_area_get_height(area));
  leds::sample(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area), (const uint16_t *)px);
  if (lv_display_flush_is_last(disp)) leds::frameDone();
  lv_display_flush_ready(disp);
}

void onDraw(lv_event_t *e) { ui::draw(lv_event_get_layer(e)); }

uint32_t tick() { return millis(); }

void setupDisplay() {
  pinMode(PIN_LCD_PWR_EN1, OUTPUT);
  pinMode(PIN_LCD_PWR_EN2, OUTPUT);
  digitalWrite(PIN_LCD_PWR_EN1, HIGH);
  digitalWrite(PIN_LCD_PWR_EN2, HIGH);
  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(PIN_LCD_BL, BL_CHANNEL);
  setBacklight(80);

  gfx->begin(80000000);
  gfx->fillScreen(BLACK);

  lv_init();
  lv_tick_set_cb(tick);
  lv_display_t *disp = lv_display_create(SCREEN, SCREEN);
  size_t bufBytes = SCREEN * BUF_LINES * 2;
  void *buf1 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *buf2 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_set_buffers(disp, buf1, buf2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush);

  // One full-screen object; ui::draw paints everything on it each frame.
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  screenObj = lv_obj_create(scr);
  lv_obj_remove_style_all(screenObj);
  lv_obj_set_size(screenObj, SCREEN, SCREEN);
  lv_obj_set_pos(screenObj, 0, 0);
  lv_obj_add_event_cb(screenObj, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
}

// ---------------------------------------------------------------- main

// TLS (Spotify, the printer) keeps its buffers in PSRAM. The framework puts them in
// internal RAM, and three connections next to Wi-Fi, Bluetooth and the screen ran it out
// ("SSL - Memory allocation failed"), so Spotify never connected.
static void *tlsCalloc(size_t n, size_t size) {
  void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void setup() {
  mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);
  Serial.setRxBufferSize(MAX_LINE);  // a whole mode lineup can arrive while a frame is drawing
  Serial.begin(115200);
  LittleFS.begin(true);  // formats the data partition on first boot
  evlog::begin(FW_VERSION);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  encPrev = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(PIN_ENC_A, onEncoder, CHANGE);
  attachInterrupt(PIN_ENC_B, onEncoder, CHANGE);
  attachInterrupt(PIN_ENC_SW, onButton, CHANGE);

  ui::begin(send);
  ui::setBacklight(setBacklight);
  ui::setFirmwareVersion(FW_VERSION);
  loadSavedLineup();
  setupDisplay();
  ui::startBoot();     // its first frame is plain black, so Home doesn't flash first
  lv_timer_handler();  // draw the first frame before Wi-Fi, which can take a few seconds to join
  ui::loadPrefs();
  leds::begin();
  // The startup animation plays before Wi-Fi starts joining, which can hold up the loop for seconds.
  while (ui::bootRunning()) {
    pollEncoder();
    pollButton();
    leds::update();
    lv_obj_invalidate(screenObj);
    lv_timer_handler();
    delay(2);
  }
  net::begin(ui::timeZone());
  touch::begin();
  printer::begin();
  ui::setSpotifyControl(spotify::control);
  spotify::begin();
  ota::begin(showUpdate);
  sendHello();
}

uint32_t lastFrame = 0;

// How long one pass of loop() takes (average and worst over the last 10 s), shown on the web page.
uint32_t diagLoopAvg = 0, diagLoopMax = 0;
static uint32_t diagPrev = 0, diagSum = 0, diagCount = 0, diagMax = 0, diagWindow = 0;

void loop() {
  uint32_t t0 = millis();
  if (diagPrev) {
    uint32_t dt = t0 - diagPrev;
    diagSum += dt, diagCount++, diagMax = max(diagMax, dt);
    if (t0 - diagWindow > 10000) {
      diagLoopAvg = diagSum / max<uint32_t>(1, diagCount), diagLoopMax = diagMax;
      diagSum = diagCount = diagMax = 0, diagWindow = t0;
    }
  }
  diagPrev = t0;
  bool changed = pollEncoder();
  changed |= pollButton();
  changed |= pollHost();
  if (int g = touch::poll()) changed |= ui::gesture(g);
  net::loop();
  evlog::flush();
  leds::update();
  // Bluetooth starts only once Wi-Fi has joined (or after 45 s): both share one radio, and joining
  // Wi-Fi while Bluetooth is advertising was failing.
  static bool bleStarted = false;
  if (!bleStarted && (WiFi.status() == WL_CONNECTED || millis() > 45000)) {
    ble::begin();
    bleStarted = true;
  }
  if (bleStarted) ble::loop();
  ota::loop();
  spotify::setFast(ui::mediaOnScreen());
  changed |= ui::autoSelect();
  JsonDocument spotifyMsg, spotifyEvt;
  if (spotify::poll(spotifyMsg)) {
    ui::host(spotifyMsg.as<JsonObjectConst>());
    changed = true;
  }
  if (spotify::pollEvent(spotifyEvt)) {
    send(spotifyEvt);
    spotifyEvt["t"] = "spotify_status";
    ui::host(spotifyEvt.as<JsonObjectConst>());
    changed = true;
  }
  JsonDocument printerMsg;
  if (printer::poll(printerMsg)) {
    ui::host(printerMsg.as<JsonObjectConst>());
    changed = true;
  }
  uint32_t now = millis();
  if (changed || now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    lv_obj_invalidate(screenObj);
  }
  lv_timer_handler();
  delay(1);
}
