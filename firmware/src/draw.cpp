#include "draw.h"

#include <math.h>
#include <string.h>

namespace draw {

static lv_layer_t *L = nullptr;
static constexpr float K = 240.0f / W;  // prototype units -> panel pixels

static inline int32_t px(float v) { return (int32_t)lroundf(v * K); }
static inline lv_opa_t opa(float a) { return (lv_opa_t)(a <= 0 ? 0 : a >= 1 ? 255 : lroundf(a * 255)); }
static inline lv_color_t col(uint32_t c) { return lv_color_hex(c); }
static inline float rad(float d) { return d * (float)M_PI / 180.0f; }

void begin(lv_layer_t *layer) { L = layer; }

uint32_t mix(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int s) {
    float x = ((a >> s) & 0xff) * (1 - t) + ((b >> s) & 0xff) * t;
    return (uint32_t)lroundf(x) << s;
  };
  return ch(16) | ch(8) | ch(0);
}

// Fonts the screens can use; text never goes below 12 px, like the prototype's 11 px floor.
static const lv_font_t *font(float size) {
  static const struct { int px; const lv_font_t *f; } fonts[] = {
      {12, &lv_font_montserrat_12}, {14, &lv_font_montserrat_14}, {16, &lv_font_montserrat_16},
      {18, &lv_font_montserrat_18}, {20, &lv_font_montserrat_20}, {22, &lv_font_montserrat_22},
      {24, &lv_font_montserrat_24}, {28, &lv_font_montserrat_28}, {32, &lv_font_montserrat_32},
      {36, &lv_font_montserrat_36}, {40, &lv_font_montserrat_40}, {48, &lv_font_montserrat_48}};
  float want = size * K;
  const lv_font_t *best = fonts[0].f;
  float bestErr = 1e9;
  for (auto &f : fonts) {
    float err = fabsf(f.px - want);
    if (err < bestErr) bestErr = err, best = f.f;
  }
  return best;
}

static void ringRect(float cx, float cy, float r, float w, uint32_t color, float alpha) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_opa = LV_OPA_TRANSP;
  d.radius = LV_RADIUS_CIRCLE;
  d.border_color = col(color);
  d.border_width = px(w) < 1 ? 1 : px(w);
  d.border_opa = opa(alpha);
  int32_t ro = px(r + w / 2);
  lv_area_t a = {px(cx) - ro, px(cy) - ro, px(cx) + ro - 1, px(cy) + ro - 1};
  lv_draw_rect(L, &d, &a);
}

