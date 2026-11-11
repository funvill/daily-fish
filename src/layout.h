// Where things go on the 800x480 panel. Shared by the firmware and the
// desktop preview so both draw the same page.
#pragma once

#include "config.h"
#include "fishdraw.h"

namespace layout {

const int SCREEN_W = 800;
const int SCREEN_H = 480;

// The fish, or the flowers, are scaled to fill this box.
const fishdraw::Box FISH_BOX = {24, 14, SCREEN_W - 48, 384};

// The species name, in single-stroke italics, centred under the fish.
const float NAME_SCALE = 0.8f;   // pixels per glyph unit; capitals are 21 units tall
const float NAME_SHEAR = -0.3f;
const int NAME_BASELINE_Y = 430;

// The footer line: the fish number, bottom right.
const int FOOTER_BASELINE_Y = 470;
const int FOOTER_MARGIN_X = 12;

// Draws every line with a square pen of the given width.
struct ThickSink : fishdraw::LineSink {
  fishdraw::LineSink& inner;
  int width;
  ThickSink(fishdraw::LineSink& s, int w) : inner(s), width(w < 1 ? 1 : w) {}
  void line(int x0, int y0, int x1, int y1) override {
    for (int dy = 0; dy < width; dy++) {
      for (int dx = 0; dx < width; dx++) inner.line(x0 + dx, y0 + dy, x1 + dx, y1 + dy);
    }
  }
};

enum Subject { FISH = 0, FLOWER = 1 };

// Draws the fish or the flowers for `name`, and the name beneath.
inline bool draw_page(Subject subject, const char* name, fishdraw::LineSink& thin,
                      fishdraw::Stats* stats = nullptr) {
  ThickSink sink(thin, LINE_THICKNESS);
  bool ok = subject == FLOWER ? fishdraw::draw_flower(name, FISH_BOX, sink, stats)
                              : fishdraw::draw_fish(name, FISH_BOX, sink, stats);
  if (!ok) return false;
  char label[72];
  size_t n = 0;
  while (name[n] && n < sizeof(label) - 2) {
    label[n] = name[n];
    n++;
  }
  label[n++] = '.';
  label[n] = 0;
  float w = fishdraw::text_width(label, NAME_SCALE);
  fishdraw::draw_text(label, (SCREEN_W - w) / 2, (float)NAME_BASELINE_Y, NAME_SCALE, NAME_SHEAR, sink);
  return true;
}

}  // namespace layout
