#include "ble.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>

#include "evlog.h"

#include <set>
#include <string>
#include <vector>

namespace ble {

// ---------------------------------------------------------------- HID report descriptor

constexpr uint8_t REPORT_KEYBOARD = 1, REPORT_MEDIA = 2, REPORT_MOUSE = 3;

// Keyboard (8-byte report), media keys (7 bits used), and a mouse with a wheel (for scrolling).
static const uint8_t HID_MAP[] = {
    // Keyboard
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, REPORT_KEYBOARD,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
    // Media keys: next, previous, stop, play/pause, mute, volume up, volume down
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, REPORT_MEDIA,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x07,
    0x09, 0xB5, 0x09, 0xB6, 0x09, 0xB7, 0x09, 0xCD, 0x09, 0xE2, 0x09, 0xE9, 0x09, 0xEA,
    0x81, 0x02, 0x95, 0x09, 0x81, 0x03,
    0xC0,
    // Mouse: 3 buttons, X, Y, wheel
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, REPORT_MOUSE, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0};

constexpr uint8_t MOD_CTRL = 0x01, MOD_SHIFT = 0x02, MOD_ALT = 0x04, MOD_GUI = 0x08;
constexpr uint8_t MEDIA_NEXT = 1 << 0, MEDIA_PREV = 1 << 1, MEDIA_PLAY = 1 << 3, MEDIA_MUTE = 1 << 4, MEDIA_VOLUP = 1 << 5,
                  MEDIA_VOLDOWN = 1 << 6;

// ---------------------------------------------------------------- state

struct Peer {
  uint16_t handle;
  std::string id;  // identity address once known, otherwise the address it connected with
  bool authenticated = false;
  uint32_t at = 0;      // when it connected
  bool warned = false;  // logged that it never encrypted
};

static NimBLEServer *server = nullptr;
static NimBLEHIDDevice *hid = nullptr;
static NimBLECharacteristic *chrKeyboard = nullptr, *chrMedia = nullptr, *chrMouse = nullptr;
static std::vector<Peer> peers;
static std::set<std::string> known;  // identity addresses that may connect (paired through the pairing window)
static std::string targetId;         // the computer that receives the keys
static uint32_t pairingUntil = 0;
static volatile uint32_t refusedAt = 0;  // when a computer last tried to pair outside the window
static volatile uint32_t connects = 0;   // paired computers that have connected since boot
static uint32_t stalePairAt = 0;         // when a computer last connected with a pairing we don't have
static volatile uint32_t activeAt = 0;   // when a companion last reported keyboard or mouse use
static volatile bool activeMac = false;  // ...and whether that was the Mac
static Preferences prefs;
static char nameBuf[32];

// A key release (or media release) to send shortly after a press.
static uint32_t releaseAt = 0;
static std::vector<std::pair<NimBLECharacteristic *, std::vector<uint8_t>>> releases;

static std::string idOf(const NimBLEAddress &a) { return a.toString(); }

// ---------------------------------------------------------------- names and bonds

int bondCount() { return NimBLEDevice::getNumBonds(); }

static int bondIndex(const std::string &id) {
  for (int i = 0; i < NimBLEDevice::getNumBonds(); i++)
    if (idOf(NimBLEDevice::getBondedAddress(i)) == id) return i;
  return -1;
}

// Saved per computer, keyed by its address (pairing order changes when one is forgotten):
// "n<addr>" = the name you gave it on the web page, "h<addr>" = the name the computer reports.
static String keyFor(char kind, const std::string &id) {
  String k(kind);
  for (char c : id)
    if (c != ':') k += c;
  return k.substring(0, 15);  // NVS keys are at most 15 characters
}

static String nameOf(const std::string &id, int fallbackIndex) {
  String v = prefs.getString(keyFor('n', id).c_str(), "");
  if (!v.length()) v = prefs.getString(keyFor('h', id).c_str(), "");
  if (!v.length()) v = "Device " + String(fallbackIndex + 1);
  return v;
}

const char *bondName(int i) {
  std::string id = idOf(NimBLEDevice::getBondedAddress(i));
  snprintf(nameBuf, sizeof nameBuf, "%s", nameOf(id, i).c_str());
  return nameBuf;
}

void setBondName(int i, const char *name) {
  std::string id = idOf(NimBLEDevice::getBondedAddress(i));
  String v = name;
  v.trim();
  if (v.length()) prefs.putString(keyFor('n', id).c_str(), v.substring(0, 24));
  else prefs.remove(keyFor('n', id).c_str());
}

// A Mac gets Cmd where a PC gets Ctrl. Decided from the name: the one you gave it, or else the
// name the computer itself reports (a Mac reports e.g. "Alex's MacBook Pro").
static bool isMac(const std::string &id) {
  if (prefs.getBool(keyFor('m', id).c_str(), false)) return true;  // it offered Apple's Continuity service
  String n = nameOf(id, 0);
  n.toLowerCase();
  return n.indexOf("mac") >= 0 || n.indexOf("apple") >= 0 || n.indexOf("imac") >= 0;
}

static void detectMac(uint16_t conn, const std::string &id);

// Reads the computer's Device Name (GATT 0x2A00) over the connection it opened to us, then looks
// for Apple's service (one GATT procedure at a time).
static int onHostName(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg) {
  std::string *id = (std::string *)arg;
  if (error && error->status == 0 && attr) {
    char buf[40] = {};
    uint16_t len = OS_MBUF_PKTLEN(attr->om);
    if (len >= sizeof buf) len = sizeof buf - 1;
    os_mbuf_copydata(attr->om, 0, len, buf);
    if (buf[0]) prefs.putString(keyFor('h', *id).c_str(), buf);
    return 0;
  }
  detectMac(conn, *id);  // the read finished (or failed): next step
  delete id;
  return 0;
}

// Macs (and other Apple devices) offer Apple's Continuity GATT service; Windows PCs don't. That
// identifies a Mac even when its name doesn't say so (managed Macs are often named by serial number).
static int onAppleService(uint16_t conn, const struct ble_gatt_error *error, const struct ble_gatt_svc *svc, void *arg) {
  std::string *id = (std::string *)arg;
  if (error && error->status == 0 && svc) prefs.putBool(keyFor('m', *id).c_str(), true);
  if (!error || error->status != 0) delete id;  // BLE_HS_EDONE or a failure ends the search
  return 0;
}

static void detectMac(uint16_t conn, const std::string &id) {
  static const ble_uuid128_t continuity =
      BLE_UUID128_INIT(0x66, 0x43, 0xae, 0x10, 0x79, 0x48, 0xf8, 0xa5, 0x91, 0x45, 0xb4, 0xbb, 0x78, 0x1e, 0x61, 0xd0);
  std::string *arg = new std::string(id);
  if (ble_gattc_disc_svc_by_uuid(conn, &continuity.u, onAppleService, arg) != 0) delete arg;
}

static void readHostName(uint16_t conn, const std::string &id) {
  static ble_uuid16_t deviceName = BLE_UUID16_INIT(0x2A00);
  std::string *arg = new std::string(id);
  if (ble_gattc_read_by_uuid(conn, 1, 0xFFFF, &deviceName.u, onHostName, arg) != 0) delete arg;
}

static Peer *peerById(const std::string &id) {
  for (auto &p : peers)
    if (p.id == id) return &p;
  return nullptr;
}

static Peer *target() {
  if (Peer *p = peerById(targetId)) return p;
  for (auto &p : peers)
    if (p.authenticated) return &p;
  return nullptr;
}

// ---------------------------------------------------------------- connection callbacks

static void startAdvertising() {
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  if (!adv->isAdvertising()) adv->start();
}

static void pruneDisconnected() {
  std::vector<uint16_t> live = server->getPeerDevices();
  for (size_t i = 0; i < peers.size();) {
    bool found = false;
    for (uint16_t h : live)
      if (h == peers[i].handle) found = true;
    if (found) i++;
    else peers.erase(peers.begin() + i);
  }
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
    Peer p;
    p.handle = desc->conn_handle;
    p.id = idOf(NimBLEAddress(desc->peer_id_addr));
    p.at = millis();
    peers.push_back(p);
    // Ask for a quick connection interval so key presses feel instant.
    // A relaxed interval (30-50 ms) leaves the shared radio time for Wi-Fi; keys still feel instant.
    s->updateConnParams(desc->conn_handle, 24, 40, 4, 400);
    evlog::add("bt connect %s handle=%d", p.id.c_str(), desc->conn_handle);
  }

