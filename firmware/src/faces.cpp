// Watch faces from the prototype's FACES list, drawn in 412 units.
#include "faces.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "draw.h"

using namespace draw;

static void atelier(const Clock &t) {
  bg(0x22211f, 0x070707);
  for (int k = 0; k < 60; k++) k % 5 ? tick(k * 6, 184, 196, 2, 0x5d5a54) : tick(k * 6, 168, 196, 6, 0xd6b36a);
  const char *nums[] = {"12", "3", "6", "9"};
  for (int i = 0; i < 4; i++) {
    float x, y;
    polar(i * 90, 138, x, y);
    txt(nums[i], x, y, 34, 0xd6b36a);
  }
  hand(t.aH, 100, 12, 0xd6b36a, 16);
  hand(t.aM, 152, 8, 0xefe6d2, 20);
  hand(t.aS, 170, 3, 0xe5484d, 36);
  disc(C, C, 9, 0xd6b36a);
  disc(C, C, 4, 0xe5484d);
}

static void stack(const Clock &t) {
  // As big as the round screen allows: two rows, tight spacing.
  bg(0x1a1206, 0x000000);
  char hh[3];
  snprintf(hh, sizeof hh, "%02d", t.h ? t.h : 12);
  bigText(bigdigits::STACK, hh, C, C - 80, 0xf4efe6, -6);
  bigText(bigdigits::STACK, t.mm, C, C + 92, 0xffb547, -6);
  arc(198, 5, -90, -90 + 360 * t.se / 60, 0xffb547, 0.9f);
}

static void orbit(const Clock &t) {
  fill(0x000000);
  // Trails: the sun's (hour) shows how much of the year has passed, the earth's (minute) how much
  // of today, and the moon's (second) a fixed 40 degrees.
  struct { float r, a, s; uint32_t c; float f; } o[] = {
      {176, t.aH, 16, 0xffb547, t.yearF}, {124, t.aM, 12, 0x60a5fa, t.dayF}, {74, t.aS, 7, 0xf2f4f7, 40 / 360.0f}};
  for (auto &p : o) {
    arc(p.r, 1.5f, 0, 360, 0x262a31);
    if (p.f > 0) arc(p.r, 5, p.a - 90 - 360 * p.f, p.a - 90, p.c, 0.6f);
    float x, y;
    polar(p.a, p.r, x, y);
    disc(x, y, p.s * 2.4f, p.c, 0.12f);
    disc(x, y, p.s * 1.6f, p.c, 0.25f);
    disc(x, y, p.s, p.c);
  }
  char hm[8];
  snprintf(hm, sizeof hm, "%s:%s", t.hh, t.mm);
  bigText(bigdigits::ORBIT, hm, C, C + 4, 0xf2f4f7, -2);
}

FaceMusic faceMusic;

// A clock with what's playing on Spotify: the record spins at 33 rpm while a song plays and stops on
// pause, its label takes the album cover's color, and the outer ring is the song's progress.
// Nothing playing (or Spotify not linked) greys the record out. Press on Home plays or pauses (ui.cpp).
static void spotify(const Clock &t) {
  const FaceMusic &sp = faceMusic;
  static float spin = 0;
  static uint32_t last = 0;
  uint32_t ms = millis();
  if (sp.on && sp.playing && last) spin = fmodf(spin + (ms - last) * 0.2f, 360);  // 33 rpm = 200 degrees a second
  last = ms;
  fill(0x000000);
  arc(198, 6, -90, 270, 0x1b1f24);
  if (sp.on && sp.len > 0) arc(198, 6, -90, -90 + 360 * fminf(1, sp.pos / sp.len), 0x1ed760);
  char hm[8];
  snprintf(hm, sizeof hm, "%s:%s", t.hh, t.mm);
  txt(hm, C, C - 128, 46, 0xf2f4f7, -1);
  const float rx = C, ry = C - 4, rr = 84;
  disc(rx, ry, rr, 0x101215);
  for (int r = 44; r < rr - 3; r += 6) arcAt(rx, ry, r, 1.5f, 0, 360, r % 12 ? 0x1c2026 : 0x23282f);
  for (int o : {0, 180}) arcAt(rx, ry, rr - 22, 10, spin + o - 20, spin + o + 20, 0xffffff, sp.on ? 0.22f : 0.08f);
  disc(rx, ry, 36, sp.on ? sp.art : 0x3a3f46);
  disc(rx, ry, 5, 0x000000);
  disc(rx + 24 * cosf(spin * (float)M_PI / 180), ry + 24 * sinf(spin * (float)M_PI / 180), 3, 0x000000, 0.45f);
  if (sp.on && !sp.playing) {
    disc(rx, ry, rr, 0x000000, 0.55f);
    rect(rx - 14, ry - 18, 10, 36, 0xf2f4f7);
    rect(rx + 4, ry - 18, 10, 36, 0xf2f4f7);
  }
  char line[96];
  if (!sp.on) snprintf(line, sizeof line, "Spotify");
  else if (sp.playing) snprintf(line, sizeof line, "%s", sp.artist);
  else snprintf(line, sizeof line, "Paused - %s", sp.artist);
  label(sp.on ? sp.track : "Nothing playing", C + 110, 26, sp.on ? 0xf2f4f7 : DIM, 250);
  label(line, C + 142, 18, !sp.on ? 0x1ed760 : sp.playing ? DIM : AMBER, 230);
}

// 0 Atelier, 1 Stack, 2 Orbit, 3 Spotify (lineups mark this numbering with settings.faceSet = 3).
const Face FACES[] = {{"Atelier", atelier}, {"Stack", stack}, {"Orbit", orbit}, {"Spotify", spotify}};
const int FACE_COUNT = sizeof(FACES) / sizeof(FACES[0]);
