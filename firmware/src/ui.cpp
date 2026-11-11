// Modes, screen templates and the mode menu from puck-ui/puck-sim.html.
// Layout numbers are the prototype's, in 412 units; draw.cpp scales them to 240 px.
#include "ui.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <Preferences.h>

#include <memory>
#include <string>
#include <vector>

#include "draw.h"
#include "faces.h"
#include "ble.h"
#include "leds.h"
#include "net.h"
#include "spotify.h"
#include "textfold.h"

using namespace draw;

namespace ui {

// ---------------------------------------------------------------- mode data

struct Bind {
  const char *act;
  const char *arg;
};
struct Layer {
  const char *name;
  Bind left, right;
};
enum Tpl { IDLE, CALLS, TYPING, MEDIA, EDITING, VALUE, LIST, TIMER, ARM, CLOCKFACE, PRINTER, SETTINGS, SIDEKICK };

struct Mode {
  const char *id, *name;
  Tpl tpl;
  uint32_t color;
  std::vector<const char *> triggers;
  std::vector<Layer> layers;
  Bind press;
  // Template settings (only the ones a template reads are set).
  int min = 0, max = 100, step = 5, value = 50, minutes = 25, face = -1;
  const char *unit = "%", *host = "arm.local";
  const char *printer = "Printer", *file = "print.3mf";  // printer
  float hours = 2;
  int totalLayers = 300;
  int brightness = 80;  // settings
  std::vector<const char *> items;
};

static Bind B(const char *act, const char *arg = nullptr) { return {act, arg}; }

static std::vector<Mode> modes;

static void buildModes() {
  Mode m;
  m = {"idle", "Home", IDLE, 0x8a94a3, {"desktop"}, {{"Volume", B("usb.volume_down"), B("usb.volume_up")}}, B("usb.mute")};
  modes.push_back(m);
  m = {"calls", "Calls", CALLS, 0xff3b3f, {"zoom", "meet", "teams"},
       {{"Call volume", B("companion.call_vol_down"), B("companion.call_vol_up")}}, B("companion.mic_toggle")};
  modes.push_back(m);
  m = {"typing", "Typing", TYPING, 0x4fb3d9, {"vscode", "word"},
       {{"Scroll", B("usb.scroll_up"), B("usb.scroll_down")},
        {"Word jump", B("usb.key", "ctrl+left"), B("usb.key", "ctrl+right")},
        {"Undo / redo", B("usb.key", "ctrl+z"), B("usb.key", "ctrl+y")}},
       B("puck.cycle")};
  modes.push_back(m);
  m = {"media", "Media", MEDIA, 0x39d98a, {"spotify"}, {{"Volume", B("spotify.vol_down"), B("spotify.vol_up")}},
       B("spotify.play_pause")};
  modes.push_back(m);
  m = {"editing", "Video editing", EDITING, 0xffb547, {"premiere"},
       {{"Frame", B("usb.key", "left"), B("usb.key", "right")},
        {"Second", B("usb.key", "shift+left"), B("usb.key", "shift+right")},
        {"Edit point", B("usb.key", "up"), B("usb.key", "down")}},
       B("puck.cycle")};
  modes.push_back(m);
  m = {"design", "Design zoom", VALUE, 0xa78bfa, {"figma"}, {{"Zoom", B("usb.key", "ctrl+-"), B("usb.key", "ctrl+=")}},
       B("usb.key", "shift+2")};
  m.min = 10, m.max = 800, m.step = 10, m.value = 100;
  modes.push_back(m);
  m = {"present", "Presenting", LIST, 0xf472b6, {"powerpoint"}, {{"Slides", B("usb.key", "left"), B("usb.key", "right")}},
       B("usb.key", "b")};
  m.items = {"Title", "Why a puck", "Hardware", "Modes", "Demo", "Questions"};
  modes.push_back(m);
  m = {"focus", "Focus timer", TIMER, 0xfb923c, {}, {{"Set time", B("puck.none"), B("puck.none")}},
       B("slack.status", "Focusing")};
  m.minutes = 25;
  modes.push_back(m);
  m = {"windows", "Window switcher", LIST, 0x60a5fa, {}, {{"Windows", B("companion.window_prev"), B("companion.window_next")}},
       B("companion.window_focus")};
  m.items = {"Chrome - Gmail", "VS Code - encoder.c", "Zoom", "Spotify", "Figma - Puck UI"};
  modes.push_back(m);
  m = {"unread", "Slack unread", LIST, 0xe879f9, {"slack"}, {{"Channels", B("puck.none"), B("puck.none")}},
       B("slack.open_unread")};
  m.items = {"#hardware - 4", "#general - 12", "Direct messages - 1"};
  modes.push_back(m);
  m = {"lights", "Desk lights", VALUE, 0xfacc15, {},
       {{"Brightness", B("homeassistant.bright_down", "light.desk"), B("homeassistant.bright_up", "light.desk")}},
       B("homeassistant.toggle", "light.desk")};
  m.min = 0, m.max = 100, m.step = 5, m.value = 60;
  modes.push_back(m);
  m = {"arm", "Robot arm", ARM, 0xfb7185, {},
       {{"Safety", B("robotarm.disarm"), B("robotarm.arm")},
        {"Base", B("robotarm.jog_minus", "base"), B("robotarm.jog_plus", "base")},
        {"Shoulder", B("robotarm.jog_minus", "shoulder"), B("robotarm.jog_plus", "shoulder")},
        {"Elbow", B("robotarm.jog_minus", "elbow"), B("robotarm.jog_plus", "elbow")},
        {"Wrist", B("robotarm.jog_minus", "wrist"), B("robotarm.jog_plus", "wrist")},
        {"Roll", B("robotarm.jog_minus", "roll"), B("robotarm.jog_plus", "roll")},
        {"Gripper", B("robotarm.grip_close"), B("robotarm.grip_open")}},
       B("puck.cycle")};
  m.step = 5;
  modes.push_back(m);
  m = {"faces", "Watch faces", CLOCKFACE, 0xd6b36a, {}, {{"Faces", B("puck.none"), B("puck.none")}}, B("puck.none")};
  m.face = 0;
  modes.push_back(m);
  m = {"printer", "3D printer", PRINTER, 0x34d399, {"bambustudio"}, {{"Pages", B("puck.none"), B("puck.none")}},
       B("puck.none")};
  m.printer = "Bambu A1 mini", m.file = "puck_dial_ring.3mf", m.hours = 2.9f, m.totalLayers = 412;
  modes.push_back(m);
  m = {"sidekick", "Sidekick", SIDEKICK, 0x7dd3fc, {"*"}, {{"Tools", B("puck.none"), B("puck.none")}}, B("puck.none")};
  modes.push_back(m);
  m = {"settings", "Settings", SETTINGS, 0x94a3b8, {}, {{"Items", B("puck.none"), B("puck.none")}}, B("puck.none")};
  modes.push_back(m);
  m = {"stream", "Streaming", LIST, 0x2dd4bf, {"obs"}, {{"Scenes", B("obs.scene_prev"), B("obs.scene_next")}},
       B("obs.record")};
  m.items = {"Camera", "Screen share", "Be right back"};
  modes.push_back(m);
}

// Labels for press actions, used for the "Press: ..." hint.
static const char *actionLabel(const char *act) {
  static const char *table[][2] = {{"usb.mute", "Mute speakers"},          {"usb.key", "Press keys"},
                                   {"companion.mic_toggle", "Toggle mic mute"},
                                   {"companion.window_focus", "Focus selected window"},
                                   {"spotify.play_pause", "Play / pause"},  {"slack.open_unread", "Open selected channel"},
                                   {"slack.status", "Set status"},          {"homeassistant.toggle", "Toggle"},
                                   {"obs.record", "Start / stop recording"}};
  for (auto &t : table)
    if (!strcmp(t[0], act)) return t[1];
  return "Nothing";
}

// ---------------------------------------------------------------- state

static uint32_t nowMs() { return millis(); }
static bool recent(uint32_t at, uint32_t ms) { return at && nowMs() - at < ms; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

// Matches armctl (the arm server, config/arm.yaml): key, limits and home pose.
// Angles in degrees; the gripper is 0-100 % open. Limits and start (home) match armctl's config/arm.yaml
// (shoulder_lift's -13.65 rounded to -14); tools/arm_bridge.py replaces them with the real pose.
struct ArmJoint {
  const char *name, *key;
  int min, max, start;
};
static const ArmJoint JOINTS[] = {{"Base", "shoulder_pan", -150, 150, 0},    {"Shoulder", "shoulder_lift", -120, -14, -45},
                                  {"Elbow", "elbow_flex", -10, 135, 90},     {"Wrist", "wrist_flex", -90, 90, 0},
                                  {"Roll", "wrist_roll", -150, 150, 0},      {"Gripper", "gripper", 0, 100, 50}};
static const int ARM_N = sizeof(JOINTS) / sizeof(JOINTS[0]), GRIP = ARM_N - 1;
// Arm layers map to joints by name, so a lineup without some joints (e.g. no Roll) still works.
static int armJoint(const Mode &m, int layer) {
  if (layer < 0 || layer >= (int)m.layers.size()) return -1;
  for (int k = 0; k < ARM_N; k++)
    if (!strcasecmp(m.layers[layer].name, JOINTS[k].name)) return k;
  return -1;
}
static const char *TYPING_WORDS[] = {"the", "puck", "should", "feel", "like", "a", "third", "hand", "on", "the", "desk"};
static const int TYPING_WORD_COUNT = sizeof(TYPING_WORDS) / sizeof(TYPING_WORDS[0]);

struct State {
  bool ready = false;
  int layer = 0;
  int vol = 45, i = 0, pos = 0, line = 142, word = 3, hist = 0, frame = 2003, face = 0, min = 25;
  bool muted = false, on = true, playing = true, running = false;
  float v = 0, left = 0, mpos = 62, len = 243;
  uint32_t touched = 0, flash = 0, pressedAt = 0, start = 0, last = 0;
  int arm[ARM_N] = {};
  std::string armStatus;  // from tools/arm_bridge.py, e.g. "armed"; empty until it connects
  float pct = 37;  // printer: simulated job
  int page = 0;
  uint32_t doneAt = 0;
  std::string pstate = "Printing";
  int btIdx = 0, btPaired = 0;  // settings: Bluetooth menu position / computers connected when pairing began
  uint32_t skipAt = 0, lastDraw = 0, pulseAt = 0;  // media: last swipe / last frame / when the screen appeared
  bool skipNext = false;
  std::string skipTxt;
  uint32_t spAt = 0, spErrAt = 0;  // media: last live Spotify update / last Spotify error
  std::string spState, spDevice, spErr;
  int row = 0, edit = 0, tzIdx = 0, bright = 80;  // settings (edit: see SettingsEdit)
  std::string app = "Zoom", file = "notes.md", track, artist;  // media: empty until Spotify or the computer says
  // Sidekick: the tools the computer asked for, their values, the current one, and the press action.
  std::vector<int> skTools;
  int skTi = 0, skVals[16] = {};
  bool skCall = false, skSet = false;
  bool skText = false, skSelect = false;  // typing in a text box: Cursor / Word jump, press toggles selecting
  std::string skAppId = "default";
  bool skView3d = false;  // a 3D view (Bambu Studio's plate): Spin and Tilt by dragging, press cycles the step
  int skStep = 0;         // index into ORBIT_STEPS
  std::string skApp = "Computer", skWhy = "Any app", skPressAct = "usb.play_pause", skPressArg, skPressLabel = "Play / pause";
  uint32_t skSwitched = 0, skPendingAt = 0;
};

static std::vector<State> states;
static void savePref(const char *key, const char *str, int num);

// Settings mode (defined further down, next to the time zone code).
static void drawSettings(Mode &m, State &s);
static void settingsRotate(State &s, int d);
static void settingsPress(Mode &m, State &s);
static int current = 0;  // index into modes
static bool autoMode = true;
static bool menuOpen = false;
static uint32_t lastInput = 0;  // last knob turn or press
static int menuIdx = 0;
static uint32_t menuAt = 0;  // last turn in the hold menu; it picks by itself after MENU_AUTO_MS
constexpr uint32_t MENU_AUTO_MS = 5000;
static SendFn sendFn = nullptr;
static void (*spotifyFn)(const char *action) = nullptr;
static void (*armMoveFn)(const char *joint, float value) = nullptr;
static void (*armSetFn)(bool armed) = nullptr;
static bool (*armLinkedFn)() = nullptr;

static State &stateOf(int idx) {
  State &s = states[idx];
  Mode &m = modes[idx];
  if (!s.ready) {
    s = State();
    s.ready = true;
    switch (m.tpl) {
      case IDLE: s.vol = 45; break;
      case CALLS: s.muted = true, s.vol = 70, s.start = nowMs(); break;
      case MEDIA: s.vol = 40, s.last = nowMs(); break;
      case VALUE: s.v = m.value; break;
      case TIMER: s.min = m.minutes, s.left = m.minutes * 60, s.last = nowMs(); break;
      case ARM:
        for (int k = 0; k < ARM_N; k++) s.arm[k] = JOINTS[k].start;
        break;
      case CLOCKFACE: s.face = m.face < 0 ? 0 : m.face; break;
      case PRINTER: s.last = nowMs(); break;
      case SETTINGS: s.bright = m.brightness; break;
      default: break;
    }
  }
  if (s.layer >= (int)m.layers.size()) s.layer = 0;
  return s;
}

static int modeIndex(const char *id) {
  for (size_t k = 0; k < modes.size(); k++)
    if (!strcmp(modes[k].id, id)) return (int)k;
  return -1;
}

// ---------------------------------------------------------------- what's playing (Spotify face)

// The puck's own Spotify data, kept here too so the Spotify watch face works without a Media mode.
static struct {
  uint32_t at = 0;  // when it last arrived
  std::string state, track, artist;
  float pos = 0, len = 1;
  uint32_t art = 0x1ed760;
} music;

static void fillFaceMusic() {
  bool live = music.at && millis() - music.at < 90000;
  faceMusic.on = live && music.state != "idle" && !music.track.empty();
  faceMusic.playing = music.state == "playing";
  faceMusic.track = music.track.c_str();
  faceMusic.artist = music.artist.c_str();
  faceMusic.len = music.len;
  faceMusic.pos = fminf(music.len, music.pos + (faceMusic.playing ? (millis() - music.at) / 1000.0f : 0));
  faceMusic.art = music.art;
}

// Press on Home with the Spotify face: play or pause, and show it right away.
static void musicPlayPause() {
  fillFaceMusic();
  music.pos = faceMusic.pos;
  music.at = millis();
  if (music.state == "playing") music.state = "paused";
  else if (music.state == "paused") music.state = "playing";
}

// ---------------------------------------------------------------- clock

static Clock clockNow() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  time_t s = tv.tv_sec;
  struct tm tm;
  localtime_r(&s, &tm);
  Clock c;
  c.h24 = tm.tm_hour, c.h = tm.tm_hour % 12, c.mi = tm.tm_min, c.date = tm.tm_mday;
  c.se = tm.tm_sec + tv.tv_usec / 1e6f;
  c.aS = c.se * 6, c.aM = (c.mi + c.se / 60) * 6, c.aH = (c.h + c.mi / 60.0f) * 30;
  int year = tm.tm_year + 1900, yearDays = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
  c.dayF = (c.h24 * 3600 + c.mi * 60 + c.se) / 86400.0f;
  c.yearF = (tm.tm_yday + c.dayF) / yearDays;
  snprintf(c.hh, sizeof c.hh, "%d", c.h ? c.h : 12);
  snprintf(c.mm, sizeof c.mm, "%02d", c.mi);
  static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  strcpy(c.day, days[tm.tm_wday]);
  return c;
}

static void setClockUtc(time_t utc) {
  struct timeval tv = {utc, 0};
  settimeofday(&tv, nullptr);
}

// The computer tools send local time as Unix seconds; turn it back into real (UTC) time.
static void setClockLocal(time_t localEpoch) {
  struct tm tm;
  gmtime_r(&localEpoch, &tm);
  tm.tm_isdst = -1;
  setClockUtc(mktime(&tm));
}

// Until the computer sends the time, start from when the firmware was built.
static void clockFromBuild() {
  static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4];
  struct tm tm = {};
  sscanf(__DATE__, "%3s %d %d", mon, &tm.tm_mday, &tm.tm_year);
  sscanf(__TIME__, "%d:%d:%d", &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
  tm.tm_mon = (int)((strstr(months, mon) - months) / 3);
  tm.tm_year -= 1900;
  tm.tm_isdst = -1;
  setClockUtc(mktime(&tm));
}

// ---------------------------------------------------------------- events to the computer

// The call app on screen (zoom, meet, teams), for the mute press.
static std::string callApp;

// Each call app's own mute shortcut, sent over Bluetooth. Zoom's works from anywhere once "Enable
// global shortcut" is on in its settings; Meet's needs the Meet tab in front. Ctrl becomes Cmd on a Mac.
static const char *muteKey(const std::string &app) {
  if (app == "zoom") return "pc:alt+a|mac:cmd+shift+a";
  if (app == "meet") return "ctrl+d";
  if (app == "teams") return "ctrl+shift+m";
  return nullptr;
}

static void emit(int idx, const Bind &b, const char *input, const char *tool = nullptr) {
  if (b.act && !strcmp(b.act, "companion.mic_toggle") && muteKey(callApp)) ble::send("key", muteKey(callApp));
  if (b.act && !strncmp(b.act, "spotify.", 8) && spotifyFn) spotifyFn(b.act + 8);  // no-op unless linked
  if (b.act && !strncmp(b.act, "usb.", 4)) ble::send(b.act + 4, b.arg);               // volume, keys, scroll over Bluetooth
  if (b.act && armSetFn && (!strcmp(b.act, "robotarm.arm") || !strcmp(b.act, "robotarm.disarm")))
    armSetFn(!strcmp(b.act, "robotarm.arm"));  // Safety layer; no-op unless the arm is linked
  if (!b.act || !strcmp(b.act, "puck.none") || !sendFn) return;
  Mode &m = modes[idx];
  State &s = stateOf(idx);
  JsonDocument doc;
  doc["evt"] = "action";
  doc["mode"] = m.id;
  doc["in"] = input;
  doc["do"] = b.act;
  if (tool) doc["tool"] = tool;
  if (b.arg) doc["arg"] = b.arg;
  if (!strcmp(b.act, "puck.cycle")) {
    doc["layer"] = m.layers[s.layer].name;
  } else if (m.tpl == ARM && armJoint(m, s.layer) >= 0) {
    int k = armJoint(m, s.layer);
    doc["joint"] = JOINTS[k].key;
    doc["value"] = s.arm[k];
    if (armMoveFn && !strncmp(input, "turn", 4)) armMoveFn(JOINTS[k].key, s.arm[k]);
  } else if (m.tpl == CLOCKFACE) {
    doc["face"] = FACES[s.face].name;
  }
  if (m.tpl == ARM && armLinkedFn && armLinkedFn()) doc["direct"] = true;  // the puck drives the arm itself
  sendFn(doc);
}

static void sendPin(const char *id) {
  if (!sendFn) return;
  JsonDocument doc;
  doc["evt"] = "pin";
  if (id) doc["mode"] = id;
  else doc["mode"] = nullptr;
  sendFn(doc);
}

// ---------------------------------------------------------------- screen templates

static const char *layerName(Mode &m, State &s) { return m.layers[s.layer].name; }

static void pressLabel(Mode &m, char *out, size_t n) {
  if (!strcmp(m.press.act, "puck.cycle")) {
    snprintf(out, n, "Press for next layer");
    return;
  }
  std::string l = actionLabel(m.press.act);
  for (auto &ch : l) ch = tolower(ch);
  snprintf(out, n, "Press: %s", l.c_str());
}

static void mmss(float secs, char *out, size_t n) {
  int s = (int)secs;
  snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

static void drawIdle(Mode &m, State &s) {
  char buf[48];
  if (m.face >= 0 && m.face < FACE_COUNT) {
    FACES[m.face].draw(clockNow());
    if (recent(s.touched, 1500) || s.muted) {
      rect(0, 0, W, W, BLACK, 0.55f);
      valueArc(186, 10, s.muted ? 0 : s.vol, WHITE);
      if (s.muted) snprintf(buf, sizeof buf, "Sound off");
      else snprintf(buf, sizeof buf, "Volume %d", s.vol);
      label(buf, C, 34, WHITE);
    } else if (net::screenHint()) {
      rect(C - 150, C + 128, 300, 40, BLACK, 0.75f, 20);
      label(net::screenHint(), C + 148, 15, AMBER, 290);
    }
    return;
  }
  Clock t = clockNow();
  snprintf(buf, sizeof buf, "%s:%s", t.hh, t.mm);
  label(buf, C - 10, 86, WHITE);
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  time_t sec = tv.tv_sec;
  struct tm tm;
  localtime_r(&sec, &tm);
  size_t n = strftime(buf, sizeof buf, "%A, %b ", &tm);
  snprintf(buf + n, sizeof buf - n, "%d", tm.tm_mday);
  label(buf, C + 58, 20, DIM);
  if (net::screenHint()) label(net::screenHint(), C + 128, 15, AMBER, 300);
  else label("Turn to choose a mode", C + 128, 15, DIM);
}

static void drawCalls(Mode &m, State &s) {
  char buf[64], t[12];
  if (!s.muted) {
    float p = 0.75f + 0.25f * sinf(nowMs() / 380.0f);
    arc(199, 12, 0, 360, m.color, p);
    arc(199, 26, 0, 360, m.color, 0.12f * p);
  } else arc(199, 4, 0, 360, FAINT);
  valueArc(170, 10, s.vol, s.muted ? DIM : WHITE);
  mmss((nowMs() - s.start) / 1000.0f, t, sizeof t);
  snprintf(buf, sizeof buf, "%s - %s", s.app.c_str(), t);
  label(buf, 96, 17, DIM);
  mic(C, C + 6, 1.35f, s.muted ? DIM : m.color, s.muted);
  label(s.muted ? "MUTED" : "LIVE", C + 70, 30, s.muted ? DIM : m.color);
  snprintf(buf, sizeof buf, "Call volume %d%%", s.vol);
  label(buf, C + 108, 16, DIM);
}

static void drawTyping(Mode &m, State &s) {
  char buf[48], pl[48];
  arc(192, 4, 0, 360, FAINT);
  float a = -90 + s.pos * 18;
  arc(192, 10, a - 9, a + 9, m.color);
  label(s.file.c_str(), 108, 17, DIM);
  label(layerName(m, s), C - 30, 34, WHITE);
  if (s.layer == 0) snprintf(buf, sizeof buf, "Line %d", s.line);
  else if (s.layer == 1) snprintf(buf, sizeof buf, "%s", TYPING_WORDS[s.word]);
  else if (s.hist) snprintf(buf, sizeof buf, "%d step%s back", s.hist, s.hist > 1 ? "s" : "");
  else snprintf(buf, sizeof buf, "Latest edit");
  label(buf, C + 24, s.layer == 1 ? 28 : 22, m.color);
  dots(m.layers.size(), s.layer, C + 110);
  pressLabel(m, pl, sizeof pl);
  label(pl, C + 138, 14, DIM);
}

static void drawMedia(Mode &m, State &s) {
  uint32_t t = nowMs();
  bool live = s.spAt && recent(s.spAt, 90000);  // Spotify data from the puck's own connection
  bool idle = live && s.spState == "idle";
  // No live Spotify data and nothing from the computer: say why instead of showing an empty player.
  bool waiting = !live && s.track.empty();
  if (s.playing && !idle) s.mpos = fminf(s.len, s.mpos + (t - s.last) / 1000.0f);
  s.last = t;
  if (t - s.lastDraw > 2000) s.pulseAt = t;  // screen just appeared
  s.lastDraw = t;
  arc(196, 5, -90, 270, FAINT);
  if (!idle && !waiting && s.len > 0) arc(196, 5, -90, -90 + 360 * s.mpos / s.len, m.color);
  valueArc(172, 8, s.vol, DIM);
  if (waiting) {
    const char *why;
    if (s.spErr == "relink") why = "Sign in again from a computer";
    else if (!spotify::linked()) why = "Link it from a computer";
    else if (net::screenHint()) why = net::screenHint();
    else if (s.spErr == "no connection") why = "Can't reach Spotify";
    else if (!s.spErr.empty()) why = s.spErr.c_str();
    else why = "Connecting...";
    label("Spotify", C - 46, 26, WHITE);
    label(why, C - 12, 16, s.spErr.empty() ? DIM : AMBER, 300);
    return;
  }
  if (idle) {
    label("Nothing playing", C - 46, 26, WHITE);
    label("Start Spotify on any device", C - 12, 16, DIM, 300);
  } else {
    label(s.track.c_str(), C - 62, 26, WHITE);
    label(s.artist.c_str(), C - 30, 18, DIM);
  }
  if (!idle) {
    if (s.playing) {
      rect(C - 17, C + 8, 11, 40, WHITE);
      rect(C + 6, C + 8, 11, 40, WHITE);
    } else triangle(C - 13, C + 6, C + 22, C + 28, C - 13, C + 50, WHITE);
  }
  // Swipe hints: faint chevrons at both sides, breathing for the first few seconds so they get noticed.
  if (live && !idle) {
    float pulse = t - s.pulseAt < 8000 ? 0.55f + 0.45f * sinf((t - s.pulseAt) / 260.0f) : 0.45f;
    bool flashL = recent(s.skipAt, 400) && !s.skipNext, flashR = recent(s.skipAt, 400) && s.skipNext;
    float yc = C;
    line(88, yc - 15, 72, yc, 5, flashL ? WHITE : DIM, flashL ? 1 : pulse);
    line(72, yc, 88, yc + 15, 5, flashL ? WHITE : DIM, flashL ? 1 : pulse);
    line(W - 88, yc - 15, W - 72, yc, 5, flashR ? WHITE : DIM, flashR ? 1 : pulse);
    line(W - 72, yc, W - 88, yc + 15, 5, flashR ? WHITE : DIM, flashR ? 1 : pulse);
  }
  char buf[64], a[12], b[12];
  bool r = recent(s.touched, 1500);
  uint32_t col = DIM;
  if (recent(s.skipAt, 1200)) {
    snprintf(buf, sizeof buf, "%s", s.skipTxt.c_str());
    col = WHITE;
  } else if (live && recent(s.spErrAt, 4000)) {
    if (s.spErr == "premium") snprintf(buf, sizeof buf, "Needs Spotify Premium");
    else if (s.spErr == "volume") snprintf(buf, sizeof buf, "This device locks volume");
    else if (s.spErr == "no active device") snprintf(buf, sizeof buf, "No active device");
    else snprintf(buf, sizeof buf, "Spotify: %s", s.spErr.c_str());
    col = AMBER;
  } else if (r) {
    snprintf(buf, sizeof buf, "Volume %d", s.vol);
    col = WHITE;
  } else if (live && !s.spDevice.empty()) {
    snprintf(buf, sizeof buf, "Spotify - %s", s.spDevice.c_str());
  } else if (idle) {
    buf[0] = 0;
  } else {
    mmss(s.mpos, a, sizeof a), mmss(s.len, b, sizeof b);
    snprintf(buf, sizeof buf, "%s / %s", a, b);
  }
  label(buf, C + 92, 17, col, 300);
}

static void drawEditing(Mode &m, State &s) {
  // Jog wheel: tick marks turn with the frame count.
  for (int k = 0; k < 60; k++) {
    float a = k * 6 + s.frame * 6 + 90;  // prototype angles start at 3 o'clock; tick() starts at 12
    float w = k % 5 ? 6 : 14;
    tick(a, 190 - w / 2, 190 + w / 2, 4, k % 5 ? FAINT : DIM);
  }
  tick(0, 181, 199, 11, m.color);
  int f = s.frame, fps = 24;
  char tc[16], buf[40], lower[24];
  snprintf(tc, sizeof tc, "%02d:%02d:%02d:%02d", f / fps / 3600, (f / fps / 60) % 60, (f / fps) % 60, f % fps);
  label(s.file.c_str(), 112, 17, DIM);
  label(tc, C, 40, WHITE);
  snprintf(lower, sizeof lower, "%s", layerName(m, s));
  for (char *p = lower; *p; p++) *p = tolower(*p);
  snprintf(buf, sizeof buf, "Jog by %s", lower);
  label(buf, C + 48, 20, m.color);
  dots(m.layers.size(), s.layer, C + 100);
  label("24 fps", C + 128, 14, DIM);
}

static void drawValue(Mode &m, State &s) {
  char buf[32], pl[48];
  float span = (m.max - m.min) ? (m.max - m.min) : 1;
  valueArc(178, 14, 100 * (s.v - m.min) / span, s.on ? m.color : DIM);
  label(m.name, 104, 19, DIM, 230);
  snprintf(buf, sizeof buf, "%d%s", (int)s.v, m.unit);
  label(buf, C - 4, 62, s.on ? WHITE : DIM);
  label(m.layers.size() > 1 || s.on ? layerName(m, s) : "Off", C + 52, 20, m.color);
  dots(m.layers.size(), s.layer, C + 92);
  pressLabel(m, pl, sizeof pl);
  label(pl, C + 128, 14, DIM, 240);
}

static void drawList(Mode &m, State &s) {
  char buf[32], pl[48];
  int n = m.items.size();
  if (!n) n = 1;
  int i = s.i % n;
  auto item = [&](int k) { return m.items.empty() ? "(empty)" : m.items[k]; };
  float seg = 360.0f / n;
  for (int k = 0; k < n; k++) {
    float a = -90 + k * seg;
    arc(190, 10, a + 3, a + seg - 3, k == i ? m.color : FAINT);
  }
  label(m.name, 100, 18, DIM, 230);
  if (n > 1) label(item((i - 1 + n) % n), C - 50, 18, DIM);
  label(item(i), C, 32, WHITE);
  if (n > 1) label(item((i + 1) % n), C + 50, 18, DIM);
  bool flash = recent(s.flash, 900);
  snprintf(buf, sizeof buf, "%d of %d", i + 1, n);
  label(flash ? "Sent" : buf, C + 96, 16, flash ? m.color : DIM);
  pressLabel(m, pl, sizeof pl);
  label(pl, C + 128, 14, DIM, 240);
}

static void drawTimer(Mode &m, State &s) {
  uint32_t t = nowMs();
  if (s.running) {
    s.left = fmaxf(0, s.left - (t - s.last) / 1000.0f);
    if (s.left <= 0) s.running = false;
  }
  s.last = t;
  arc(186, 14, 0, 360, FAINT);
  float f = s.left / (s.min * 60);
  if (f > 0) arc(186, 14, -90, -90 + 360 * f, m.color);
  label(m.name, 104, 19, DIM, 230);
  char buf[16], pl[48];
  mmss(ceilf(s.left), buf, sizeof buf);
  label(buf, C - 2, 64, WHITE);
  label(s.left <= 0 ? "Done" : s.running ? "Running" : "Turn to set, press to start", C + 56, 17,
        s.running ? m.color : DIM);
  pressLabel(m, pl, sizeof pl);
  label(pl, C + 128, 14, DIM, 240);
}

static void drawArm(Mode &m, State &s) {
  int base = s.arm[0], sh = s.arm[1], el = s.arm[2], wr = s.arm[3], gr = s.arm[GRIP], cur = armJoint(m, s.layer);
  auto col = [&](int i) { return i == cur ? m.color : WHITE; };
  arc(192, 4, 0, 360, FAINT);
  arc(192, 12, base - 90 - 5, base - 90 + 5, cur == 0 ? m.color : DIM);
  char buf[48];
  snprintf(buf, sizeof buf, "%s - %s", m.name, s.armStatus.empty() ? m.host : s.armStatus.c_str());
  label(buf, 96, 15, DIM, 230);
  if (cur < 0 && !strcasecmp(m.layers[s.layer].name, "Safety")) {
    bool live = s.armStatus == "armed";
    label("Safety", 124, 20, m.color);
    label(live ? "Armed" : s.armStatus.empty() ? "Not linked" : "Disarmed", 160, 34, live ? m.color : WHITE);
    label(live ? "Turn left to disarm" : "Turn right to arm", 196, 16, DIM, 220);
  }
  if (cur >= 0) {
    label(JOINTS[cur].name, 124, 20, m.color);
    if (cur == GRIP) snprintf(buf, sizeof buf, "%d%%", gr);
    else snprintf(buf, sizeof buf, "%d", s.arm[cur]);
    label(buf, 160, 38, WHITE);
  }
  if (cur >= 0 && cur != GRIP) {  // degree sign (not in the built-in font)
    float w = textWidth(buf, 38);
    ring(C + w / 2 + 8, 146, 4, 2.5f, WHITE, 0, false);
  }
  // Side view: shoulder, elbow, wrist, gripper.
  const float ox = C - 34, oy = C + 100, L1 = 80, L2 = 66, L3 = 22;
  auto rad = [](float d) { return d * (float)M_PI / 180; };
  float a1 = -(sh + 90), a2 = a1 + el, a3 = a2 + wr;
  float p1x = ox + L1 * cosf(rad(a1)), p1y = oy + L1 * sinf(rad(a1));
  float p2x = p1x + L2 * cosf(rad(a2)), p2y = p1y + L2 * sinf(rad(a2));
  float p3x = p2x + L3 * cosf(rad(a3)), p3y = p2y + L3 * sinf(rad(a3));
  rect(ox - 22, oy + 4, 44, 8, cur == 0 ? m.color : DIM);
  line(ox, oy + 4, ox, oy, 8, cur == 0 ? m.color : DIM);
  line(ox, oy, p1x, p1y, 9, col(1));
  line(p1x, p1y, p2x, p2y, 7, col(2));
  line(p2x, p2y, p3x, p3y, 5, cur == 4 ? m.color : col(3));
  float jaw = 4 + gr / 10.0f, nx = cosf(rad(a3 + 90)), ny = sinf(rad(a3 + 90)), fx = cosf(rad(a3)), fy = sinf(rad(a3));
  for (int sgn : {1, -1}) {
    float bx = p3x + nx * jaw * sgn, by = p3y + ny * jaw * sgn;
    line(bx, by, bx + fx * 14, by + fy * 14, 4, col(GRIP));
  }
  float jp[3][2] = {{ox, oy}, {p1x, p1y}, {p2x, p2y}};
  for (int k = 0; k < 3; k++) {
    int i = k + 1;
    ring(jp[k][0], jp[k][1], 5, 2, col(i), (i - 1 == cur || i == cur) ? m.color : BLACK, true);
  }
  dots(m.layers.size(), s.layer, C + 132);
}

static void drawClockFace(Mode &m, State &s) {
  FACES[s.face].draw(clockNow());
  bool pressed = recent(s.pressedAt, 1400), turned = recent(s.touched, 1400);
  if (pressed || turned) {
    rect(C - 120, C + 118, 240, 50, BLACK, 0.7f, 25);
    char buf[40];
    snprintf(buf, sizeof buf, "%s - %d of %d", FACES[s.face].name, s.face + 1, FACE_COUNT);
    label(pressed ? "Set as Home face" : buf, C + 143, 20, WHITE, 220);
  }
}

// Live printer status from the computer ({"t":"printer",...}); counts as fresh for 5 minutes.
struct PrinterLive {
  uint32_t at = 0;
  std::string state, file;
  float percent = 0, remainingMin = 0, nozzle = 0, bed = 0;
  int layer = -1, totalLayers = -1;
};
static PrinterLive live;
static bool liveFresh() { return recent(live.at, 5 * 60 * 1000); }

static const char *printState(const std::string &s) {
  static const char *names[][2] = {{"RUNNING", "Printing"}, {"PAUSE", "Paused"},  {"FINISH", "Finished"},
                                   {"FAILED", "Failed"},    {"IDLE", "Idle"},     {"PREPARE", "Preparing"},
                                   {"SLICING", "Preparing"}};
  for (auto &n : names)
    if (s == n[0]) return n[1];
  return s.empty() ? "Unknown" : s.c_str();
}

static void fmtLeft(int leftMin, char *out, size_t n) {
  if (leftMin >= 60) snprintf(out, n, "%dh %dm", leftMin / 60, leftMin % 60);
  else snprintf(out, n, "%dm", leftMin);
}

static void fmtEta(int leftMin, char *out, size_t n) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  time_t t = tv.tv_sec + leftMin * 60;
  struct tm tm;
  localtime_r(&t, &tm);
  snprintf(out, n, "Done at %d:%02d", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min);
}

// A temperature with a drawn degree sign (the built-in font has none).
static void temperature(int v, float y) {
  char buf[12];
  snprintf(buf, sizeof buf, "%d", v);
  label(buf, y, 40, WHITE);
  ring(C + textWidth(buf, 40) / 2 + 8, y - 12, 4, 2.5f, WHITE, 0, false);
}

static void drawPrinter(Mode &m, State &s) {
  uint32_t t = nowMs();
  float dt = (t - s.last) / 1000.0f;
  s.last = t;
  bool isLive = liveFresh();
  std::string state;
  float pct;
  int leftMin, layer, total;
  int noz, bed;
  if (isLive) {
    state = printState(live.state);
    pct = clampf(live.percent, 0, 100);
    float since = (t - live.at) / 60000.0f;
    leftMin = max(0, (int)lroundf(live.remainingMin - (live.state == "RUNNING" ? since : 0)));
    layer = live.layer, total = live.totalLayers;
    noz = lroundf(live.nozzle), bed = lroundf(live.bed);
  } else {
    // Simulated job: runs fast so a whole print plays out in about 6 minutes.
    if (s.pstate == "Printing") {
      s.pct = fminf(100, s.pct + dt * 0.28f);
      if (s.pct >= 100) s.pstate = "Finished", s.doneAt = t;
    }
    if (s.pstate == "Finished" && t - s.doneAt > 8000) s.pct = 0, s.pstate = "Printing";
    state = s.pstate;
    pct = s.pct;
    total = m.totalLayers > 0 ? m.totalLayers : 300;
    layer = max(1, (int)lroundf(total * s.pct / 100));
    leftMin = lroundf((100 - s.pct) / 100 * (m.hours > 0 ? m.hours : 2) * 60);
    bool done = state == "Finished";
    noz = done ? 45 : 220, bed = done ? 30 : 65;
  }
  uint32_t col = state == "Paused" ? AMBER : state == "Finished" ? GREEN : state == "Failed" ? RED : state == "Idle" ? DIM : m.color;
  arc(184, 16, -90, 270, col, 0.16f);
  if (pct > 0) arc(184, 16, -90, -90 + 3.6f * pct, col);
  label(isLive && !live.file.empty() ? live.file.c_str() : isLive ? m.printer : m.file, 100, 16, DIM, 230);
  char buf[40], left[16];
  fmtLeft(leftMin, left, sizeof left);
  if (s.page == 0) {
    snprintf(buf, sizeof buf, "%d%%", (int)pct);
    label(buf, C - 6, 72, WHITE);
    if (state == "Printing") snprintf(buf, sizeof buf, "%s left", left);
    else if (state == "Finished") snprintf(buf, sizeof buf, "Done, take it off the plate");
    else snprintf(buf, sizeof buf, "%s", state.c_str());
    label(buf, C + 52, 20, col, 260);
  } else if (s.page == 1) {
    label("Nozzle", C - 56, 16, DIM);
    temperature(noz, C - 24);
    label("Bed", C + 22, 16, DIM);
    temperature(bed, C + 54);
  } else {
    label("Layer", C - 56, 16, DIM);
    if (layer >= 0) snprintf(buf, sizeof buf, "%d", layer);
    else snprintf(buf, sizeof buf, "-");
    label(buf, C - 12, 64, WHITE);
    if (total >= 0) snprintf(buf, sizeof buf, "of %d", total);
    else snprintf(buf, sizeof buf, "of -");
    label(buf, C + 36, 20, DIM);
    if (state == "Printing") fmtEta(leftMin, buf, sizeof buf);
    else snprintf(buf, sizeof buf, "%s", state.c_str());
    label(buf, C + 70, 18, col);
  }
  dots(3, s.page, C + 104);
  if (recent(s.flash, 1200)) {
    if (!strcmp(m.press.act, "bambu.light")) snprintf(buf, sizeof buf, "Light %s", s.on ? "on" : "off");
    else snprintf(buf, sizeof buf, "Sent");
    label(buf, C + 134, 15, col, 240);
    return;
  }
  if (isLive) {
    uint32_t ago = (t - live.at) / 1000;
    if (ago < 60) snprintf(buf, sizeof buf, "Live - %us ago", (unsigned)ago);
    else snprintf(buf, sizeof buf, "Live - %um ago", (unsigned)(ago / 60));
  } else {
    snprintf(buf, sizeof buf, "Simulated - %s", state.c_str());
  }
  label(buf, C + 134, 15, isLive ? col : DIM, 240);
}

// The hold menu shows the selected mode only. Picking Home unpins (the puck follows the computer again);
// picking any other mode pins it.
// ---------------------------------------------------------------- sidekick

// One adaptive mode: the computer says which app is in front, and the dial gets up to three tools
// for it plus one press action. Turn = current tool, press = the app's main action, double press =
// next tool. The rules live here, so the computer only has to send the app id
// ({"t":"ctx","app":"vscode"}); a full {"t":"sidekick",...} message can override them.
struct Tool {
  const char *id, *name;
  Bind left, right;
  int pct, step, min, max;  // pct >= 0: a percentage value (start, step, range); otherwise a counter
};
static const Tool TOOLBOX[] = {
    {"scroll", "Scroll", {"usb.scroll_up", nullptr}, {"usb.scroll_down", nullptr}, -1, 1, -9999, 9999},
    {"volume", "Volume", {"usb.volume_down", nullptr}, {"usb.volume_up", nullptr}, 40, 2, 0, 100},
    {"word", "Word jump", {"usb.key", "ctrl+left"}, {"usb.key", "ctrl+right"}, -1, 1, -9999, 9999},
    {"cursor", "Cursor", {"usb.key", "left"}, {"usb.key", "right"}, -1, 1, -9999, 9999},
    {"spin", "Spin (Z)", {"usb.orbit", nullptr}, {"usb.orbit", nullptr}, -1, 1, -99999, 99999},
    {"tilt", "Tilt (X)", {"usb.orbit", nullptr}, {"usb.orbit", nullptr}, -1, 1, -99999, 99999},
    {"undo", "Undo / redo", {"usb.key", "ctrl+z"}, {"usb.key", "ctrl+y"}, -1, 1, -20, 0},
    {"zoom", "Zoom", {"usb.key", "ctrl+-"}, {"usb.key", "ctrl+="}, 100, 10, 10, 800},
    {"scrub", "Scrub", {"usb.key", "left"}, {"usb.key", "right"}, -1, 1, -9999, 9999},
    {"frames", "Frame by frame", {"usb.key", "left"}, {"usb.key", "right"}, -1, 1, -9999, 9999},
    {"slides", "Slides", {"usb.key", "left"}, {"usb.key", "right"}, -1, 1, -9999, 9999},
    // "control" stays Ctrl on a Mac too (plain "ctrl" becomes Cmd there, and Cmd+Tab is the app switcher).
    {"tabs", "Tabs", {"usb.key", "control+shift+tab"}, {"usb.key", "control+tab"}, -1, 1, -9999, 9999},
    {"track", "Track", {"spotify.prev", nullptr}, {"spotify.next", nullptr}, -1, 1, -9999, 9999},
    {"channels", "Channels", {"usb.key", "alt+up"}, {"usb.key", "alt+down"}, -1, 1, -9999, 9999},
    {"scenes", "Scenes", {"obs.scene_prev", nullptr}, {"obs.scene_next", nullptr}, -1, 1, -9999, 9999},
};
constexpr int TOOL_COUNT = sizeof(TOOLBOX) / sizeof(TOOLBOX[0]);

struct Rule {
  const char *app, *why;
  bool call;
  const char *tools[3];
  const char *act, *arg, *label;
};
// Same as the simulator's SIDEKICK_RULES.
static const Rule RULES[] = {
    {"zoom", "In a meeting", true, {}, "companion.mic_toggle", nullptr, "Mute / unmute"},
    {"meet", "In a meeting", true, {}, "companion.mic_toggle", nullptr, "Mute / unmute"},
    {"teams", "In a meeting", true, {}, "companion.mic_toggle", nullptr, "Mute / unmute"},
    {"vscode", "Editing code", false, {"scroll", "word", "undo"}, "usb.key", "ctrl+s", "Save"},
    {"word", "Writing", false, {"scroll", "word", "undo"}, "usb.key", "ctrl+s", "Save"},
    {"spotify", "Music", false, {"volume", "track"}, "spotify.play_pause", nullptr, "Play / pause"},
    {"premiere", "Video timeline", false, {"frames", "scrub", "zoom"}, "usb.key", "space", "Play / pause"},
    {"figma", "Design canvas", false, {"zoom", "undo"}, "usb.key", "shift+1", "Zoom to fit"},
    {"powerpoint", "Presenting", false, {"slides"}, "usb.key", "b", "Black screen"},
    {"slack", "Chat", false, {"channels", "scroll"}, "usb.key", "enter", "Open channel"},
    {"obs", "Streaming", false, {"scenes", "volume"}, "obs.record", nullptr, "Start / stop recording"},
    {"chrome", "Reading a page", false, {"scroll", "tabs", "zoom"}, "usb.key", "alt+left", "Back"},
    {"youtube", "Video playing", false, {"scrub", "volume"}, "usb.key", "k", "Play / pause"},
};
static const Rule DEFAULT_RULE = {"default", "Any app", false, {"scroll", "volume"}, "usb.play_pause", nullptr, "Play / pause"};

static int toolIndex(const char *idOrName) {
  for (int k = 0; k < TOOL_COUNT; k++)
    if (!strcasecmp(TOOLBOX[k].id, idOrName) || !strcasecmp(TOOLBOX[k].name, idOrName)) return k;
  return -1;
}

static void sidekickReset(State &s) {
  s.skTi = 0;
  memset(s.skVals, 0, sizeof s.skVals);
  for (size_t k = 0; k < s.skTools.size() && k < 16; k++)
    if (TOOLBOX[s.skTools[k]].pct >= 0) s.skVals[k] = TOOLBOX[s.skTools[k]].pct;
  s.skSwitched = 0;
  s.skSet = true;
}

static void sidekickApplyRule(State &s, const Rule &r, const char *appLabel) {
  s.skTools.clear();
  // The dial cycles tabs first in every app; the app's own tools follow.
  if (!r.call && toolIndex("tabs") >= 0) s.skTools.push_back(toolIndex("tabs"));
  for (const char *t : r.tools)
    if (t && toolIndex(t) >= 0 && strcmp(t, "tabs")) s.skTools.push_back(toolIndex(t));
  s.skCall = r.call;
  s.skWhy = r.why;
  s.skApp = appLabel;
  s.skPressAct = r.act;
  s.skPressArg = r.arg ? r.arg : "";
  s.skPressLabel = r.label;
  if (r.call) s.muted = true, s.vol = 70, s.start = nowMs(), s.app = appLabel;
  s.skText = s.skSelect = false;
  sidekickReset(s);
}

static void sidekickForApp(State &s, const char *app, const char *label) {
  s.skAppId = app;
  for (auto &r : RULES)
    if (!strcasecmp(r.app, app)) return sidekickApplyRule(s, r, label);
  sidekickApplyRule(s, DEFAULT_RULE, label);
}

// A 3D view has focus (Bambu Studio's prepare view): the dial turns the build plate like a left-drag.
// Spin is a sideways drag (Z, a turntable), Tilt an up-down drag (X). Each detent turns the step.
static const int ORBIT_STEPS[] = {15, 5, 1};
constexpr int ORBIT_PX_PER_DEG = 4;  // drag pixels per degree; tune if a detent turns too much or too little

static void sidekickView3d(State &s, bool on) {
  s.skView3d = on;
  if (!on) return;
  s.skText = s.skSelect = false;
  s.skTools = {toolIndex("spin"), toolIndex("tilt")};
  s.skWhy = "3D view";
  s.skPressLabel = "Step size";
  sidekickReset(s);
}

// A text box has focus: the dial moves the cursor like the arrow keys (by letter or by word), and a
// press starts or stops selecting (Shift held on the arrows).
static void sidekickText(State &s, bool on) {
  if (s.skCall || on == s.skText) return;
  s.skText = on;
  s.skSelect = false;
  if (on) {
    s.skTools = {toolIndex("cursor"), toolIndex("word")};
    s.skWhy = "Typing";
    sidekickReset(s);
  } else {
    std::string id = s.skAppId, label = s.skApp;
    sidekickForApp(s, id.c_str(), label.c_str());  // back to the app's own tools
  }
}

// {"t":"sidekick","app","why","call","text","tools":[{name|id,left,right}],"press":{act,arg,label}}
static void sidekickFromMessage(State &s, JsonObjectConst msg) {
  s.skApp = asciiFold(msg["app"] | "Computer");
  if (!s.skApp.empty()) s.skApp[0] = toupper(s.skApp[0]);
  s.skWhy = asciiFold(msg["why"] | "");
  s.skCall = msg["call"] | false;
  s.skTools.clear();
  for (JsonVariantConst t : msg["tools"].as<JsonArrayConst>()) {
    // Tools are named from the toolbox (as a string or an object with id/name).
    const char *key = t.is<const char *>() ? t.as<const char *>() : (t["id"] | (t["name"] | ""));
    int k = toolIndex(key);
    if (k >= 0 && s.skTools.size() < 3) s.skTools.push_back(k);
  }
  s.skPressAct = msg["press"]["act"] | "puck.none";
  s.skPressArg = msg["press"]["arg"] | "";
  s.skPressLabel = asciiFold(msg["press"]["label"] | "");
  if (s.skCall) s.muted = true, s.vol = 70, s.start = nowMs(), s.app = s.skApp;
  s.skText = false;
  sidekickReset(s);
  s.skView3d = false;
  sidekickView3d(s, msg["view3d"] | false);
  sidekickText(s, msg["text"] | false);
}

static void sidekickRotate(int idx, State &s, int d) {
  if (s.skCall) {
    s.vol = clampf(s.vol + d * 2, 0, 100);
    emit(idx, d > 0 ? B("companion.call_vol_up") : B("companion.call_vol_down"), d > 0 ? "turn_right" : "turn_left");
    return;
  }
  if (s.skTools.empty()) return;
  const Tool &t = TOOLBOX[s.skTools[s.skTi]];
  int &v = s.skVals[s.skTi];
  v = clampf(v + d * t.step, t.min, t.max);
  s.pos += d;
  Bind b = d > 0 ? t.right : t.left;
  if (s.skView3d && !strcmp(b.act, "usb.orbit")) {
    char orbit[16];
    int deg = ORBIT_STEPS[s.skStep];
    v = s.skVals[s.skTi] - d * t.step + d * deg;  // the value counts degrees here
    s.skVals[s.skTi] = v;
    snprintf(orbit, sizeof orbit, "%c:%d", !strcmp(t.id, "spin") ? 'x' : 'y', d * deg * ORBIT_PX_PER_DEG);
    emit(idx, {"usb.orbit", orbit}, d > 0 ? "turn_right" : "turn_left", t.id);
    return;
  }
  std::string shifted;
  if (s.skText && s.skSelect && b.arg) {  // selecting: the same keys with Shift held
    std::string a = b.arg;
    size_t plus = a.rfind('+');
    shifted = plus == std::string::npos ? "shift+" + a : a.substr(0, plus + 1) + "shift+" + a.substr(plus + 1);
    b.arg = shifted.c_str();
  }
  emit(idx, b, d > 0 ? "turn_right" : "turn_left", t.id);
}

static void sidekickPressNow(int idx, State &s) {
  if (s.skView3d) {  // cycle the step: 15, 5, 1 degrees
    s.skStep = (s.skStep + 1) % 3;
    s.flash = nowMs();
    if (sendFn) {
      JsonDocument doc;
      doc["evt"] = "sidekick";
      doc["step_deg"] = ORBIT_STEPS[s.skStep];
      sendFn(doc);
    }
    return;
  }
  if (s.skText) {
    s.skSelect = !s.skSelect;
    s.flash = nowMs();
    if (sendFn) {
      JsonDocument doc;
      doc["evt"] = "sidekick";
      doc["select"] = s.skSelect;
      sendFn(doc);
    }
    return;
  }
  if (s.skCall) s.muted = !s.muted;
  s.flash = nowMs();
  emit(idx, {s.skPressAct.c_str(), s.skPressArg.empty() ? nullptr : s.skPressArg.c_str()}, "press");
}

static void sidekickNextTool(State &s) {
  if (s.skTools.size() < 2) return;
  s.skTi = (s.skTi + 1) % s.skTools.size();
  s.skSwitched = nowMs();
  if (sendFn) {
    JsonDocument doc;
    doc["evt"] = "sidekick";
    doc["tool"] = TOOLBOX[s.skTools[s.skTi]].id;
    sendFn(doc);
  }
}

static void sidekickValue(const Tool &t, int v, char *out, size_t n) {
  if (t.pct >= 0) snprintf(out, n, "%d%%", v);
  else if (!strcmp(t.id, "scroll")) snprintf(out, n, "Line %d", v * 3 + 120 > 1 ? v * 3 + 120 : 1);
  else if (!strcmp(t.id, "word")) {
    if (v) snprintf(out, n, "%d word%s %s", abs(v), abs(v) > 1 ? "s" : "", v > 0 ? "right" : "left");
    else snprintf(out, n, "At cursor");
  } else if (!strcmp(t.id, "spin") || !strcmp(t.id, "tilt")) {
    snprintf(out, n, "%+d deg", v);
  } else if (!strcmp(t.id, "cursor")) {
    if (v) snprintf(out, n, "%d letter%s %s", abs(v), abs(v) > 1 ? "s" : "", v > 0 ? "right" : "left");
    else snprintf(out, n, "At cursor");
  } else if (!strcmp(t.id, "undo")) {
    if (v < 0) snprintf(out, n, "%d step%s back", -v, v < -1 ? "s" : "");
    else snprintf(out, n, "Latest edit");
  } else if (!strcmp(t.id, "scrub")) snprintf(out, n, "%s%d:%02d", v >= 0 ? "+" : "-", abs(v) * 5 / 60, abs(v) * 5 % 60);
  else if (!strcmp(t.id, "frames")) snprintf(out, n, "%s%d frame%s", v >= 0 ? "+" : "-", abs(v), abs(v) == 1 ? "" : "s");
  else if (!strcmp(t.id, "slides")) snprintf(out, n, "Slide %d", 4 + v > 1 ? 4 + v : 1);
  else if (!strcmp(t.id, "tabs")) snprintf(out, n, "Tab %d of 8", ((3 + v) % 8 + 8) % 8 + 1);
  else if (!strcmp(t.id, "track")) {
    if (v) snprintf(out, n, "%d %s", abs(v), v > 0 ? "ahead" : "back");
    else snprintf(out, n, "This song");
  } else if (!strcmp(t.id, "channels")) {
    static const char *ch[] = {"#hardware", "#general", "DMs", "#random"};
    snprintf(out, n, "%s", ch[((v % 4) + 4) % 4]);
  } else if (!strcmp(t.id, "scenes")) {
    static const char *sc[] = {"Camera", "Screen share", "Be right back"};
    snprintf(out, n, "%s", sc[((v % 3) + 3) % 3]);
  } else snprintf(out, n, "%+d", v);
}

static void drawSidekick(Mode &m, State &s) {
  if (!s.skSet) sidekickApplyRule(s, DEFAULT_RULE, "Computer");
  char buf[64];
  if (s.skCall) {
    Mode red = m;
    red.color = RED;
    drawCalls(red, s);
    label("Sidekick", 66, 15, m.color);
    return;
  }
  int n = s.skTools.size();
  const Tool *t = n ? &TOOLBOX[s.skTools[s.skTi]] : nullptr;
  arc(192, 4, 0, 360, FAINT);
  float a = -90 + s.pos * 18;
  arc(192, 10, a - 9, a + 9, m.color);
  snprintf(buf, sizeof buf, "%s - %s", s.skApp.c_str(), s.skWhy.c_str());
  label(buf, 100, 17, DIM, 250);
  bool fresh = recent(s.skSwitched, 1200);
  label(t ? t->name : "Sidekick", C - 30, 36, fresh ? m.color : WHITE, 300);
  if (t) {
    int v = s.skVals[s.skTi];
    if (t->pct >= 0) valueArc(172, 8, 100.0f * (v - t->min) / (t->max - t->min), m.color);
    sidekickValue(*t, v, buf, sizeof buf);
    label(buf, C + 18, t->pct >= 0 ? 26 : 24, m.color, 280);
  }
  if (n > 1) {
    dots(n, s.skTi, C + 62);
    snprintf(buf, sizeof buf, "Double-press: %s", TOOLBOX[s.skTools[(s.skTi + 1) % n]].name);
    label(buf, C + 92, 16, DIM, 260);
  }
  bool flash = recent(s.flash, 900);
  if (s.skView3d) {
    snprintf(buf, sizeof buf, "Step %d deg - press to change", ORBIT_STEPS[s.skStep]);
    label(buf, C + 124, 16, flash ? m.color : DIM, 280);
    return;
  }
  if (s.skText) {
    label(s.skSelect ? "Selecting - press to stop" : "Press: select text", C + 124, 16, s.skSelect ? AMBER : DIM, 260);
    if (s.skSelect) arc(192, 4, 0, 360, AMBER, 0.6f);
    return;
  }
  if (flash) snprintf(buf, sizeof buf, "%s", s.skPressLabel.c_str());
  else snprintf(buf, sizeof buf, "Press: %s", s.skPressLabel.c_str());
  label(buf, C + 124, 16, flash ? m.color : DIM, 240);
}

// ---------------------------------------------------------------- interrupts

// Things the puck watches itself (the printer, Spotify) pop up over whatever is on screen.
// Alerts (print paused, failed, finished) stay until you act: press opens that mode, turn dismisses.
// Glances (print started, song changed) show for 5 s as a band; press opens, turn goes to the mode.
// Nothing pops up during a call; it waits until the call ends. A glance never covers an alert.
struct Interrupt {
  bool active = false, sticky = false;
  std::string kind, title, body, via;
  uint32_t color = 0, at = 0;
  int src = -1;
};
static Interrupt INT;
static std::vector<Interrupt> intWaiting;
constexpr uint32_t GLANCE_MS = 5000;

static bool inCall() {
  if (current < 0 || current >= (int)modes.size()) return false;
  Mode &m = modes[current];
  return m.tpl == CALLS || (m.tpl == SIDEKICK && stateOf(current).skCall);
}

static void sendInterruptEvent(const char *evt, const Interrupt &d) {
  if (!sendFn) return;
  JsonDocument doc;
  doc["evt"] = evt;
  doc["kind"] = d.kind;
  if (d.src >= 0) doc["src"] = modes[d.src].id;
  sendFn(doc);
}

static void showInterrupt(Interrupt d) {
  d.active = true;
  d.at = nowMs();
  INT = d;
  sendInterruptEvent("interrupt", d);
}

static void interrupt(const char *kind, const std::string &title, const std::string &body) {
  Interrupt d;
  d.kind = kind;
  Tpl srcTpl = PRINTER;
  if (!strcmp(kind, "print_pause")) d.title = "Print paused", d.body = "Needs your attention", d.color = AMBER, d.sticky = true;
  else if (!strcmp(kind, "print_failed")) d.title = "Print failed", d.body = "Check the printer", d.color = RED, d.sticky = true;
  else if (!strcmp(kind, "print_finish")) d.title = "Print finished", d.body = "Ready on the plate", d.color = GREEN, d.sticky = true;
  else if (!strcmp(kind, "print_start")) d.title = "Print started", d.body = body, d.color = 0x34d399;
  else if (!strcmp(kind, "song")) d.title = title, d.body = body, d.color = 0x39d98a, d.via = "Spotify", srcTpl = MEDIA;
  else if (!strcmp(kind, "pair_refused"))
    d.title = "Pairing refused", d.body = "Settings > Bluetooth > Pair first", d.color = AMBER, d.via = "Bluetooth", srcTpl = SETTINGS;
  else if (!strcmp(kind, "pair_stale"))
    d.title = "Pair this computer again", d.body = "Remove Smart Puck in its Bluetooth settings", d.color = AMBER, d.via = "Bluetooth",
    d.sticky = true, srcTpl = SETTINGS;
  for (size_t k = 0; k < modes.size(); k++)
    if (modes[k].tpl == srcTpl) {
      d.src = k;
      break;
    }
  if (d.src < 0) return;                         // that mode isn't on the puck
  if (current == d.src && !menuOpen) return;     // already looking at it
  if (inCall()) {                                // wait until the call ends
    for (size_t k = 0; k < intWaiting.size();)
      if (intWaiting[k].src == d.src) intWaiting.erase(intWaiting.begin() + k);
      else k++;
    intWaiting.push_back(d);
    return;
  }
  if (INT.active && INT.sticky && !d.sticky) return;  // a glance never covers an alert
  showInterrupt(d);
}

static void openInterrupt() {
  sendInterruptEvent("interrupt_open", INT);
  int src = INT.src;
  INT.active = false;
  if (src >= 0) current = src;
}

static void dismissInterrupt() {
  sendInterruptEvent("interrupt_dismiss", INT);
  INT.active = false;
}

static void drawInterrupt() {
  if (!INT.active && !intWaiting.empty() && !inCall()) {
    Interrupt d = intWaiting.front();
    intWaiting.erase(intWaiting.begin());
    showInterrupt(d);
  }
  if (!INT.active) return;
  uint32_t age = nowMs() - INT.at;
  if (!INT.sticky && age > GLANCE_MS) {
    INT.active = false;
    return;
  }
  float k = fminf(1, age / 180.0f);
  const char *srcName = INT.src >= 0 ? modes[INT.src].name : "";
  char buf[96];
  if (INT.sticky) {
    disc(C, C, C, BLACK, k);
    float pulse = 0.7f + 0.3f * sinf(nowMs() / 300.0f);
    arc(194, 14, 0, 360, INT.color, pulse * k);
    label(srcName, C - 74, 20, INT.color);
    label(INT.title.c_str(), C - 20, 40, WHITE, 300);
    label(INT.body.c_str(), C + 26, 20, DIM, 280);
    label("Press to open", C + 92, 18, INT.color);
    label("Turn to dismiss", C + 122, 16, DIM);
  } else {
    // A band across the lower part of the screen that empties as it times out.
    const float y0 = C + 50, h = 116;
    rect(0, y0, W, h, 0x0c0e11, 0.95f * k);
    rect(0, y0, W * fmaxf(0, 1 - age / (float)GLANCE_MS), 4, INT.color, k);
    label(INT.title.c_str(), y0 + 34, 24, WHITE, 250);
    const char *via = INT.via.empty() ? srcName : INT.via.c_str();
    if (INT.body.empty()) snprintf(buf, sizeof buf, "%s", via);
    else snprintf(buf, sizeof buf, "%s - %s", INT.body.c_str(), via);
    label(buf, y0 + 62, 16, DIM, 230);
    label("Press to open", y0 + 88, 15, INT.color);
  }
}

static void drawMenu() {
  int n = modes.size(), i = menuIdx;
  bool home = !strcmp(modes[i].id, "idle");
  fill(BLACK);
  float seg = 360.0f / n;
  for (int k = 0; k < n; k++) {
    float a = -90 + k * seg;
    arc(186, 12, a + 2, a + seg - 2, k == i ? modes[k].color : FAINT);
  }
  label("Choose mode", C - 62, 18, DIM);
  label(modes[i].name, C, 44, WHITE, 320);
  label(home ? "Clock, follow the computer" : "Pin this mode", C + 56, 18, modes[i].color, 320);
  // Thin inner ring fills up to the automatic pick; any turn starts it over.
  float f = fminf(1, (nowMs() - menuAt) / (float)MENU_AUTO_MS);
  if (f > 0) arc(166, 3, -90, -90 + 360 * f, modes[i].color, 0.6f);
}

// ---------------------------------------------------------------- startup animation

// "Segments" from the simulator (BOOT_DRAW.segments; spec in puck-ui/README.md): a color wheel sweeps
// in clockwise from 12 o'clock while spinning 3/4 turn to a stop, its hues settle into the mode colors
// and gaps open into the hold menu's segments, "Smart Puck" fades in, then it all fades out to Home.
// Any turn or press ends it at once, and that input does nothing else.
constexpr uint32_t BOOT_MS = 2600, BOOT_SWALLOW_MS = 400;
static bool booting = false;
static uint32_t bootAt = 0, bootSkipAt = 0;

static float easeOut(float x) { return 1 - powf(1 - x, 3); }
static float easeInOut(float x) { return x < 0.5f ? 4 * x * x * x : 1 - powf(-2 * x + 2, 3) / 2; }
static float spanOf(float p, float a, float b) { return clampf((p - a) / (b - a), 0, 1); }

// The wheel's color at `hue` degrees (red at 12 o'clock), HSL with S 85 % and L 58 % like the sim.
static uint32_t wheelColor(float hue) {
  const float sat = 0.85f, lit = 0.58f;
  auto f = [&](float k) {
    float q = fmodf(k + hue / 30, 12);
    if (q < 0) q += 12;
    float v = lit - sat * fminf(lit, 1 - lit) * fmaxf(-1, fminf(fminf(q - 3, 9 - q), 1));
    return (uint32_t)lroundf(clampf(v, 0, 1) * 255);
  };
  return f(0) << 16 | f(8) << 8 | f(4);
}

void startBoot() {
  booting = true;
  bootAt = nowMs();
  bootSkipAt = 0;
}

static void endBoot(bool skipped) {
  booting = false;
  if (skipped) bootSkipAt = nowMs();
  leds::release();
  if (!sendFn) return;
  JsonDocument doc;
  doc["evt"] = "boot";
  doc["animation"] = "segments";
  if (skipped) doc["skipped"] = true;
  sendFn(doc);
}

bool bootRunning() {
  if (booting && nowMs() - bootAt >= BOOT_MS) endBoot(false);
  return booting;
}

// Knob input while the animation plays ends it; the rest of that turn or press is dropped.
static bool bootEatsInput() {
  if (booting) {
    endBoot(true);
    return true;
  }
  return recent(bootSkipAt, BOOT_SWALLOW_MS);
}

static void drawBoot() {
  float p = (nowMs() - bootAt) / (float)BOOT_MS;
  if (p >= 1) return;
  float fade = 1 - easeOut(spanOf(p, 0.8f, 1));
  rect(0, 0, W, W, BLACK, fade);  // Home shows through as it fades
  uint32_t pal[32];
  int n = 0;
  for (auto &md : modes)
    if (strcmp(md.id, "idle") && n < 32) pal[n++] = md.color;
  if (!n) pal[n++] = ACCENT;
  float seg = 360.0f / n;
  float drawn = 360 * easeOut(spanOf(p, 0, 0.3f));        // sweeps in clockwise from 12 o'clock
  float rot = -270 * (1 - easeOut(spanOf(p, 0, 0.6f)));    // spins 3/4 turn, slowing to a stop
  float m = easeInOut(spanOf(p, 0.32f, 0.6f));             // 0 = rainbow wheel, 1 = mode segments
  float gap = 4 * m;
  const int SL = 6;  // slices per segment
  for (int k = 0; k < n; k++) {
    float a0 = k * seg + gap / 2, a1 = (k + 1) * seg - gap / 2;
    for (int j = 0; j < SL; j++) {
      float s0 = a0 + (a1 - a0) * j / SL, s1 = a0 + (a1 - a0) * (j + 1) / SL;
      if (s0 >= drawn) continue;
      float e = fminf(s1, drawn);
      uint32_t c = mix(wheelColor((s0 + s1) / 2), pal[k], m);
      float join = j < SL - 1 && e == s1 ? 1 : 0;  // overlap the next slice so no seam shows
      arc(186, 12, -90 + rot + s0, -90 + rot + e + join, c, fade, false);
    }
  }
  float w = easeOut(spanOf(p, 0.45f, 0.68f));
  if (w > 0) txt("Smart Puck", C, C, 35, mix(BLACK, WHITE, w * fade), 8 * (1 - w));
  // The LEDs run the hue under 12 o'clock while it's a wheel, then the first mode color.
  leds::hold(m < 0.5f ? wheelColor(fmodf(fmodf(-rot, 360) + 360, 360)) : pal[0]);
}

// ---------------------------------------------------------------- input

static void setMode(int idx) {
  if (idx < 0) return;
  current = idx;
}

void rotate(int d) {
  if (bootEatsInput()) return;
  lastInput = nowMs();
  if (!menuOpen && modes[current].tpl == SETTINGS) return settingsRotate(stateOf(current), d);
  if (menuOpen) {
    int n = modes.size();
    menuIdx = ((menuIdx + d) % n + n) % n;
    menuAt = nowMs();
    return;
  }
  if (INT.active && INT.sticky) return dismissInterrupt();
  Mode &m = modes[current];
  State &s = stateOf(current);
  if (m.tpl == SIDEKICK) return sidekickRotate(current, s, d);
  if (m.tpl == IDLE) {
    // Turning on Home means you want to do something: it opens the mode menu and that same turn
    // already moves through it.
    hold();
    int n = modes.size();
    menuIdx = ((menuIdx + d) % n + n) % n;
    return;
  }
  switch (m.tpl) {
    case CALLS:
    case MEDIA:
      s.vol = clampf(s.vol + d * (m.tpl == MEDIA && s.spAt ? 5 : 2), 0, 100), s.touched = nowMs();
      break;
    case TYPING:
      s.pos += d;
      if (s.layer == 0) s.line = max(1, s.line + d * 3);
      if (s.layer == 1) s.word = clampf(s.word + d, 0, TYPING_WORD_COUNT - 1);
      if (s.layer == 2) s.hist = clampf(s.hist - d, 0, 20);
      break;
    case EDITING: {
      static const int jog[] = {1, 24, 72};
      s.frame = max(0, s.frame + d * jog[s.layer % 3]);
      break;
    }
    case VALUE: s.v = clampf(s.v + d * m.step, m.min, m.max), s.touched = nowMs(); break;
    case LIST: {
      int n = m.items.empty() ? 1 : m.items.size();
      s.i = ((s.i + d) % n + n) % n;
      break;
    }
    case TIMER:
      if (!s.running) s.min = clampf(s.min + d, 1, 120), s.left = s.min * 60;
      break;
    case ARM: {
      int k = armJoint(m, s.layer);
      if (k < 0) break;
      int step = k == GRIP ? 5 : m.step;
      s.arm[k] = clampf(s.arm[k] + d * step, JOINTS[k].min, JOINTS[k].max), s.touched = nowMs();
      break;
    }
    case CLOCKFACE: s.face = ((s.face + d) % FACE_COUNT + FACE_COUNT) % FACE_COUNT, s.touched = nowMs(); break;
    case PRINTER: s.page = ((s.page + d) % 3 + 3) % 3; break;
  }
  Layer &ly = m.layers[s.layer];
  emit(current, d > 0 ? ly.right : ly.left, d > 0 ? "turn_right" : "turn_left");
}

void press() {
  if (bootEatsInput()) return;
  lastInput = nowMs();
  if (menuOpen) {
    menuOpen = false;
    setMode(menuIdx);
    autoMode = !strcmp(modes[current].id, "idle");

    sendPin(autoMode ? nullptr : modes[current].id);
    return;
  }
  if (INT.active) return openInterrupt();
  Mode &m = modes[current];
  State &s = stateOf(current);
  if (m.tpl == SETTINGS) return settingsPress(m, s);
  if (m.tpl == IDLE) return;  // Home's press does nothing
  if (m.tpl == SIDEKICK) {
    // Double press = next tool. Wait briefly to tell it from a single press, only when there is a choice.
    if (!s.skCall && s.skTools.size() > 1) {
      if (s.skPendingAt && nowMs() - s.skPendingAt < 320) {
        s.skPendingAt = 0;
        sidekickNextTool(s);
      } else {
        s.skPendingAt = nowMs();
      }
      return;
    }
    return sidekickPressNow(current, s);
  }
  if (!strcmp(m.press.act, "puck.wifi_reset")) {
    net::reset();
  } else if (!strcmp(m.press.act, "puck.cycle")) {
    s.layer = (s.layer + 1) % m.layers.size();
  } else {
    switch (m.tpl) {
      case IDLE:
        if (m.face == FACE_SPOTIFY) {  // the Spotify face: press plays or pauses
          if (spotifyFn) spotifyFn("play_pause");
          musicPlayPause();
          return;
        }
        s.muted = !s.muted;
        break;
      case CALLS: s.muted = !s.muted; break;
      case MEDIA: s.playing = !s.playing; break;
      case VALUE: s.on = !s.on; break;
      case LIST: s.flash = nowMs(); break;
      case TIMER:
        if (s.left <= 0) s.left = s.min * 60;
        s.running = !s.running;
        break;
      case CLOCKFACE: {
        int idle = modeIndex("idle");
        if (idle >= 0) modes[idle].face = s.face;
        s.pressedAt = nowMs();
        break;
      }
      case PRINTER:
        if (!strcmp(m.press.act, "bambu.light")) s.on = !s.on;
        else if (!strcmp(m.press.act, "bambu.pause")) s.pstate = "Paused";
        else if (!strcmp(m.press.act, "bambu.resume")) s.pstate = "Printing";
        s.flash = nowMs();
        break;
      default: break;
    }
  }
  emit(current, m.press, "press");
}

void hold() {
  if (bootEatsInput()) return;
  lastInput = nowMs();
  INT.active = false;
  menuOpen = true;
  int idle = modeIndex("idle");
  menuIdx = autoMode && idle >= 0 ? idle : current;
  menuAt = nowMs();
}

// Swipe left on the Media screen for the next track, swipe right for the previous one.
bool gesture(int g) {
  if (menuOpen || modes[current].tpl != MEDIA || !spotifyFn || (g != 3 && g != 4)) return false;
  lastInput = nowMs();
  State &s = stateOf(current);
  spotifyFn(g == 3 ? "next" : "prev");
  s.skipAt = nowMs();
  s.skipNext = g == 3;
  s.skipTxt = g == 3 ? "Next track" : "Previous track";
  return true;
}

// ---------------------------------------------------------------- tick

// Called every loop: the hold menu's automatic pick and Sidekick's single press (which waits to
// see whether a second press follows). Returns true when the screen should be redrawn.
bool autoSelect() {
  if (booting) {
    bootRunning();
    return true;  // redraw every pass while it plays, and once as it ends
  }
  static uint32_t refusedCheck = 0;
  // A computer connecting over Bluetooth means you're about to use it: show Sidekick (unless a mode
  // is pinned, the menu is open or a call is on screen).
  static uint32_t bleSeen = 0;
  uint32_t bc = ble::connectCount();
  if (bc != bleSeen) {
    bleSeen = bc;
    if (autoMode && !menuOpen && !inCall())
      for (size_t k = 0; k < modes.size(); k++)
        if (modes[k].tpl == SIDEKICK) {
          setMode(k);
          return true;
        }
  }
  if (ble::refusedSince(refusedCheck)) {
    refusedCheck = nowMs();
    interrupt("pair_refused", "", "");
  }
  static uint32_t staleSeen = 0;  // once per boot: it would come back every minute otherwise
  if (!staleSeen && ble::stalePairingAt()) {
    staleSeen = ble::stalePairingAt();
    interrupt("pair_stale", "", "");
  }
  if (menuOpen && nowMs() - menuAt > MENU_AUTO_MS) {
    press();
    return true;
  }
  if (!menuOpen && modes[current].tpl == SIDEKICK) {
    State &s = stateOf(current);
    if (s.skPendingAt && nowMs() - s.skPendingAt >= 320) {
      s.skPendingAt = 0;
      sidekickPressNow(current, s);
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------- messages from the computer

void host(JsonObjectConst msg) {
  const char *t = msg["t"] | "";
  if (!strcmp(t, "time")) {
    if (msg["utc"].is<long long>()) setClockUtc((time_t)msg["utc"].as<long long>());
    else setClockLocal((time_t)(msg["epoch"] | 0LL));
  } else if (!strcmp(t, "active")) {
    // {"t":"active","host":"mac"}: a companion saw keyboard or mouse use; keys go to that computer.
    ble::hostActive(!strcasecmp(msg["host"] | "pc", "mac"));
  } else if (!strcmp(t, "mic")) {
    int idx = modeIndex("calls");
    if (idx >= 0) stateOf(idx).muted = msg["muted"] | true;
  } else if (!strcmp(t, "arm")) {
    // {"t":"arm","joints":{"shoulder_pan":0,...},"status":"armed"} from tools/arm_bridge.py
    int idx = modeIndex("arm");
    if (idx < 0) return;
    State &s = stateOf(idx);
    if (msg["status"].is<const char *>()) s.armStatus = msg["status"].as<const char *>();
    if (recent(s.touched, 1500)) return;  // the knob was just turned; don't fight it
    JsonObjectConst joints = msg["joints"];
    for (int k = 0; k < ARM_N; k++)
      if (joints[JOINTS[k].key].is<float>())
        s.arm[k] = clampf(lroundf(joints[JOINTS[k].key].as<float>()), JOINTS[k].min, JOINTS[k].max);
  } else if (!strcmp(t, "sidekick")) {
    for (size_t k = 0; k < modes.size(); k++)
      if (modes[k].tpl == SIDEKICK) {
        sidekickFromMessage(stateOf(k), msg);
        if (autoMode) setMode(k);
        break;
      }
  } else if (!strcmp(t, "nowplaying") || !strcmp(t, "spotify_status")) {
    if (!strcmp(t, "nowplaying")) {
      music.at = nowMs();
      music.state = msg["state"] | "idle";
      music.track = asciiFold(msg["track"] | "");
      music.artist = asciiFold(msg["artist"] | "");
      music.pos = (msg["progress_ms"] | 0) / 1000.0f;
      music.len = fmaxf(1, (msg["duration_ms"] | 0) / 1000.0f);
      if (msg["art"].is<long>()) music.art = msg["art"].as<long>();
    } else if (!strcmp(msg["state"] | "", "unlinked")) {
      music.at = 0;
    }
    int idx = -1;
    for (size_t k = 0; k < modes.size(); k++)
      if (modes[k].tpl == MEDIA) {
        idx = k;
        break;
      }
    if (idx < 0) return;
    State &s = stateOf(idx);
    if (!strcmp(t, "spotify_status")) {  // {"t":"spotify_status","state":"error","error":"premium"}
      if (!strcmp(msg["state"] | "", "error")) s.spErr = msg["error"] | "", s.spErrAt = nowMs();
      else if (!strcmp(msg["state"] | "", "unlinked")) s.spAt = 0, s.spErr.clear();
      else if (!strcmp(msg["state"] | "", "linked")) s.spErr.clear();
      return;
    }
    std::string newTrack = asciiFold(msg["track"] | "");
    bool playing = !strcmp(msg["state"] | "", "playing");
    if (s.spAt && playing && !newTrack.empty() && newTrack != s.track) interrupt("song", newTrack, asciiFold(msg["artist"] | ""));
    if (!s.spAt || !recent(s.spErrAt, 4000)) s.spErr.clear();  // reached Spotify: old trouble is over
    s.spAt = nowMs();
    s.spState = msg["state"] | "idle";
    s.spDevice = asciiFold(msg["device"] | "");
    s.track = asciiFold(msg["track"] | "");
    s.artist = asciiFold(msg["artist"] | "");
    s.playing = s.spState == "playing";
    s.mpos = (msg["progress_ms"] | 0) / 1000.0f;
    s.len = fmaxf(1, (msg["duration_ms"] | 0) / 1000.0f);
    s.last = nowMs();
    int vol = msg["volume"] | -1;
    if (vol >= 0 && !recent(s.touched, 2500)) s.vol = vol;  // don't fight a knob turn that is still being sent
  } else if (!strcmp(t, "printer")) {
    // {"t":"printer","state":"RUNNING","percent":37,"remaining_min":110,"nozzle":220,"bed":65,
    //  "layer":150,"total_layers":412,"file":"..."}; a print starting pulls the puck to the printer mode.
    std::string prevState = liveFresh() ? live.state : "";
    live.at = nowMs();
    live.state = msg["state"] | "";
    live.file = msg["file"] | "";
    live.percent = msg["percent"] | 0.0f, live.remainingMin = msg["remaining_min"] | 0.0f;
    live.nozzle = msg["nozzle"] | 0.0f, live.bed = msg["bed"] | 0.0f;
    live.layer = msg["layer"] | -1, live.totalLayers = msg["total_layers"] | -1;
    // State changes pop up over whatever is on screen (not the first report after boot).
    if (!prevState.empty() && prevState != live.state) {
      if (live.state == "PAUSE") interrupt("print_pause", "", "");
      else if (live.state == "FAILED") interrupt("print_failed", "", "");
      else if (live.state == "FINISH") interrupt("print_finish", "", "");
      else if (live.state == "RUNNING" && prevState != "PAUSE") interrupt("print_start", "", asciiFold(live.file.c_str()));
    }
  } else if (!strcmp(t, "ctx")) {
    const char *app = msg["app"] | "";
    // With companions on both computers, only the one in use moves the puck: the other going idle
    // ("desktop") doesn't send it Home while you're working on the first.
    static std::string activeHost = "pc";
    std::string host = msg["host"] | "pc";
    if (!strcasecmp(app, "desktop") && host != activeHost) return;
    if (strcasecmp(app, "desktop")) activeHost = host;
    if (muteKey(app)) callApp = app;
    int idx = modeIndex(msg["mode"] | "");
    for (size_t k = 0; idx < 0 && k < modes.size(); k++)
      for (const char *trig : modes[k].triggers)
        if (!strcasecmp(trig, app)) idx = k;
    for (size_t k = 0; idx < 0 && *app && strcasecmp(app, "desktop") && k < modes.size(); k++)
      for (const char *trig : modes[k].triggers)
        if (!strcmp(trig, "*")) idx = k;
    if (idx < 0) idx = modeIndex("idle");
    State &s = stateOf(idx);
    // Keys follow the computer in use: a companion that's active pulls them to its computer, and when
    // it goes idle ("desktop") they go to the other one. "host" says which computer sent this.
    bool fromMac = !strcasecmp(msg["host"] | "pc", "mac");
    ble::preferMac(strcasecmp(app, "desktop") ? fromMac : !fromMac);
    if (modes[idx].tpl == SIDEKICK) {
      std::string label = asciiFold(msg["label"] | app);
      if (!label.empty()) label[0] = toupper(label[0]);
      if (strcasecmp(s.skApp.c_str(), label.c_str()) || !s.skSet) sidekickForApp(s, app, label.c_str());
      sidekickView3d(s, msg["view3d"] | false);
      sidekickText(s, msg["text"] | false);
    }
    const char *title = msg["title"] | "";
    switch (modes[idx].tpl) {
      case CALLS:
        s.app = msg["label"] | (*app ? app : "Call");
        if (!s.app.empty()) s.app[0] = toupper(s.app[0]);
        s.start = nowMs();
        break;
      case TYPING:
      case EDITING:
        if (*title) s.file = title;
        break;
      case MEDIA:
        if (s.spAt && recent(s.spAt, 90000)) break;  // the puck's own Spotify data wins
        s.track = msg["track"] | (*title ? title : s.track.c_str());
        s.artist = msg["artist"] | s.artist.c_str();
        break;
      default: break;
    }
    if (autoMode) setMode(idx);
  }
}

// ---------------------------------------------------------------- time zone

// The simulator stores the time zone as an IANA name in Idle's settings.tz; the ESP32 needs POSIX rules.
// Same list and labels as the simulator's TZ_CHOICES.
static const char *TZ_CHOICES[][3] = {
    {"America/New_York", "Eastern", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "Mountain", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Phoenix", "Arizona", "MST7"},
    {"America/Los_Angeles", "Pacific", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Anchorage", "Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Pacific/Honolulu", "Hawaii", "HST10"},
    {"Europe/London", "London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Berlin", "Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Asia/Tokyo", "Tokyo", "JST-9"},
    {"Australia/Sydney", "Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"UTC", "UTC", "UTC0"}};
constexpr int TZ_COUNT = sizeof(TZ_CHOICES) / sizeof(TZ_CHOICES[0]);
static int tzIndex = 1;  // America/Chicago
static const char *tzPosix = TZ_CHOICES[1][2];

static int tzFind(const char *iana) {
  for (int k = 0; k < TZ_COUNT; k++)
    if (!strcmp(TZ_CHOICES[k][0], iana)) return k;
  return -1;
}

static void setTimeZoneIndex(int k, bool save);

const char *timeZone() { return tzPosix; }

// ---------------------------------------------------------------- mode lineup from the simulator

// Strings in modes loaded at runtime point into this document, so it lives as long as they do.
static std::unique_ptr<JsonDocument> modeStore;

// The arm screen always starts on a Safety layer: turn right to arm, left to disarm.
static void addSafetyLayer(Mode &m) {
  for (auto &l : m.layers)
    if (!strcasecmp(l.name, "Safety")) return;
  m.layers.insert(m.layers.begin(), {"Safety", B("robotarm.disarm"), B("robotarm.arm")});
}

static bool tplFromName(const char *name, Tpl &out) {
  static const struct { const char *name; Tpl tpl; } names[] = {
      {"idle", IDLE},   {"calls", CALLS}, {"typing", TYPING}, {"media", MEDIA}, {"editing", EDITING},
      {"value", VALUE}, {"list", LIST},   {"timer", TIMER},   {"arm", ARM},     {"clock", CLOCKFACE}, {"printer", PRINTER}, {"settings", SETTINGS}, {"sidekick", SIDEKICK}};
  for (auto &n : names)
    if (!strcmp(n.name, name)) return out = n.tpl, true;
  return false;
}

static Bind bindFrom(JsonVariantConst v) {
  const char *act = v["act"] | "puck.none";
  const char *arg = v["arg"].is<const char *>() ? v["arg"].as<const char *>() : nullptr;
  return {act, arg && *arg ? arg : nullptr};
}

bool applyModes(std::unique_ptr<JsonDocument> doc, JsonDocument &reply) {
  JsonArrayConst list = (*doc)["modes"];
  if (list.isNull() || list.size() == 0) {
    reply["evt"] = "modes_err";
    reply["error"] = "no modes";
    return false;
  }
  std::vector<Mode> next;
  reply["evt"] = "modes_ok";
  JsonArray skipped = reply["skipped"].to<JsonArray>();
  for (JsonObjectConst o : list) {
    Mode m;
    const char *id = o["id"] | "";
    if (!*id || !tplFromName(o["template"] | "", m.tpl)) {
      skipped.add(id);
      continue;
    }
    m.id = id;
    m.name = o["name"] | id;
    const char *hex = o["color"] | "#8a94a3";
    m.color = strtoul(hex[0] == '#' ? hex + 1 : hex, nullptr, 16);
    for (const char *t : o["triggers"].as<JsonArrayConst>()) m.triggers.push_back(t);
    for (JsonObjectConst l : o["layers"].as<JsonArrayConst>())
      m.layers.push_back({l["name"] | "", bindFrom(l["left"]), bindFrom(l["right"])});
    if (m.tpl == ARM) addSafetyLayer(m);
    if (m.layers.empty()) m.layers.push_back({"", B("puck.none"), B("puck.none")});
    m.press = bindFrom(o["press"]);
    JsonObjectConst st = o["settings"];
    m.min = st["min"] | m.min, m.max = st["max"] | m.max, m.step = st["step"] | m.step;
    m.value = st["value"] | m.value, m.minutes = st["minutes"] | m.minutes, m.face = st["face"] | m.face;
    m.unit = st["unit"] | m.unit, m.host = st["host"] | m.host;
    m.printer = st["printer"] | m.printer, m.file = st["file"] | m.file;
    m.hours = st["hours"] | m.hours, m.totalLayers = st["layers"] | m.totalLayers;
    m.brightness = st["brightness"] | m.brightness;
    // Faces were cut to 0 Atelier, 1 Stack, 2 Orbit (faceSet 3). Older lineups number them from the
    // original 9 (no faceSet) or from Atelier, Mono, Stack, Orbit (faceSet 2); removed faces become Atelier.
    int faceSet = st["faceSet"] | 1;
    if (m.face >= 0 && faceSet != 3) {
      static const int from9[] = {0, 0, 0, 1, 0, 0, 0, 0, 2}, from4[] = {0, 0, 1, 2};
      m.face = faceSet == 2 ? (m.face < 4 ? from4[m.face] : 0) : (m.face < 9 ? from9[m.face] : 0);
    }
    if (m.face >= FACE_COUNT) m.face = 0;
    for (const char *it : st["items"].as<JsonArrayConst>()) m.items.push_back(it);
    next.push_back(m);
  }
  bool hasIdle = false;
  for (auto &m : next) hasIdle |= !strcmp(m.id, "idle");
  for (JsonObjectConst o : list)
    if (!strcmp(o["id"] | "", "idle") && tzFind(o["settings"]["tz"] | "") >= 0)
      setTimeZoneIndex(tzFind(o["settings"]["tz"] | ""), true);
  if (!hasIdle) {
    reply.clear();
    reply["evt"] = "modes_err";
    reply["error"] = "no idle mode";
    return false;
  }

  // Keep each surviving mode's state and the current mode; otherwise fall back to Idle.
  std::string curId = modes[current].id;
  std::vector<State> nextStates(next.size());
  for (size_t k = 0; k < next.size(); k++) {
    int old = modeIndex(next[k].id);
    if (old >= 0 && modes[old].tpl == next[k].tpl) nextStates[k] = states[old];
  }
  modes.swap(next);
  states.swap(nextStates);
  modeStore = std::move(doc);  // the old document (if any) is freed only now
  int cur = modeIndex(curId.c_str());
  current = cur >= 0 ? cur : modeIndex("idle");
  if (cur < 0) autoMode = true;
  menuOpen = false;
  menuIdx = 0;

  reply["version"] = (*modeStore)["version"] | 0;
  reply["count"] = modes.size();
  return true;
}

void resetModes() {
  modes.clear();
  buildModes();
  states.assign(modes.size(), State());
  modeStore.reset();
  current = modeIndex("idle");
  autoMode = true;
  menuOpen = false;
  menuIdx = 0;
}

// ---------------------------------------------------------------- settings mode

enum SettingsEdit { SE_NONE, SE_WIFI_CONFIRM, SE_WIFI_SETUP, SE_TZ, SE_BRIGHTNESS, SE_BT, SE_BT_PAIR, SE_BT_FORGET };
static const char *SETTINGS_ROWS[] = {"wifi", "bluetooth", "glow", "brightness", "tz", "about"};
constexpr int SETTINGS_ROW_COUNT = 6;
static bool glowOn = true;  // the RGB lights follow the screen color
static void (*backlightFn)(int) = nullptr;
static const char *fwVersion = "";
static uint32_t settingsAt = 0;

static void savePref(const char *key, const char *str, int num) {
  Preferences p;
  p.begin("puck", false);
  if (str) p.putString(key, str);
  else p.putInt(key, num);
  p.end();
}

static void setTimeZoneIndex(int k, bool save) {
  tzIndex = k;
  tzPosix = TZ_CHOICES[k][2];
  net::setTimeZone(tzPosix);
  if (save) savePref("tz", TZ_CHOICES[k][0], 0);
}

static void setBrightness(int pct, bool save) {
  if (backlightFn) backlightFn(pct);
  if (save) savePref("bright", nullptr, pct);
}

static void sendSetting(const char *key, const char *str, int num) {
  if (!sendFn) return;
  JsonDocument doc;
  doc["evt"] = "setting";
  doc["key"] = key;
  if (str) doc["value"] = str;
  else doc["value"] = num;
  sendFn(doc);
}

static void settingsRotate(State &s, int d) {
  if (s.edit == SE_BT) {
    int n = ble::connectedCount() + 3;  // one "Send to" per connected computer, then pair / forget / back
    s.btIdx = (s.btIdx + d + n) % n;
  } else if (s.edit == SE_BT_FORGET) s.edit = SE_BT;
  else if (s.edit == SE_TZ) s.tzIdx = (s.tzIdx + d + TZ_COUNT) % TZ_COUNT;
  else if (s.edit == SE_BRIGHTNESS) s.bright = clampf(s.bright + d * 5, 5, 100), setBrightness(s.bright, false);
  else if (s.edit == SE_WIFI_CONFIRM) s.edit = SE_NONE;
  else if (s.edit == SE_NONE) s.row = (s.row + d + SETTINGS_ROW_COUNT) % SETTINGS_ROW_COUNT;
}

static void settingsPress(Mode &m, State &s) {
  const char *row = SETTINGS_ROWS[s.row];
  switch (s.edit) {
    case SE_WIFI_SETUP: break;  // stays until the puck is back on Wi-Fi
    case SE_BT: {
      int c = ble::connectedCount();
      if (s.btIdx < c) {
        ble::setTarget(s.btIdx);
        s.edit = SE_NONE;
      } else if (s.btIdx == c) {  // pair a new computer
        ble::startPairing();
        s.btPaired = c;
        s.edit = SE_BT_PAIR;
      } else if (s.btIdx == c + 1) {
        s.edit = SE_BT_FORGET;
      } else {
        s.edit = SE_NONE;
      }
      break;
    }
    case SE_BT_PAIR: s.edit = SE_NONE; break;  // cancel
    case SE_BT_FORGET:
      ble::forgetAll();
      s.btPaired = 0;
      s.edit = SE_BT_PAIR;
      break;
    case SE_WIFI_CONFIRM:
      s.edit = SE_WIFI_SETUP;
      net::reset();
      break;
    case SE_TZ:
      setTimeZoneIndex(s.tzIdx, true);
      s.edit = SE_NONE;
      sendSetting("tz", TZ_CHOICES[s.tzIdx][0], 0);
      break;
    case SE_BRIGHTNESS:
      m.brightness = s.bright;
      setBrightness(s.bright, true);
      s.edit = SE_NONE;
      sendSetting("brightness", nullptr, s.bright);
      break;
    default:
      if (!strcmp(row, "wifi")) s.edit = SE_WIFI_CONFIRM, settingsAt = nowMs();
      else if (!strcmp(row, "tz")) s.edit = SE_TZ, s.tzIdx = tzIndex;
      else if (!strcmp(row, "brightness")) s.edit = SE_BRIGHTNESS;
      else if (!strcmp(row, "bluetooth")) s.edit = SE_BT, s.btIdx = 0;
      else if (!strcmp(row, "glow")) {  // a plain toggle: takes effect and saves right away
        glowOn = !glowOn;
        leds::setEnabled(glowOn);
        savePref("glow", nullptr, glowOn);
        sendSetting("glow", nullptr, glowOn);
      }
  }
}

// Clock time in another zone, for the time zone picker.
static void timeIn(int k, char *out, size_t n) {
  time_t now = time(nullptr);
  struct tm tm;
  setenv("TZ", TZ_CHOICES[k][2], 1);
  tzset();
  localtime_r(&now, &tm);
  setenv("TZ", tzPosix, 1);
  tzset();
  snprintf(out, n, "%d:%02d %s", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
}

static void drawSettings(Mode &m, State &s) {
  JsonDocument w;
  net::describe(w);
  const char *wifi = w["wifi"] | "offline";
  bool connected = !strcmp(wifi, "connected"), setup = !strcmp(wifi, "setup");
  uint32_t wcol = connected ? GREEN : setup ? AMBER : RED;
  char buf[48];
  if (s.edit == SE_WIFI_CONFIRM && nowMs() - settingsAt > 6000) s.edit = SE_NONE;
  if (s.edit == SE_WIFI_SETUP && connected) s.edit = SE_NONE;
  if (s.edit == SE_WIFI_SETUP) {
    arc(186, 10, 0, 360, AMBER, 0.5f);
    label("Wi-Fi setup", 100, 22, AMBER);
    label("On your phone, join", C - 36, 20, DIM);
    label(net::SETUP_AP, C + 2, 28, WHITE, 300);
    label("then open 192.168.4.1", C + 42, 20, WHITE, 300);
    return;
  }
  if (s.edit == SE_WIFI_CONFIRM) {
    arc(186, 10, 0, 360, AMBER);
    label("Set up Wi-Fi again?", C - 40, 26, WHITE, 300);
    snprintf(buf, sizeof buf, "Forgets %s", (const char *)(w["ssid"] | "this network"));
    label(buf, C + 4, 20, DIM, 280);
    label("Press to confirm", C + 64, 21, AMBER);
    label("Turn to cancel", C + 96, 19, DIM);
    return;
  }
  if (s.edit == SE_BT_PAIR) {
    // Pairing window open: leave when a computer joined or the time ran out.
    if (ble::connectedCount() > s.btPaired) {
      arc(186, 10, 0, 360, GREEN);
      label("Paired", C - 20, 34, GREEN);
      snprintf(buf, sizeof buf, "%s connected", ble::connectedName(ble::connectedCount() - 1));
      label(buf, C + 28, 20, WHITE, 300);
      label("Press to continue", C + 100, 19, DIM);
      return;
    }
    if (!ble::pairing()) s.edit = SE_NONE;
    else {
      arc(186, 10, 0, 360, AMBER, 0.5f);
      label("Pairing", 100, 22, AMBER);
      label("On your PC or Mac, open", C - 40, 19, DIM, 320);
      label("Bluetooth settings and", C - 14, 19, DIM, 320);
      label("choose Smart Puck", C + 14, 24, WHITE, 320);
      snprintf(buf, sizeof buf, "%ds left, press to cancel", ble::pairingSecondsLeft());
      label(buf, C + 104, 17, DIM, 300);
      return;
    }
  }
  if (s.edit == SE_BT || s.edit == SE_BT_FORGET) {
    int c = ble::connectedCount();
    arc(186, 8, 0, 360, m.color);
    label("Bluetooth", 100, 20, DIM);
    if (s.edit == SE_BT_FORGET) {
      label("Forget all computers?", C - 30, 24, WHITE, 320);
      label("You'll need to pair again", C + 10, 18, DIM, 320);
      label("Press to confirm", C + 64, 21, AMBER);
      label("Turn to cancel", C + 96, 19, DIM);
      return;
    }
    if (s.btIdx < c) {
      snprintf(buf, sizeof buf, "Send to %s", ble::connectedName(s.btIdx));
      label(buf, C - 6, 32, WHITE, 320);
      label(ble::connectedIsTarget(s.btIdx) ? "Receiving keys now" : "Press to switch", C + 40, 18, ble::connectedIsTarget(s.btIdx) ? GREEN : DIM, 320);
    } else if (s.btIdx == c) {
      label("Pair a new computer", C - 6, 28, WHITE, 320);
      label("Press to start", C + 40, 18, DIM);
    } else if (s.btIdx == c + 1) {
      label("Forget all computers", C - 6, 28, WHITE, 320);
      label("Press to choose", C + 40, 18, DIM);
    } else {
      label("Back", C - 6, 36, WHITE);
    }
    dots(c + 3, s.btIdx, C + 130);
    return;
  }
  label("Settings", 92, 20, DIM);
  const char *row = SETTINGS_ROWS[s.row];
  if (!strcmp(row, "wifi")) {
    arc(186, 8, 0, 360, wcol, 0.35f);
    label("Wi-Fi", C - 52, 22, DIM);
    label(connected ? (const char *)(w["ssid"] | "") : setup ? "Setting up" : "Not connected", C - 12, 34, WHITE, 300);
    int rssi = w["rssi"] | -100;
    int bars = !connected ? 0 : rssi >= -50 ? 4 : rssi >= -60 ? 3 : rssi >= -70 ? 2 : 1;
    for (int k = 0; k < 4; k++) rect(C - 22 + k * 12, C + 40 - (k + 1) * 7, 8, (k + 1) * 7, k < bars ? wcol : FAINT);
    if (connected) label(w["ip"] | "", C + 64, 20, DIM);
    label("Press to set up again", C + 104, 19, m.color, 260);
  } else if (!strcmp(row, "tz")) {
    bool editing = s.edit == SE_TZ;
    int k = editing ? s.tzIdx : tzIndex;
    arc(186, 8, 0, 360, editing ? m.color : FAINT);
    label("Time zone", C - 52, 22, DIM);
    label(TZ_CHOICES[k][1], C - 12, 38, WHITE);
    timeIn(k, buf, sizeof buf);
    label(buf, C + 30, 22, DIM);
    label(editing ? "Turn to choose, press to save" : "Press to change", C + 104, 19, m.color, 280);
  } else if (!strcmp(row, "brightness")) {
    bool editing = s.edit == SE_BRIGHTNESS;
    valueArc(186, 12, s.bright, editing ? m.color : DIM);
    label("Brightness", C - 52, 22, DIM);
    snprintf(buf, sizeof buf, "%d%%", s.bright);
    label(buf, C - 6, 52, WHITE);
    label(editing ? "Turn to adjust, press to save" : "Press to change", C + 104, 19, m.color, 280);
  } else if (!strcmp(row, "bluetooth")) {
    int c = ble::connectedCount();
    arc(186, 8, 0, 360, c ? GREEN : FAINT, c ? 0.35f : 1);
    label("Bluetooth", C - 52, 22, DIM);
    const char *tgt = "Not connected";
    for (int i = 0; i < c; i++)
      if (ble::connectedIsTarget(i)) tgt = ble::connectedName(i);
    label(c ? tgt : "Not connected", C - 12, 34, c ? WHITE : DIM, 300);
    if (c > 1) {
      snprintf(buf, sizeof buf, "%d computers connected", c);
      label(buf, C + 30, 18, DIM, 300);
    } else if (c == 1) label("Receiving keys", C + 30, 18, GREEN, 300);
    label("Press for options", C + 104, 19, m.color, 260);
  } else if (!strcmp(row, "glow")) {
    arc(186, 8, 0, 360, glowOn ? m.color : FAINT);
    label("Light glow", C - 52, 22, DIM);
    label(glowOn ? "On" : "Off", C - 6, 52, glowOn ? GREEN : DIM);
    label("Lights match the screen color", C + 40, 16, DIM, 300);
    label(glowOn ? "Press to turn off" : "Press to turn on", C + 104, 19, m.color, 280);
  } else {
    arc(186, 8, 0, 360, FAINT);
    label("About", C - 52, 22, DIM);
    label("Smart Puck", C - 14, 32, WHITE);
    snprintf(buf, sizeof buf, "Firmware %s", fwVersion);
    label(buf, C + 22, 21, DIM);
    bool ntp = w["ntp"] | false;
    label(ntp ? "Clock synced online" : "Clock not synced", C + 52, 19, ntp ? GREEN : AMBER);
  }
  if (s.edit == SE_NONE) dots(SETTINGS_ROW_COUNT, s.row, C + 150);
}

void setBacklight(void (*fn)(int pct)) { backlightFn = fn; }
void setSpotifyControl(void (*fn)(const char *action)) { spotifyFn = fn; }
void setArmControl(void (*move)(const char *joint, float value), void (*setArmed)(bool armed), bool (*linked)()) {
  armMoveFn = move, armSetFn = setArmed, armLinkedFn = linked;
}
bool mediaOnScreen() { return !menuOpen && modes[current].tpl == MEDIA; }
void setFirmwareVersion(const char *v) { fwVersion = v; }

// Saved time zone and brightness win over the built-in defaults and the lineup at boot.
void loadPrefs() {
  Preferences p;
  p.begin("puck", true);
  String tz = p.getString("tz", "");
  int bright = p.getInt("bright", -1);
  glowOn = p.getInt("glow", 1) != 0;
  int face = p.getInt("face", -2);  // Home's face, chosen by turning on Home
  if (face >= -1 && face < FACE_COUNT)
    for (auto &m : modes)
      if (m.tpl == IDLE) m.face = face;
  p.end();
  leds::setEnabled(glowOn);
  if (tz.length() && tzFind(tz.c_str()) >= 0) setTimeZoneIndex(tzFind(tz.c_str()), false);
  for (size_t k = 0; k < modes.size(); k++)
    if (modes[k].tpl == SETTINGS) {
      if (bright > 0) modes[k].brightness = bright;
      bright = modes[k].brightness;
      stateOf(k).bright = bright;
    }
  setBrightness(bright > 0 ? bright : 80, false);
}

// ---------------------------------------------------------------- drawing

void draw(lv_layer_t *layer) {
  draw::begin(layer);
  fillFaceMusic();
  fill(BLACK);
  Mode &m = modes[current];
  State &s = stateOf(current);
  switch (m.tpl) {
    case IDLE: drawIdle(m, s); break;
    case CALLS: drawCalls(m, s); break;
    case TYPING: drawTyping(m, s); break;
    case MEDIA: drawMedia(m, s); break;
    case EDITING: drawEditing(m, s); break;
    case VALUE: drawValue(m, s); break;
    case LIST: drawList(m, s); break;
    case TIMER: drawTimer(m, s); break;
    case ARM: drawArm(m, s); break;
    case CLOCKFACE: drawClockFace(m, s); break;
    case PRINTER: drawPrinter(m, s); break;
    case SIDEKICK: drawSidekick(m, s); break;
    case SETTINGS: drawSettings(m, s); break;
  }
  if (booting) drawBoot();
  else if (menuOpen) drawMenu();
  else drawInterrupt();
}

void begin(SendFn send) {
  sendFn = send;
  buildModes();
  states.resize(modes.size());
  setenv("TZ", tzPosix, 1);
  tzset();
  clockFromBuild();
  current = modeIndex("idle");
}

}  // namespace ui