  void onDisconnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
    std::string id = idOf(NimBLEAddress(desc->peer_id_addr));
    evlog::add("bt disconnect %s (%s)", nameOf(id, 0).c_str(), id.c_str());
    pruneDisconnected();
  }

  void onAuthenticationComplete(ble_gap_conn_desc *desc) override {
    std::string id = idOf(NimBLEAddress(desc->peer_id_addr));
    bool wasKnown = known.count(id) > 0;
    bool open = (int32_t)(pairingUntil - millis()) > 0;
    if (!desc->sec_state.encrypted && wasKnown) {
      // A paired computer whose reconnect didn't encrypt (sleep, wake, a busy radio). Keep its pairing:
      // deleting it here is what used to force a re-pair. Drop the link and let it reconnect.
      evlog::add("bt %s: encryption failed on reconnect, pairing kept", nameOf(id, 0).c_str());
      server->disconnect(desc->conn_handle);
      return;
    }
    if (!desc->sec_state.encrypted || (!wasKnown && !open)) {
      // A computer that isn't paired tried to pair outside the pairing window.
      evlog::add("bt refused %s: %s", id.c_str(), desc->sec_state.encrypted ? "pairing window closed" : "not encrypted");
      NimBLEDevice::deleteBond(NimBLEAddress(desc->peer_id_addr));
      server->disconnect(desc->conn_handle);
      refusedAt = millis();
      return;
    }
    if (!wasKnown) {
      known.insert(id);
      pairingUntil = 0;  // one pairing per window
      // The first computer ever paired starts as the target.
      if (targetId.empty()) targetId = id;
    }
    for (auto &p : peers)
      if (p.handle == desc->conn_handle) p.id = id, p.authenticated = true;
    // The computer that just connected (opened, woke up) is usually the one you're about to use, but
    // not when the other one is in use right now (a Mac waking in the background stole the keys).
    bool otherInUse = activeAt && millis() - activeAt < 30000 && activeMac != isMac(id);
    if (!otherInUse) targetId = id;
    evlog::add("bt ready %s (%s, %s)%s", nameOf(id, 0).c_str(), isMac(id) ? "Mac" : "PC", wasKnown ? "paired before" : "new pairing",
               otherInUse ? ", keys stay on the computer in use" : ", gets the keys");
    connects = connects + 1;
    readHostName(desc->conn_handle, id);
  }
} serverCallbacks;

