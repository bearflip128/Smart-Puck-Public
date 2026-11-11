// The five RGB LEDs (WS2812 on GPIO 48) glow in the most prominent color on the screen.
//
// The display flush hands every drawn pixel block to sample(); at the end of each frame the
// dominant saturated hue becomes the target, and the LEDs fade toward it.
#pragma once
#include <stdint.h>

namespace leds {

void begin();
// Called for each block of RGB565 pixels the display is about to show (x1,y1 = top-left).
void sample(int x1, int y1, int w, int h, const uint16_t *px);
// Called after the last block of a frame.
void frameDone();
// Moves the LEDs toward the target color; call often.
void update();
void setEnabled(bool on);
// Shows `rgb` instead of the screen's color (the startup animation) until release().
void hold(uint32_t rgb);
void release();

}  // namespace leds