float textWidth(const char *s, float size) {
  lv_point_t sz;
  lv_text_get_size(&sz, s, font(size), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return sz.x / K;
}

void arcAt(float cx, float cy, float r, float w, float a0, float a1, uint32_t color, float alpha, bool rounded) {
  float span = a1 - a0;
  if (span <= 0) return;
  if (span >= 360) return ringRect(cx, cy, r, w, color, alpha);
  int32_t s = (int32_t)lroundf(a0), e = (int32_t)lroundf(a0 + span);
  if (s == e) e = s + 1;
  s = ((s % 360) + 360) % 360;
  e = ((e % 360) + 360) % 360;
  lv_draw_arc_dsc_t d;
  lv_draw_arc_dsc_init(&d);
  d.color = col(color);
  d.opa = opa(alpha);
  d.width = px(w) < 1 ? 1 : px(w);
  d.center.x = px(cx);
  d.center.y = px(cy);
  d.radius = px(r + w / 2);
  d.start_angle = s;
  d.end_angle = e;
  d.rounded = rounded;
  lv_draw_arc(L, &d);
}

void arc(float r, float w, float a0, float a1, uint32_t color, float alpha, bool rounded) {
  arcAt(C, C, r, w, a0, a1, color, alpha, rounded);
}

void valueArc(float r, float w, float pct, uint32_t color) {
  arc(r, w, 135, 405, FAINT);
  if (pct > 0) arc(r, w, 135, 135 + 270 * pct / 100, color);
}

static void text(const char *s, float x, float y, float size, uint32_t color, float maxW, float spacing) {
  const lv_font_t *f = font(size);
  int32_t ls = px(spacing);
  char buf[96];
  strncpy(buf, s, sizeof buf - 1);
  buf[sizeof buf - 1] = 0;
  lv_point_t sz;
  lv_text_get_size(&sz, buf, f, ls, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  int32_t limit = maxW > 0 ? px(maxW) : LV_COORD_MAX;
  size_t n = strlen(buf);
  while (sz.x > limit && n > 3) {  // cut until it fits, ending in ".."
    n--;
    buf[n - 2] = '.';
    buf[n - 1] = '.';
    buf[n] = 0;
    lv_text_get_size(&sz, buf, f, ls, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  }
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.font = f;
  d.color = col(color);
  d.letter_space = ls;
  d.align = LV_TEXT_ALIGN_CENTER;
  d.text = buf;
  d.text_local = 1;  // LVGL copies the text, since buf goes away before rendering
  int32_t cx = px(x), cy = px(y), h = lv_font_get_line_height(f);
  lv_area_t a = {cx - 160, cy - h / 2, cx + 160, cy - h / 2 + h};
  lv_draw_label(L, &d, &a);
}

void label(const char *s, float y, float size, uint32_t color, float maxW) { text(s, C, y, size, color, maxW, 0); }

void txt(const char *s, float x, float y, float size, uint32_t color, float spacing) {
  text(s, x, y, size, color, 0, spacing);
}

static const bigdigits::Glyph *glyphFor(const bigdigits::Font &f, char ch) {
  for (int i = 0; i < f.count; i++)
    if (f.glyphs[i].ch == ch) return &f.glyphs[i];
  return nullptr;
}

void bigText(const bigdigits::Font &f, const char *t, float x, float y, uint32_t color, float spacing) {
  int32_t gap = px(spacing), width = 0, n = 0;
  for (const char *p = t; *p; p++)
    if (auto g = glyphFor(f, *p)) width += g->advance, n++;
  width += gap * (n > 0 ? n - 1 : 0);
  int32_t pen = px(x) - width / 2, base = px(y) + f.middleShift;
  for (const char *p = t; *p; p++) {
    auto g = glyphFor(f, *p);
    if (!g) continue;
    // LVGL keeps a pointer to the image descriptor until the frame is drawn, so it must outlive this call.
    static lv_image_dsc_t imgs[64];
    static int next = 0;
    lv_image_dsc_t &img = imgs[next++ % 64];
    img = {};
    img.header.magic = LV_IMAGE_HEADER_MAGIC;
    img.header.cf = LV_COLOR_FORMAT_A8;
    img.header.w = g->w;
    img.header.h = g->h;
    img.header.stride = g->w;
    img.data = g->data;
    img.data_size = g->w * g->h;
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = &img;
    d.recolor = col(color);
    d.recolor_opa = LV_OPA_COVER;
    lv_area_t a = {pen + g->left, base + g->top, pen + g->left + g->w - 1, base + g->top + g->h - 1};
    lv_draw_image(L, &d, &a);
    pen += g->advance + gap;
  }
}

void dots(int n, int i, float y) {
  if (n < 2) return;
  for (int k = 0; k < n; k++) disc(C + (k - (n - 1) / 2.0f) * 16, y, 4, k == i ? WHITE : FAINT);
}

void disc(float x, float y, float r, uint32_t color, float alpha) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = col(color);
  d.bg_opa = opa(alpha);
  d.radius = LV_RADIUS_CIRCLE;
  int32_t rr = px(r) < 1 ? 1 : px(r);
  lv_area_t a = {px(x) - rr, px(y) - rr, px(x) + rr - 1, px(y) + rr - 1};
  lv_draw_rect(L, &d, &a);
}

void ring(float x, float y, float r, float w, uint32_t color, uint32_t fillColor, bool filled) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = col(fillColor);
  d.bg_opa = filled ? LV_OPA_COVER : LV_OPA_TRANSP;
  d.radius = LV_RADIUS_CIRCLE;
  d.border_color = col(color);
  d.border_width = px(w) < 1 ? 1 : px(w);
  int32_t rr = px(r + w / 2);
  lv_area_t a = {px(x) - rr, px(y) - rr, px(x) + rr - 1, px(y) + rr - 1};
  lv_draw_rect(L, &d, &a);
}

void line(float x0, float y0, float x1, float y1, float w, uint32_t color, float alpha, bool round) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = col(color);
  d.opa = opa(alpha);
  d.width = px(w) < 1 ? 1 : px(w);
  d.round_start = d.round_end = round;
  d.p1.x = x0 * K;
  d.p1.y = y0 * K;
  d.p2.x = x1 * K;
  d.p2.y = y1 * K;
  lv_draw_line(L, &d);
}

void rect(float x, float y, float w, float h, uint32_t color, float alpha, float radius) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = col(color);
  d.bg_opa = opa(alpha);
  d.radius = px(radius);
  lv_area_t a = {px(x), px(y), px(x + w) - 1, px(y + h) - 1};
  lv_draw_rect(L, &d, &a);
}

void triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color) {
  lv_draw_triangle_dsc_t d;
  lv_draw_triangle_dsc_init(&d);
  d.bg_color = col(color);
  d.bg_opa = LV_OPA_COVER;
  d.p[0].x = x0 * K, d.p[0].y = y0 * K;
  d.p[1].x = x1 * K, d.p[1].y = y1 * K;
  d.p[2].x = x2 * K, d.p[2].y = y2 * K;
  lv_draw_triangle(L, &d);
}

void fill(uint32_t color) { rect(0, 0, W, W, color); }

void bg(uint32_t inner, uint32_t outer) {
  fill(outer);
  const int steps = 8;
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / steps;  // 0 at the edge, 1 at the center
    disc(C, C, C * (1 - t) + 4, mix(outer, inner, t));
  }
}

void polar(float a, float r, float &x, float &y) {
  x = C + r * cosf(rad(a - 90));
  y = C + r * sinf(rad(a - 90));
}

void hand(float a, float len, float w, uint32_t color, float tail) {
  float c = cosf(rad(a - 90)), s = sinf(rad(a - 90));
  line(C - tail * c, C - tail * s, C + len * c, C + len * s, w, color);
}

void tick(float a, float r0, float r1, float w, uint32_t color) {
  float c = cosf(rad(a - 90)), s = sinf(rad(a - 90));
  line(C + r0 * c, C + r0 * s, C + r1 * c, C + r1 * s, w, color, 1, false);
}

void mic(float x, float y, float s, uint32_t color, bool slashed) {
  float w = 22 * s, h = 40 * s;
  rect(x - w / 2, y - h, w, h, color, 1, w / 2);
  arcAt(x, y - h / 2 + 4 * s, 20 * s, 5 * s, 20, 160, color, 1, true);
  line(x, y + 12 * s, x, y + 20 * s, 5 * s, color);
  if (slashed) {
    line(x - 24 * s, y - h - 2 * s, x + 24 * s, y + 20 * s, 11 * s, BLACK);
    line(x - 24 * s, y - h - 2 * s, x + 24 * s, y + 20 * s, 5 * s, color);
  }
}

}  // namespace draw
