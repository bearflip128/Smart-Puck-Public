// Folds UTF-8 text to plain ASCII for the screen: LVGL's built-in Montserrat fonts only have
// basic Latin, so accented letters lose their accents, curly quotes and dashes become plain ones,
// and anything else (CJK, emoji) becomes "?".
#pragma once
#include <stdint.h>

#include <string>

inline std::string asciiFold(const char *s) {
  static const char *latin[] = {"A","A","A","A","A","A","AE","C","E","E","E","E","I","I","I","I","D","N","O","O","O","O","O","x","O","U","U","U","U","Y","Th","ss","a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i","d","n","o","o","o","o","o","/","o","u","u","u","u","y","th","y","A","a","A","a","A","a","C","c","C","c","C","c","C","c","D","d","D","d","E","e","E","e","E","e","E","e","E","e","G","g","G","g","G","g","G","g","H","h","?","?","I","i","I","i","I","i","I","i","I","i","IJ","ij","J","j","K","k","?","L","l","L","l","L","l","L","l","L","l","N","n","N","n","N","n","n","?","?","O","o","O","o","O","o","OE","oe","R","r","R","r","R","r","S","s","S","s","S","s","S","s","T","t","T","t","?","?","U","u","U","u","U","u","U","u","U","u","U","u","W","w","Y","y","Y","Z","z","Z","z","Z","z","s"};
  std::string out;
  const uint8_t *p = (const uint8_t *)s;
  while (*p) {
    uint32_t cp = *p++;
    if (cp >= 0x80) {  // decode one UTF-8 sequence
      int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
      cp = extra == 3 ? cp & 0x07 : extra == 2 ? cp & 0x0F : cp & 0x1F;
      for (int i = 0; i < extra && (*p & 0xC0) == 0x80; i++) cp = (cp << 6) | (*p++ & 0x3F);
    }
    const char *rep = nullptr;
    char one[2] = {0, 0};
    if (cp < 0x80) {
      one[0] = (char)cp;
      rep = one;
    } else if (cp >= 0xC0 && cp <= 0x17F) {
      rep = latin[cp - 0xC0];
    } else {
      switch (cp) {
        case 0xA0: case 0x2002: case 0x2003: case 0x2009: rep = " "; break;
        case 0x2018: case 0x2019: case 0x201A: case 0x2032: rep = "'"; break;
        case 0x201C: case 0x201D: case 0x201E: case 0x2033: rep = "\""; break;
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: rep = "-"; break;
        case 0x2026: rep = "..."; break;
        case 0xB7: case 0x2022: case 0x25CF: rep = "-"; break;
        case 0xA1: rep = "!"; break;
        case 0xBF: rep = "?"; break;
        case 0xA9: rep = "(c)"; break;
        case 0x2122: rep = "TM"; break;
        case 0x20AC: rep = "EUR"; break;
        default: rep = "?";
      }
    }
    // Collapse runs of "?" (a title in another script becomes a single mark).
    if (rep[0] == '?' && rep[1] == 0 && !out.empty() && out.back() == '?') continue;
    out += rep;
  }
  return out;
}
