// fishdraw for microcontrollers.
//
// A C++ port of fishdraw by Lingdong Huang (https://github.com/LingDong-/fishdraw,
// MIT License), restructured so that a whole fish can be generated and
// rasterised on a device with a few hundred kilobytes of RAM.
//
// The port keeps the original's random number stream, so a fish drawn here for
// a given name matches `node fishdraw.js --seed "<name>"` (up to float rounding).
//
// This file has no Arduino dependencies; it also builds on a desktop compiler
// (see tools/preview).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fishdraw {

// Receives the finished drawing as straight line segments in pixel coordinates.
struct LineSink {
  virtual void line(int x0, int y0, int x1, int y1) = 0;
  virtual ~LineSink() {}
};

// A rectangle in pixels.
struct Box {
  int x, y, w, h;
};

struct Stats {
  uint32_t segments;      // line segments sent to the sink
  uint32_t arena_peak;    // peak bytes used in the polyline store
  uint32_t arena_size;    // bytes allocated for the polyline store
  bool overflow;          // true if lines were dropped because the store filled up
};

// Makes a pseudo-Latin species name ("Genus species") from any text, such as a
// date. `salt` picks a different name for the same text. `out` should hold at
// least 64 bytes.
void make_name(const char* seed_text, uint32_t salt, char* out, size_t out_size);

// Generates the fish that belongs to `name` and draws it scaled to fit `box`.
// Returns false if the polyline store could not be allocated.
bool draw_fish(const char* name, const Box& box, LineSink& sink, Stats* stats = nullptr);

// Generates the flowers that belong to `name` (one to three stems of one
// made-up species) and draws them scaled to fit `box`. Not part of the
// original fishdraw; see flowerdraw.cpp.
bool draw_flower(const char* name, const Box& box, LineSink& sink, Stats* stats = nullptr);

// Single-stroke (Hershey) text, as used for the name under the original
// drawings. Supports A-Z, a-z, '.' and space. `scale` is pixels per glyph unit
// (capitals are 21 units tall); `shear` slants the text (-0.3 is italic).
float text_width(const char* text, float scale);
void draw_text(const char* text, float x, float baseline_y, float scale, float shear,
               LineSink& sink);

}  // namespace fishdraw