// ---------------------------------------------------------------- setup

void begin() {
  prefs.begin("ble", false);
  // Names used to be saved by pairing order ("n0".."n2"), which mixed computers up once one was
  // forgotten. Drop them: each computer's own reported name is read again when it connects.
  for (int i = 0; i < 3; i++) prefs.remove(("n" + String(i)).c_str());
  NimBLEDevice::init("Smart Puck");
  NimBLEDevice::setSecurityAuth(true, false, true);  // bonding, no PIN ("just works"), secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setPower(ESP_PWR_LVL_P6);

  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);

  hid = new NimBLEHIDDevice(server);
  chrKeyboard = hid->inputReport(REPORT_KEYBOARD);
  chrMedia = hid->inputReport(REPORT_MEDIA);
  chrMouse = hid->inputReport(REPORT_MOUSE);
  hid->manufacturer()->setValue("Smart Puck");
  hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap((uint8_t *)HID_MAP, sizeof HID_MAP);
  hid->startServices();
  hid->setBatteryLevel(100);

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C1);  // keyboard
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->setScanResponse(true);
  adv->setMinInterval(800);  // 500 ms between advertisements: constant fast advertising starves Wi-Fi
  adv->setMaxInterval(1600);

  // Computers paired before (this firmware's bonds) may reconnect without opening the pairing window.
  for (int i = 0; i < NimBLEDevice::getNumBonds(); i++) known.insert(idOf(NimBLEDevice::getBondedAddress(i)));
  targetId = prefs.getString("target", "").c_str();
  if (known.empty()) startPairing();  // nothing paired yet: the first boot is open for pairing
  startAdvertising();
}

