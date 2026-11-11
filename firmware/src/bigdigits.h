// Large clock digits (0-9 and ':') as alpha bitmaps; see tools/gen_bigdigits.py.
#pragma once
#include <stdint.h>

namespace bigdigits {

struct Glyph {
  char ch;
  const uint8_t *data;  // w*h alpha bytes
  int16_t w, h;
  int16_t left, top;  // bitmap offset from the pen position on the baseline
  int16_t advance;
};

struct Font {
  const Glyph *glyphs;
  int count;
  int px;           // em size in panel pixels
  int middleShift;  // baseline sits this far below the text's vertical middle
};

extern const Font STACK;  // 176 prototype units
extern const Font ORBIT;  // 84 prototype units

}  // namespace bigdigits
