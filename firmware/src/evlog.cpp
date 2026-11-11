#include "evlog.h"

#include "net.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>
#include <stdarg.h>
#include <time.h>

namespace evlog {

constexpr const char *FILE_NOW = "/log.txt", *FILE_OLD = "/log.old";
constexpr size_t MAX_FILE = 24 * 1024, MAX_PENDING = 24, LINE = 200;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static char pending[MAX_PENDING][LINE];  // plain buffers: no allocation inside the critical section
static volatile size_t pendingCount = 0;
static uint32_t dropped = 0;  // lines lost because the queue was full

static const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "restart";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT: return "watchdog (interrupt)";
    case ESP_RST_TASK_WDT: return "watchdog (task)";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout (power dipped)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_EXT: return "reset pin";
    default: return "unknown";
  }
}

void add(const char *fmt, ...) {
  char msg[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  char stamp[24] = "--";
  time_t now = time(nullptr);
  if (net::timeSynced()) {  // before that the clock is the firmware's build time
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(stamp, sizeof stamp, "%m-%d %H:%M:%S", &tm);
  }
  char line[LINE];
  snprintf(line, sizeof line, "%s up=%lus rssi=%d heap=%uk %s", stamp, (unsigned long)(millis() / 1000),
           WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0, (unsigned)(ESP.getFreeHeap() / 1024), msg);
  Serial.printf("{\"evt\":\"log\",\"line\":\"%s\"}\n", line);
  portENTER_CRITICAL(&mux);
  if (pendingCount < MAX_PENDING) {
    memcpy(pending[pendingCount], line, LINE);
    pendingCount = pendingCount + 1;
  } else {
    dropped++;
  }
  portEXIT_CRITICAL(&mux);
}

void flush() {
  if (!pendingCount) return;
  static char batch[MAX_PENDING][LINE];
  portENTER_CRITICAL(&mux);
  size_t n = pendingCount;
  memcpy(batch, pending, n * LINE);
  pendingCount = 0;
  portEXIT_CRITICAL(&mux);
  File f = LittleFS.open(FILE_NOW, "a");
  if (!f) return;
  for (size_t i = 0; i < n; i++) f.println(batch[i]);
  size_t size = f.size();
  f.close();
  if (size > MAX_FILE) {
    LittleFS.remove(FILE_OLD);
    LittleFS.rename(FILE_NOW, FILE_OLD);
  }
}

void begin(const char *fwVersion) {
  add("boot fw=%s reset=%s", fwVersion, resetReason());
  flush();
}

String read() {
  String out;
  for (const char *name : {FILE_OLD, FILE_NOW}) {
    File f = LittleFS.open(name, "r");
    if (!f) continue;
    out += f.readString();
    f.close();
  }
  if (dropped) out += "(" + String(dropped) + " lines dropped)\n";
  return out;
}

String last(const char *prefix) {
  String all = read(), hit;
  int from = 0;
  while (from < (int)all.length()) {
    int end = all.indexOf('\n', from);
    if (end < 0) end = all.length();
    String line = all.substring(from, end);
    // The message starts after "heap=NNk ".
    int at = line.indexOf("k ", line.indexOf("heap="));
    if (at >= 0 && line.substring(at + 2).startsWith(prefix)) hit = line;
    from = end + 1;
  }
  return hit;
}

}  // namespace evlog
