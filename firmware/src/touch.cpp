#include "touch.h"

#include <Arduino.h>
#include <Wire.h>

namespace touch {

constexpr int PIN_SDA = 6, PIN_SCL = 7, PIN_INT = 5, PIN_RST = 13;
constexpr uint8_t ADDR = 0x15;

static bool present = false;
static int lastGesture = 0;
static uint32_t lastPoll = 0;

static bool readRegs(uint8_t reg, uint8_t *buf, size_t n) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)ADDR, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

void begin() {
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(10);
  digitalWrite(PIN_RST, HIGH);
  delay(60);
  pinMode(PIN_INT, INPUT);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  Wire.setTimeOut(20);
  uint8_t id;
  present = readRegs(0xA7, &id, 1);  // chip ID
  if (present) {
    Wire.beginTransmission(ADDR);  // keep reporting instead of falling asleep after a while
    Wire.write(0xFE);
    Wire.write(0x01);
    Wire.endTransmission();
  }
}

int poll() {
  if (!present || millis() - lastPoll < 20) return 0;
  lastPoll = millis();
  uint8_t d[6];
  if (!readRegs(0x01, d, 6)) return 0;
  int g = d[0];
  if (g == lastGesture) return 0;  // report each gesture once
  lastGesture = g;
  return g >= SWIPE_UP && g <= SWIPE_RIGHT ? g : (g == 0x05 ? TAP : 0);
}

const char *status() { return present ? "ok" : "not found"; }

}  // namespace touch
