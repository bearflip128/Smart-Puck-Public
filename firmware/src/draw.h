// Immediate-mode drawing helpers that mirror the canvas primitives in puck-ui/puck-sim.html.
// Screens are laid out in the prototype's 412-unit space and scaled to the 240 px panel,
// so coordinates and sizes can be copied from the prototype unchanged.
#pragma once
#include <lvgl.h>
#include <stdint.h>

#include "bigdigits.h"

namespace draw {

constexpr float W = 412, C = W / 2;

// Colors from the prototype's COL table.
constexpr uint32_t WHITE = 0xf2f4f7, DIM = 0x7c8591, FAINT = 0x2a2f36, ACCENT = 0x4fb3d9,
                   RED = 0xff3b3f, GREEN = 0x39d98a, AMBER = 0xffb547, BLACK = 0x000000;

void begin(lv_layer_t *layer);

// Ring of radius r (centerline) and width w, clockwise from a0 to a1 degrees (0 = 3 o'clock).
void arc(float r, float w, float a0, float a1, uint32_t color, float alpha = 1, bool rounded = true);
void arcAt(float cx, float cy, float r, float w, float a0, float a1, uint32_t color, float alpha = 1,
           bool rounded = true);
// Width of text in prototype units at the font size label() would pick.
float textWidth(const char *text, float size);
// Background track plus a 270-degree value arc, like the prototype's valueArc.
void valueArc(float r, float w, float pct, uint32_t color);
// Centered text at height y. Text that is wider than maxW is cut with "..".
void label(const char *text, float y, float size, uint32_t color = WHITE, float maxW = 300);
// Text centered on (x, y), with optional extra letter spacing.
void txt(const char *text, float x, float y, float size, uint32_t color, float spacing = 0);
// Clock digits bigger than the built-in fonts, centered on (x, y); spacing in prototype units.
void bigText(const bigdigits::Font &f, const char *text, float x, float y, uint32_t color, float spacing = 0);
void dots(int n, int i, float y);
void disc(float x, float y, float r, uint32_t color, float alpha = 1);
void ring(float x, float y, float r, float w, uint32_t color, uint32_t fill, bool filled);
void line(float x0, float y0, float x1, float y1, float w, uint32_t color, float alpha = 1, bool round = true);
void rect(float x, float y, float w, float h, uint32_t color, float alpha = 1, float radius = 0);
void triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color);
void fill(uint32_t color);
// Radial background from inner to outer color, drawn as concentric discs.
void bg(uint32_t inner, uint32_t outer);
// Hand from the center at clock angle a (0 = 12 o'clock), with an optional tail.
void hand(float a, float len, float w, uint32_t color, float tail = 0);
void tick(float a, float r0, float r1, float w, uint32_t color);
void polar(float a, float r, float &x, float &y);
void mic(float x, float y, float s, uint32_t color, bool slashed);
uint32_t mix(uint32_t a, uint32_t b, float t);

}  // namespace draw
