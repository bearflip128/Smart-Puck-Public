#pragma once
#include <stdint.h>

// The current time, broken out the way the prototype's clockNow() does.
struct Clock {
  int h24, h, mi, date;
  float se;          // seconds with fraction
  float aS, aM, aH;  // hand angles in degrees, 0 = 12 o'clock
  float yearF, dayF;  // fraction of the year and of today that has passed
  char hh[3], mm[3], day[4];
};

struct Face {
  const char *name;
  void (*draw)(const Clock &t);
};

// What's playing, for the Spotify face. ui.cpp fills it from the puck's own Spotify connection.
struct FaceMusic {
  bool on = false, playing = false;  // on: Spotify has a song (playing or paused)
  const char *track = "", *artist = "";
  float pos = 0, len = 1;            // seconds
  uint32_t art = 0x1ed760;           // average color of the album cover
};
extern FaceMusic faceMusic;
constexpr int FACE_SPOTIFY = 3;

extern const Face FACES[];
extern const int FACE_COUNT;