static bool suspended = false;

// Frees the radio for Wi-Fi (firmware updates): disconnects computers and stops advertising.
void suspend() {
  suspended = true;
  server->advertiseOnDisconnect(false);  // otherwise a computer reconnects straight away
  for (auto &p : peers) server->disconnect(p.handle);
  NimBLEDevice::getAdvertising()->stop();
}

void resume() {
  if (!suspended) return;
  suspended = false;
  server->advertiseOnDisconnect(true);
  startAdvertising();
}

void loop() {
  if (suspended) return;
  uint32_t now = millis();
  if (releaseAt && (int32_t)(now - releaseAt) >= 0) {
    releaseAt = 0;
    Peer *t = target();
    for (auto &r : releases)
      if (t) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(r.second.data(), r.second.size());
        if (om) ble_gattc_notify_custom(t->handle, r.first->getHandle(), om);
      }
    releases.clear();
  }
  pruneDisconnected();
  // A computer that connects but never encrypts has keys for a pairing this puck no longer has
  // (Windows then drops and retries every minute). Only pairing again on that computer fixes it.
  for (auto &p : peers)
    if (!p.authenticated && !p.warned && now - p.at > 15000) {
      p.warned = true;
      evlog::add("bt %s connected but never encrypted: remove Smart Puck on that computer and pair again", p.id.c_str());
      stalePairAt = now;
    }
  if (server->getConnectedCount() < 2) startAdvertising();
}

// ---------------------------------------------------------------- pairing and targets

void startPairing() { pairingUntil = millis() + 120000; }
bool pairing() { return (int32_t)(pairingUntil - millis()) > 0; }
int pairingSecondsLeft() { return pairing() ? (int)((pairingUntil - millis()) / 1000) : 0; }

void forgetAll() {
  for (auto &p : peers) server->disconnect(p.handle);
  NimBLEDevice::deleteAllBonds();
  known.clear();
  targetId.clear();
  prefs.clear();
  startPairing();
}

int connectedCount() {
  int n = 0;
  for (auto &p : peers)
    if (p.authenticated) n++;
  return n;
}

static Peer *connectedAt(int i) {
  for (auto &p : peers)
    if (p.authenticated && i-- == 0) return &p;
  return nullptr;
}

const char *connectedName(int i) {
  Peer *p = connectedAt(i);
  if (!p) return "Device";
  int b = bondIndex(p->id);
  snprintf(nameBuf, sizeof nameBuf, "%s", nameOf(p->id, b >= 0 ? b : i).c_str());
  return nameBuf;
}

bool connectedIsMac(int i) {
  Peer *p = connectedAt(i);
  return p && isMac(p->id);
}

uint32_t connectCount() { return connects; }
uint32_t stalePairingAt() { return stalePairAt; }

bool refusedSince(uint32_t since) { return refusedAt && (int32_t)(refusedAt - since) > 0; }

