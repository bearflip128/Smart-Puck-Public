#include "leds.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <math.h>

namespace leds {

constexpr int PIN = 48, COUNT = 5;
constexpr float BRIGHTNESS = 0.35f;  // five LEDs are very bright at full power
constexpr int BINS = 36;             // hue bins of 10 degrees
constexpr int STEP = 3;              // look at every 3rd pixel in both directions

static Adafruit_NeoPixel strip(COUNT, PIN, NEO_GRB + NEO_KHZ800);
static bool enabled = true;

// Histogram for the frame being drawn.
static float weight[BINS];
static float sumR[BINS], sumG[BINS], sumB[BINS];
static float anyR, anyG, anyB, anyW;  // fallback for screens with no saturated color

static float targetR = 0, targetG = 0, targetB = 0;
static float curR = 0, curG = 0, curB = 0;
static uint32_t lastUpdate = 0;
static bool held = false;

void begin() {
  strip.begin();
  strip.clear();
  strip.show();
}

void setEnabled(bool on) {
  enabled = on;
  if (!on) targetR = targetG = targetB = 0;
}

void hold(uint32_t rgb) {
  held = true;
  if (!enabled) return;
  float r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff, mx = fmaxf(r, fmaxf(g, b));
  if (mx < 1) mx = 1;
  targetR = r / mx * 255, targetG = g / mx * 255, targetB = b / mx * 255;  // full vividness, like frameDone
}

void release() { held = false; }

void sample(int x1, int y1, int w, int h, const uint16_t *px) {
  for (int y = 0; y < h; y++) {
    if ((y1 + y) % STEP) continue;
    const uint16_t *row = px + y * w;
    for (int x = 0; x < w; x += STEP) {
      uint16_t v = row[x];
      float r = ((v >> 11) & 31) / 31.0f, g = ((v >> 5) & 63) / 63.0f, b = (v & 31) / 31.0f;
      float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
      if (mx < 0.2f) continue;  // background black
      float s = (mx - mn) / mx;
      if (s < 0.3f) {  // white or gray text: only used if nothing colorful is on screen
        anyR += r * mx, anyG += g * mx, anyB += b * mx, anyW += mx;
        continue;
      }
      float hue;
      float d = mx - mn;
      if (mx == r) hue = fmodf((g - b) / d, 6.0f);
      else if (mx == g) hue = (b - r) / d + 2;
      else hue = (r - g) / d + 4;
      if (hue < 0) hue += 6;
      int bin = (int)(hue * (BINS / 6.0f)) % BINS;
      float wgt = s * mx;
      weight[bin] += wgt;
      sumR[bin] += r * wgt, sumG[bin] += g * wgt, sumB[bin] += b * wgt;
    }
  }
}

void frameDone() {
  int best = -1;
  float bestW = 0;
  for (int b = 0; b < BINS; b++) {  // a hue and its two neighbors count together, so a gradient wins as one color
    float w = weight[b] + weight[(b + 1) % BINS] + weight[(b + BINS - 1) % BINS];
    if (w > bestW) bestW = w, best = b;
  }
  float r = 0, g = 0, b = 0;
  if (best >= 0 && bestW > 8) {
    float w = 0;
    for (int k = -1; k <= 1; k++) {
      int i = (best + k + BINS) % BINS;
      r += sumR[i], g += sumG[i], b += sumB[i], w += weight[i];
    }
    r /= w, g /= w, b /= w;
  } else if (anyW > 40) {
    r = anyR / anyW, g = anyG / anyW, b = anyB / anyW;
  }
  float mx = fmaxf(r, fmaxf(g, b));
  if (held) {
    // the startup animation picks the color
  } else if (mx > 0.01f && enabled) {  // keep the hue, use full vividness; BRIGHTNESS limits the power
    targetR = r / mx * 255, targetG = g / mx * 255, targetB = b / mx * 255;
  } else {
    targetR = targetG = targetB = 0;
  }
  memset(weight, 0, sizeof weight);
  memset(sumR, 0, sizeof sumR);
  memset(sumG, 0, sizeof sumG);
  memset(sumB, 0, sizeof sumB);
  anyR = anyG = anyB = anyW = 0;
}

void update() {
  uint32_t t = millis();
  if (t - lastUpdate < 30) return;
  float k = held ? 0.35f : 0.12f;  // fade: roughly a third of a second to settle (quicker for the spinning wheel)
  lastUpdate = t;
  curR += (targetR - curR) * k;
  curG += (targetG - curG) * k;
  curB += (targetB - curB) * k;
  uint8_t r = (uint8_t)(curR * BRIGHTNESS + 0.5f), g = (uint8_t)(curG * BRIGHTNESS + 0.5f), b = (uint8_t)(curB * BRIGHTNESS + 0.5f);
  static uint32_t last = 0xFFFFFFFF;
  uint32_t packed = (r << 16) | (g << 8) | b;
  if (packed == last) return;
  last = packed;
  for (int i = 0; i < COUNT; i++) strip.setPixelColor(i, r, g, b);
  strip.show();
}

}  // namespace leds
