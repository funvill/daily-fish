// Desktop preview: runs the same generator and page layout as the firmware and
// writes the 800x480 1-bit image as a PBM file.
//
//   g++ -O2 -std=c++17 -Ilib/fishdraw -Isrc -Iinclude tools/preview/preview.cpp lib/fishdraw/fishdraw.cpp -o preview
//   ./preview --date 2026-11-11 out.pbm
//   ./preview --name "Carmalumis barneus" out.pbm
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "fishdraw.h"
#include "layout.h"

struct Bitmap : fishdraw::LineSink {
  int w, h;
  std::vector<unsigned char> bits;
  Bitmap(int w_, int h_) : w(w_), h(h_), bits((size_t)((w_ + 7) / 8) * h_, 0) {}
  void set(int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    bits[(size_t)y * ((w + 7) / 8) + x / 8] |= (unsigned char)(0x80 >> (x & 7));
  }
  void line(int x0, int y0, int x1, int y1) override {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
      set(x0, y0);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }
};

struct NullSink : fishdraw::LineSink {
  void line(int, int, int, int) override {}
};

// Generates `count` fish in a row and reports the extremes.
static int stress(int count) {
  NullSink sink;
  unsigned peak = 0, overflows = 0, failures = 0, max_segments = 0;
  long long max_ms = 0;
  char worst[64] = "";
  for (int i = 0; i < count; i++) {
    char seed[32], name[64];
    snprintf(seed, sizeof(seed), "stress-%d", i);
    fishdraw::make_name(seed, 0, name, sizeof(name));
    fishdraw::Stats st;
    auto t0 = std::chrono::steady_clock::now();
    if (!layout::draw_page(name, sink, &st)) failures++;
    long long ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (ms > max_ms) max_ms = ms;
    if (st.overflow) overflows++;
    if (st.segments > max_segments) max_segments = st.segments;
    if (st.arena_peak > peak) {
      peak = st.arena_peak;
      snprintf(worst, sizeof(worst), "%s", name);
    }
  }
  printf("%d fish: failures=%u overflows=%u peak store=%u bytes (%s) max segments=%u max time=%lld ms\n", count,
         failures, overflows, peak, worst, max_segments, max_ms);
  return failures ? 1 : 0;
}

int main(int argc, char** argv) {
  char name[64] = "";
  const char* out = "fish.pbm";
  unsigned salt = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--stress") && i + 1 < argc) {
      return stress(atoi(argv[++i]));
    } else if (!strcmp(argv[i], "--date") && i + 1 < argc) {
      fishdraw::make_name(argv[++i], salt, name, sizeof(name));
    } else if (!strcmp(argv[i], "--salt") && i + 1 < argc) {
      salt = (unsigned)atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--name") && i + 1 < argc) {
      snprintf(name, sizeof(name), "%s", argv[++i]);
    } else {
      out = argv[i];
    }
  }
  if (!name[0]) fishdraw::make_name("2026-11-11", salt, name, sizeof(name));

  Bitmap bmp(layout::SCREEN_W, layout::SCREEN_H);
  fishdraw::Stats st;
  auto t0 = std::chrono::steady_clock::now();
  bool ok = layout::draw_page(name, bmp, &st);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  printf("%s | ok=%d segments=%u store=%u/%u bytes overflow=%d %lld ms\n", name, ok, (unsigned)st.segments,
         (unsigned)st.arena_peak, (unsigned)st.arena_size, st.overflow, (long long)ms);

  FILE* f = fopen(out, "wb");
  if (!f) return 1;
  fprintf(f, "P4\n%d %d\n", bmp.w, bmp.h);
  fwrite(bmp.bits.data(), 1, bmp.bits.size(), f);
  fclose(f);
  return ok ? 0 : 1;
}