// The PC companion says whether the PC is in use. In use: keys go to the PC; away: to the Mac.
void preferMac(bool mac) {
  for (auto &p : peers)
    if (p.authenticated && isMac(p.id) == mac) {
      if (targetId != p.id) evlog::add("bt keys -> %s", nameOf(p.id, 0).c_str());
      targetId = p.id;
      return;
    }
}

void hostActive(bool mac) {
  activeAt = millis();
  activeMac = mac;
  preferMac(mac);
}

bool connectedIsTarget(int i) {
  Peer *p = connectedAt(i);
  return p && target() == p;
}

void setTarget(int i) {
  Peer *p = connectedAt(i);
  if (!p) return;
  targetId = p->id;
  prefs.putString("target", targetId.c_str());
  evlog::add("bt keys -> %s (from Settings)", nameOf(p->id, 0).c_str());
}

// ---------------------------------------------------------------- sending

static void notifyReport(NimBLECharacteristic *chr, std::vector<uint8_t> data) {
  Peer *t = target();
  if (!t) return;
  struct os_mbuf *om = ble_hs_mbuf_from_flat(data.data(), data.size());
  if (om) ble_gattc_notify_custom(t->handle, chr->getHandle(), om);
}

static void flushReleases() {
  // A new press while a release is pending: send the release first.
  if (!releases.empty()) {
    releaseAt = millis();
    loop();
  }
}

static void press(NimBLECharacteristic *chr, std::vector<uint8_t> down, std::vector<uint8_t> up) {
  flushReleases();
  notifyReport(chr, down);
  releases.push_back({chr, up});
  releaseAt = millis() + 25;
}

struct KeyName {
  const char *name;
  uint8_t code;
};
static const KeyName KEYS[] = {{"left", 0x50}, {"right", 0x4F}, {"up", 0x52}, {"down", 0x51}, {"enter", 0x28},
                               {"esc", 0x29},  {"backspace", 0x2A}, {"tab", 0x2B}, {"space", 0x2C}, {"-", 0x2D},
                               {"=", 0x2E},    {"delete", 0x4C}, {"home", 0x4A}, {"end", 0x4D}, {"pageup", 0x4B},
                               {"pagedown", 0x4E}};

static bool parseKey(const char *combo, bool mac, uint8_t &mods, uint8_t &key) {
  mods = 0;
  key = 0;
  String s = combo;
  s.toLowerCase();
  bool word = false;  // ctrl+left/right: word jump (Alt on a Mac)
  bool literalCtrl = false;  // "control": Ctrl on a Mac as well
  while (s.length()) {
    int plus = s.indexOf('+');
    // A trailing "+" or "ctrl++" can't be told apart from the separator; "=" is used for zoom in.
    String tok = plus < 0 ? s : s.substring(0, plus);
    s = plus < 0 ? "" : s.substring(plus + 1);
    if (tok == "ctrl") mods |= MOD_CTRL;
    else if (tok == "control") mods |= MOD_CTRL, literalCtrl = true;
    else if (tok == "shift") mods |= MOD_SHIFT;
    else if (tok == "alt" || tok == "option") mods |= MOD_ALT;
    else if (tok == "cmd" || tok == "gui" || tok == "meta" || tok == "win") mods |= MOD_GUI;
    else if (tok.length() == 1 && tok[0] >= 'a' && tok[0] <= 'z') key = 0x04 + (tok[0] - 'a');
    else if (tok.length() == 1 && tok[0] >= '1' && tok[0] <= '9') key = 0x1E + (tok[0] - '1');
    else if (tok == "0") key = 0x27;
    else
      for (auto &k : KEYS)
        if (tok == k.name) key = k.code;
  }
  if (!key) return false;
  if (mac && (mods & MOD_CTRL) && !literalCtrl) {
    mods &= ~MOD_CTRL;
    bool arrow = key == 0x50 || key == 0x4F;
    mods |= arrow ? MOD_ALT : MOD_GUI;  // word jump uses Option on a Mac, shortcuts use Cmd
    if (key == 0x1C && !(mods & MOD_SHIFT)) mods |= MOD_SHIFT, key = 0x1D;  // ctrl+y (redo) -> Cmd+Shift+Z
    (void)word;
  }
  return true;
}

bool send(const char *action, const char *arg) {
  Peer *t = target();
  if (!t) {
    static uint32_t loggedAt = 0;
    if (!loggedAt || millis() - loggedAt > 10000) loggedAt = millis(), evlog::add("bt dropped %s: no computer connected", action);
    return false;
  }
  if (!strcmp(action, "volume_up")) press(chrMedia, {MEDIA_VOLUP, 0}, {0, 0});
  else if (!strcmp(action, "volume_down")) press(chrMedia, {MEDIA_VOLDOWN, 0}, {0, 0});
  else if (!strcmp(action, "mute")) press(chrMedia, {MEDIA_MUTE, 0}, {0, 0});
  else if (!strcmp(action, "play_pause")) press(chrMedia, {MEDIA_PLAY, 0}, {0, 0});
  else if (!strcmp(action, "next")) press(chrMedia, {MEDIA_NEXT, 0}, {0, 0});
  else if (!strcmp(action, "prev")) press(chrMedia, {MEDIA_PREV, 0}, {0, 0});
  else if (!strcmp(action, "scroll_up")) notifyReport(chrMouse, {0, 0, 0, 1});
  else if (!strcmp(action, "scroll_down")) notifyReport(chrMouse, {0, 0, 0, (uint8_t)-1});
  else if (!strcmp(action, "orbit") && arg) {
    // "x:+60" / "y:-20": a left-button drag of that many pixels (rotates a 3D view, e.g. Bambu
    // Studio's plate), then the cursor goes back where it was so repeated turns don't walk it away.
    char axis = arg[0];
    int px = atoi(arg + 2);
    if ((axis != 'x' && axis != 'y') || !px) return false;
    flushReleases();
    notifyReport(chrMouse, {1, 0, 0, 0});  // left button down
    for (int left = px; left;) {
      int step = left > 100 ? 100 : left < -100 ? -100 : left;
      left -= step;
      int8_t v = (int8_t)step;
      notifyReport(chrMouse, axis == 'x' ? std::vector<uint8_t>{1, (uint8_t)v, 0, 0} : std::vector<uint8_t>{1, 0, (uint8_t)v, 0});
    }
    releases.push_back({chrMouse, {0, 0, 0, 0}});  // button up
    for (int left = -px; left;) {                 // and back, with the button released
      int step = left > 100 ? 100 : left < -100 ? -100 : left;
      left -= step;
      int8_t v = (int8_t)step;
      releases.push_back({chrMouse, axis == 'x' ? std::vector<uint8_t>{0, (uint8_t)v, 0, 0} : std::vector<uint8_t>{0, 0, (uint8_t)v, 0}});
    }
    releaseAt = millis() + 30;
  }
  else if (!strcmp(action, "key") && arg) {
    // "pc:alt+a|mac:cmd+shift+a" picks a combo for each kind of computer (Zoom's mute differs).
    bool mac = isMac(t->id);
    String combo = arg;
    if (combo.indexOf('|') >= 0) {
      const char *want = mac ? "mac:" : "pc:";
      String pick;
      for (String part = combo; part.length();) {
        int bar = part.indexOf('|');
        String one = bar < 0 ? part : part.substring(0, bar);
        part = bar < 0 ? "" : part.substring(bar + 1);
        if (one.startsWith(want)) pick = one.substring(strlen(want));
      }
      if (!pick.length()) return false;
      combo = pick;
    }
    uint8_t mods, key;
    if (!parseKey(combo.c_str(), mac, mods, key)) return false;
    press(chrKeyboard, {mods, 0, key, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0});
  } else {
    return false;
  }
  return true;
}

}  // namespace ble
