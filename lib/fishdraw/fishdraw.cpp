// fishdraw for microcontrollers.
//
// Ported from fishdraw.js, Copyright (c) 2021 Lingdong Huang, MIT License
// (https://github.com/LingDong-/fishdraw). See LICENSE-fishdraw.txt.
//
// How this differs from the original:
//
//  * The original builds every polyline of the fish in memory, then scales the
//    lot to fit the page. A big fish is ~25,000 points, which as JavaScript
//    arrays is megabytes. Here, finished polylines are packed into one fixed
//    block of 16-bit coordinates (the "store"), the dense body textures are
//    generated one scale at a time straight into it, and the pattern shading,
//    which never changes the outline, is rasterised as it is generated.
//  * Clipping the parts against each other is deferred until all the clip
//    shapes exist, then done in place inside the store.
//  * Calls to the random number generator happen in exactly the original
//    order, so a name produces the same fish as the JavaScript does.
//
// Everything is single precision: the ESP32-C3 has no FPU, and soft doubles
// would roughly double the generation time.

#include "fishdraw.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#ifndef FISHDRAW_REAL
#define FISHDRAW_REAL float
#endif

// Bytes for the packed polyline store. The largest of 400 surveyed fish needs
// about 77 KB; anything that does not fit is dropped (Stats::overflow).
#ifndef FISHDRAW_ARENA_BYTES
#define FISHDRAW_ARENA_BYTES 90000
#endif

// Upper bound on poisson-disk samples, which bounds the memory used by the
// "small dots" pattern.
#ifndef FISHDRAW_MAX_SAMPLES
#define FISHDRAW_MAX_SAMPLES 8000
#endif

namespace fishdraw {
namespace {

#include "fishdraw_tables.inc"

#include "fishdraw_core.inc"

// ---------------------------------------------------------------------------
// Scales
// ---------------------------------------------------------------------------

// The unit outlines of a scale and of its mask are the same for every scale,
// so the trigonometry is done once.
Pt g_squama_unit[8];
Pt g_mask_unit[7];
bool g_units_ready = false;

void init_units() {
  if (g_units_ready) return;
  for (int i = 0; i < 8; i++) {
    real t = (real)i / 7;
    real a = t * PI + PI / 2;
    g_squama_unit[i] = P(-spow(std::cos(a), 1.4_r), spow(std::sin(a), 1.4_r));
  }
  for (int i = 0; i < 7; i++) {
    real t = (real)i / 7;
    real a = t * PI * 2;
    g_mask_unit[i] = P(-spow(std::cos(a), 1.3_r), spow(std::sin(a), 1.3_r));
  }
  g_units_ready = true;
}

Poly squama_mask(real w, real h) {
  Poly p(7);
  for (int i = 0; i < 7; i++) p[i] = P(g_mask_unit[i].x * w, g_mask_unit[i].y * h);
  return p;
}

// One scale: an arc, plus m short lines of texture.
Polys squama(real w, real h, int m) {
  Polys q;
  Poly p(8);
  for (int i = 0; i < 8; i++) p[i] = P(g_squama_unit[i].x * w, g_squama_unit[i].y * h);
  q.push_back(p);
  for (int i = 0; i < m; i++) {
    real t = (real)i / (real)(m - 1);
    Poly l(2);
    real ax = -w * 0.3_r + (rnd() - 0.5_r);
    real ay = -h * 0.2_r + t * h * 0.4_r + (rnd() - 0.5_r);
    real bx = w * 0.5_r + (rnd() - 0.5_r);
    real by = -h * 0.3_r + t * h * 0.6_r + (rnd() - 0.5_r);
    l[0] = P(ax, ay);
    l[1] = P(bx, by);
    q.push_back(l);
  }
  return q;
}

// The warped grid that scales sit on. The original keeps the whole grid; this
// keeps the four columns in use, since the scales are visited column by column.
struct Mesh {
  int m, n;
  real uw, uh, noise_x, noise_y;
  int col_index[4];
  Poly cols[4];

  Mesh(int m_, int n_, real uw_, real uh_, real nx, real ny)
      : m(m_), n(n_), uw(uw_), uh(uh_), noise_x(nx), noise_y(ny) {
    for (int k = 0; k < 4; k++) col_index[k] = -1;
  }

  const Poly& column(int j) {
    int slot = j & 3;
    if (col_index[slot] != j) {
      Poly& c = cols[slot];
      c.resize(n);
      for (int i = 0; i < n; i++) {
        real x = j * uw;
        real y = (n * uh / 2) - std::cos((real)i / (real)(n - 1) * PI) * (n * uh / 2);
        real nz = noise(x * 0.005_r, y * 0.005_r);
        real a = nz * PI * 2 - PI;
        real ca = std::cos(a);
        c[i] = P(x + ca * nz * noise_x, y + ca * nz * noise_y);
      }
      col_index[slot] = j;
    }
    return cols[slot];
  }

  Pt pt(int i, int j) { return column(j)[i]; }

  void wh(int i, int j, real* dw, real* dh) {
    if (i == 0 || j == 0 || i == n - 1 || j == m - 1) {
      *dw = uw / 2;
      *dh = uh / 2;
      return;
    }
    Pt a = pt(i, j), b = pt(i, j + 1), c = pt(i, j - 1), d = pt(i - 1, j), e = pt(i + 1, j);
    *dw = (dist(a, b) + dist(a, c)) / 4;
    *dh = (dist(a, d) + dist(a, e)) / 4;
  }

  // Calls fn(x, y, dw, dh) for every scale, in the original's order.
  template <class Fn>
  void for_each_scale(Fn fn) {
    for (int j = 1; j < m - 1; j++) {
      for (int i = 1; i < n - 1; i++) {
        Pt p = pt(i, j);
        real dw, dh;
        wh(i, j, &dw, &dh);
        fn(p.x, p.y, dw, dh);
      }
      for (int i = 1; i < n - 1; i++) {
        Pt a = pt(i, j), b = pt(i, j + 1), c = pt(i + 1, j), d = pt(i + 1, j + 1);
        real dwa, dha, dwb, dhb, dwc, dhc, dwd, dhd;
        wh(i, j, &dwa, &dha);
        wh(i, j + 1, &dwb, &dhb);
        wh(i + 1, j, &dwc, &dhc);
        wh(i + 1, j + 1, &dwd, &dhd);
        real x = (a.x + b.x + c.x + d.x) / 4, y = (a.y + b.y + c.y + d.y) / 4;
        real dw = (dwa + dwb + dwc + dwd) / 4, dh = (dha + dhb + dhc + dhd) / 4;
        dw *= 1.2_r;
        fn(x, y, dw, dh);
      }
    }
  }
};

// ---------------------------------------------------------------------------
// Body patterns
// ---------------------------------------------------------------------------

inline real gauss2d(real x, real y) { return std::exp(-0.5_r * x * x) * std::exp(-0.5_r * y * y); }

struct Pattern {
  int type;
  real scale;
  Poly samples;
  std::vector<real> rs;

  Pattern() : type(0), scale(1) {}

  // True for the patterns that are a function of position (1-3).
  bool active() const { return type >= 1 && type <= 3; }

  bool operator()(real x, real y) const {
    if (type == 1) {
      for (size_t i = 0; i < samples.size(); i++) {
        real r = rs[i];
        real dx = x - samples[i].x, dy = y - samples[i].y;
        if (dx * dx + dy * dy < r * r) {
          if (gauss2d(dx / r * 2, dy / r * 2) * noise(x, y, 999) > 0.2_r) return true;
        }
      }
      return false;
    }
    if (type == 2) {
      return (noise(x * 0.1_r, y * 0.1_r) * std::max(0.35_r, (y - 10) / 280)) < 0.2_r;
    }
    if (type == 3) {
      real dx = noise(x * 0.01_r, y * 0.01_r) * 30;
      return (trunc_i((x + dx) / (30 * scale))) % 2 == 1;
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// The polyline store
// ---------------------------------------------------------------------------

enum { G_SH, G_BD, G_F0, G_F1, G_F2, G_F3, G_F4, G_F5, G_FH, G_COUNT };

const real Q = 16;  // store resolution: 1/16 of a drawing unit

struct Store {
  int16_t* buf;
  size_t cap, len, peak;
  bool overflow;
  size_t off[G_COUNT], glen[G_COUNT];
  int cur;

  Store() : buf(nullptr), cap(0), len(0), peak(0), overflow(false), cur(-1) {}

  bool init(size_t bytes) {
    release();
    cap = bytes / 2;
    buf = (int16_t*)malloc(cap * 2);
    len = peak = 0;
    overflow = false;
    for (int g = 0; g < G_COUNT; g++) off[g] = glen[g] = 0;
    return buf != nullptr;
  }

  void release() {
    free(buf);
    buf = nullptr;
    cap = len = 0;
  }

  void begin(int g) {
    cur = g;
    off[g] = len;
    glen[g] = 0;
  }
  void end() {
    glen[cur] = len - off[cur];
    cur = -1;
  }

  static inline int16_t quant(real v) {
    real q = v * Q;
    if (q > 32767) q = 32767;
    if (q < -32767) q = -32767;
    return (int16_t)std::lround(q);
  }

  // Packs points [p, p+n) at dst, dropping repeats. Returns slots used: 0 if
  // fewer than two distinct points remain.
  static size_t encode(const Pt* p, size_t n, int16_t* dst) {
    size_t cnt = 0;
    int16_t lx = 0, ly = 0;
    for (size_t i = 0; i < n; i++) {
      int16_t x = quant(p[i].x), y = quant(p[i].y);
      if (cnt && x == lx && y == ly) continue;
      dst[1 + cnt * 2] = x;
      dst[2 + cnt * 2] = y;
      lx = x;
      ly = y;
      cnt++;
    }
    if (cnt < 2) return 0;
    dst[0] = (int16_t)(uint16_t)cnt;
    return 1 + cnt * 2;
  }

  // Calls fn(first, count) for each run of finite points, so that a NaN
  // breaks a line instead of dragging it across the page.
  template <class Fn>
  static void runs(const Poly& p, Fn fn) {
    size_t i = 0, n = p.size();
    while (i < n) {
      while (i < n && !(std::isfinite(p[i].x) && std::isfinite(p[i].y))) i++;
      size_t j = i;
      while (j < n && std::isfinite(p[j].x) && std::isfinite(p[j].y)) j++;
      size_t k = i;
      while (j - k > 30000) {  // a count must fit in 15 bits
        fn(&p[k], (size_t)30000);
        k += 29999;
      }
      if (j - k >= 2) fn(&p[k], j - k);
      i = j;
    }
  }

  void add(const Poly& p) {
    runs(p, [this](const Pt* q, size_t n) {
      if (len + 1 + 2 * n > cap) {
        overflow = true;
        return;
      }
      len += encode(q, n, buf + len);
      if (len > peak) peak = len;
    });
  }

  void add(const Polys& ps) {
    for (size_t i = 0; i < ps.size(); i++) add(ps[i]);
  }

  void read(size_t& pos, Poly& out) const {
    size_t n = (uint16_t)buf[pos++];
    out.resize(n);
    for (size_t i = 0; i < n; i++) {
      out[i] = P(buf[pos] / Q, buf[pos + 1] / Q);
      pos += 2;
    }
  }

  // Replaces a group by what is left of it outside every region given. Works
  // in place: output is written over input already consumed, and only spills
  // past the end when a polyline grows.
  void clip_group(int g, const Region* const* regions, int nregions) {
    if (!glen[g]) return;
    if (off[g] + glen[g] != len) {
      std::rotate(buf + off[g], buf + off[g] + glen[g], buf + len);
      for (int h = 0; h < G_COUNT; h++) {
        if (h != g && off[h] > off[g]) off[h] -= glen[g];
      }
      off[g] = len - glen[g];
    }
    size_t r = off[g], w = off[g];
    const size_t rend = len, spill_begin = len;
    Poly p;
    Polys a, b;
    while (r < rend) {
      read(r, p);
      a.clear();
      a.push_back(p);
      for (int k = 0; k < nregions; k++) {
        b.clear();
        for (size_t i = 0; i < a.size(); i++) clip(a[i], *regions[k], nullptr, &b);
        a.swap(b);
      }
      for (size_t i = 0; i < a.size(); i++) {
        runs(a[i], [&](const Pt* q, size_t n) {
          size_t need = 1 + 2 * n;
          if (w + need <= r) {
            w += encode(q, n, buf + w);
          } else if (len + need <= cap) {
            len += encode(q, n, buf + len);
            if (len > peak) peak = len;
          } else {
            overflow = true;
          }
        });
      }
    }
    size_t spill = len - spill_begin;
    if (spill) memmove(buf + w, buf + spill_begin, spill * sizeof(int16_t));
    len = w + spill;
    glen[g] = len - off[g];
  }
};

Store S;

// ---------------------------------------------------------------------------
// Bodies. Each writes its polylines to the open group of the store.
// ---------------------------------------------------------------------------

void fish_body_a(const Poly& curve0, const Poly& curve1, real scale_scale, const Pattern& pf) {
  Poly curve2, curve3;
  for (size_t i = 0; i < curve0.size(); i++) {
    curve2.push_back(lerp2d(curve0[i], curve1[i], 0.95_r));
    curve3.push_back(lerp2d(curve0[i], curve1[i], 0.85_r));
  }
  Poly outline2 = concat(curve0, rev(curve2));
  Poly outline3 = concat(curve0, rev(curve3));
  Region r2(outline2), r3(outline3);

  BBox bbox = get_bbox(concat(curve0, curve1));
  S.add(curve0);
  S.add(rev(curve1));
  int m = trunc_i(bbox.w / (scale_scale * 15));
  int n = trunc_i(bbox.h / (scale_scale * 15));
  if (m < 1 || n < 1) return;
  real uw = bbox.w / m, uh = bbox.h / n;
  Mesh mesh(m, n + 3, uw, uh, uw * 3, uh * 3);

  // In the original, every scale is generated (drawing random numbers for its
  // texture lines) before the random thinning of the belly scales begins. To
  // generate and thin one scale at a time, find out first how many random
  // numbers the scales will use, and run a second generator that far ahead.
  std::vector<bool> lines;
  uint32_t textured = 0;
  mesh.for_each_scale([&](real x, real y, real, real) {
    bool f = pf.active() ? pf(x, y) : true;
    lines.push_back(f);
    if (f) textured++;
  });
  uint32_t thin = jsr;
  for (uint32_t k = 0; k < textured * 12; k++) thin = xorshift(thin);

  Poly clipper;
  bool have_clipper = false;
  size_t index = 0;
  Polys visible, inside, t3, f3;
  mesh.for_each_scale([&](real x, real y, real dw, real dh) {
    Poly q = trsl_poly(squama_mask(dw, dh), x, y);
    Polys p = squama(dw, dh, lines[index++] ? 3 : 0);
    for (size_t i = 0; i < p.size(); i++) p[i] = trsl_poly(p[i], x, y);
    visible.clear();
    if (have_clipper) {
      Region rc(clipper);
      for (size_t i = 0; i < p.size(); i++) clip(p[i], rc, nullptr, &visible);
      clipper = poly_union(clipper, q);
    } else {
      visible = p;
      clipper = q;
      have_clipper = true;
    }
    for (size_t i = 0; i < visible.size(); i++) {
      Poly o = trsl_poly(visible[i], bbox.x, bbox.y - uh * 1.5_r);
      inside.clear();
      clip(o, r2, &inside, nullptr);
      for (size_t k = 0; k < inside.size(); k++) {
        t3.clear();
        f3.clear();
        clip(inside[k], r3, &t3, &f3);
        S.add(t3);
        for (size_t u = 0; u < f3.size(); u++) {
          thin = xorshift(thin);
          if (to_unit(thin) < 0.6_r) S.add(f3[u]);
        }
      }
    }
  });
  jsr = thin;
}

void fish_body_b(const Poly& curve0, const Poly& curve1, real scale_scale, const Pattern& pf) {
  Poly curve2;
  for (size_t i = 0; i < curve0.size(); i++) curve2.push_back(lerp2d(curve0[i], curve1[i], 0.95_r));
  Poly outline2 = concat(curve0, rev(curve2));
  Region r2(outline2);

  BBox bbox = get_bbox(concat(curve0, curve1));
  S.add(curve0);
  S.add(rev(curve1));
  int m = trunc_i(bbox.w / (scale_scale * 5));
  int n = trunc_i(bbox.h / (scale_scale * 5));
  if (m < 1 || n < 1) return;
  real uw = bbox.w / m, uh = bbox.h / n;
  Mesh mesh(m, n + 16, uw, uh, uw * 8, uh * 8);
  bool has_pattern = pf.active();

  Polys inside;
  Poly o(8);
  mesh.for_each_scale([&](real x, real y, real dw, real dh) {
    real w = dw * 0.7_r, h = dh * 0.6_r;
    real ox = x + bbox.x, oy = y + bbox.y - uh * 8;
    for (int i = 0; i < 8; i++) o[i] = P(g_squama_unit[i].x * w + ox, g_squama_unit[i].y * h + oy);
    inside.clear();
    clip(o, r2, &inside, nullptr);
    for (size_t k = 0; k < inside.size(); k++) {
      real px = inside[k][0].x, py = inside[k][0].y;
      real t = (py - bbox.y) / bbox.h;
      if (has_pattern) {
        if (pf(px, py) || (rnd() > t && rnd() > t)) S.add(inside[k]);
      } else {
        if (rnd() > t) S.add(inside[k]);
      }
    }
  });
}

void fish_body_c(const Poly& curve0, const Poly& curve1, real scale_scale) {
  real step = 6 * scale_scale;
  Poly curve2, curve3;
  for (size_t i = 0; i < curve0.size(); i++) {
    curve2.push_back(lerp2d(curve0[i], curve1[i], 0.95_r));
    curve3.push_back(lerp2d(curve0[i], curve1[i], 0.4_r));
  }
  Poly outline2 = concat(curve0, rev(curve2));
  Region r2(outline2);

  BBox bbox = get_bbox(concat(curve0, curve1));
  bbox.x -= step;
  bbox.y -= step;
  bbox.w += step * 2;
  bbox.h += step * 2;

  S.add(curve0);
  S.add(rev(curve1));

  Polys inside, kept;
  auto do_line = [&](const Poly& raw) {
    Poly line = resample(raw, 4);
    for (size_t j = 0; j < line.size(); j++) {
      real x = line[j].x, y = line[j].y;
      real t = (y - bbox.y) / bbox.h;
      real y1 = -std::cos(t * PI) * bbox.h / 2 + bbox.y + bbox.h / 2;
      real dx = (noise(x * 0.005_r, y1 * 0.005_r, 0.1_r) - 0.5_r) * 50;
      real dy = (noise(x * 0.005_r, y1 * 0.005_r, 1.2_r) - 0.5_r) * 50;
      line[j].x += dx;
      line[j].y = y1 + dy;
    }
    inside.clear();
    clip(line, r2, &inside, nullptr);
    for (size_t k = 0; k < inside.size(); k++) {
      kept.clear();
      binclip(inside[k], [](real, real, real t) { return rnd() > t || rnd() > t; }, &kept, nullptr);
      S.add(kept);
    }
  };

  do_line(rev(curve3));
  Poly l(2);
  for (real i = -bbox.h; i < bbox.w; i += step) {
    l[0] = P(bbox.x + i, bbox.y);
    l[1] = P(bbox.x + i + bbox.h, bbox.y + bbox.h);
    do_line(l);
  }
  for (real i = 0; i < bbox.w + bbox.h; i += step) {
    l[0] = P(bbox.x + i, bbox.y);
    l[1] = P(bbox.x + i - bbox.h, bbox.y + bbox.h);
    do_line(l);
  }
}

void fish_body_d(const Poly& curve0_in, const Poly& curve1_in, real scale_scale) {
  Poly curve2;
  for (size_t i = 0; i < curve0_in.size(); i++) {
    curve2.push_back(lerp2d(curve0_in[i], curve1_in[i], 0.4_r));
  }
  Poly curve0 = resample(curve0_in, 10 * scale_scale);
  Poly curve1 = resample(curve1_in, 10 * scale_scale);
  curve2 = resample(curve2, 10 * scale_scale);

  Poly outline1 = concat(curve0, rev(curve1));
  Region r1(outline1);

  S.add(curve0);
  S.add(rev(curve1));

  Polys kept, inside;
  auto do_line = [&](const Poly& raw) {
    Poly line = resample(raw, 4);
    for (size_t j = 0; j < line.size(); j++) {
      real x = line[j].x, y = line[j].y;
      real dx = 30 * (noise(x * 0.01_r, y * 0.01_r, -1) - 0.5_r);
      real dy = 30 * (noise(x * 0.01_r, y * 0.01_r, 9) - 0.5_r);
      line[j].x += dx;
      line[j].y += dy;
    }
    kept.clear();
    binclip(
        line,
        [](real x, real, real t) {
          real c = std::cos(t * PI);
          return (rnd() > c && rnd() < x / 500) || (rnd() > c && rnd() < x / 500);
        },
        &kept, nullptr);
    for (size_t k = 0; k < kept.size(); k++) {
      inside.clear();
      clip(kept[k], r1, &inside, nullptr);
      S.add(inside);
    }
  };

  do_line(curve2);
  size_t lim = std::min(curve0.size(), std::min(curve1.size(), curve2.size()));
  Poly l(2);
  for (size_t i = 3; i < lim; i++) {
    l[0] = curve0[i];
    l[1] = curve2[i - 3];
    do_line(l);
    l[0] = curve2[i - 3];
    l[1] = curve1[i];
    do_line(l);
  }
  S.add(vein_shape(outline1, 50));
}

// ---------------------------------------------------------------------------
// Fins
// ---------------------------------------------------------------------------

struct Shape {
  Poly c;   // the outline, used to clip what lies behind
  Polys f;  // the lines to draw
};

typedef std::function<real(real)> Profile;

std::vector<real> fin_angles(const Poly& curve) {
  std::vector<real> angs;
  int n = (int)curve.size();
  for (int i = 0; i < n; i++) {
    if (i == 0) {
      angs.push_back(std::atan2(curve[i + 1].y - curve[i].y, curve[i + 1].x - curve[i].x) - PI / 2);
    } else if (i == n - 1) {
      angs.push_back(std::atan2(curve[i].y - curve[i - 1].y, curve[i].x - curve[i - 1].x) - PI / 2);
    } else {
      real a0 = std::atan2(curve[i - 1].y - curve[i].y, curve[i - 1].x - curve[i].x);
      real a1 = std::atan2(curve[i + 1].y - curve[i].y, curve[i + 1].x - curve[i].x);
      while (a1 > a0) a1 -= PI * 2;
      a1 += PI * 2;
      angs.push_back((a0 + a1) / 2);
    }
  }
  return angs;
}

// A soft fin made of rays.
Shape fin_a(const Poly& curve, real ang0, real ang1, const Profile& func, bool clip_root = false,
            real curvature0 = 0, real curvature1 = 0, real softness = 10) {
  Shape res;
  int n = (int)curve.size();
  if (n < 2) return res;
  std::vector<real> angs = fin_angles(curve);
  Poly out0, out2, out3;
  Polys out1;
  for (int i = 0; i < n; i++) {
    real t = (real)i / (real)(n - 1);
    real aa = lerp(ang0, ang1, t);
    real a = angs[i] + aa;
    real w = func(t);
    real x0 = curve[i].x, y0 = curve[i].y;
    real x1 = x0 + std::cos(a) * w;
    real y1 = y0 + std::sin(a) * w;
    Poly seg(2);
    seg[0] = P(x0, y0);
    seg[1] = P(x1, y1);
    Poly p = resample(seg, 3);
    int pn = (int)p.size();
    for (int j = 0; j < pn; j++) {
      real s = (real)j / (real)(pn - 1);
      real ss = std::sqrt(s);
      real x = p[j].x, y = p[j].y;
      real cv = lerp(curvature0, curvature1, t) * std::sin(s * PI);
      p[j].x += noise(x * 0.1_r, y * 0.1_r, 3) * ss * softness + std::cos(a - PI / 2) * cv;
      p[j].y += noise(x * 0.1_r, y * 0.1_r, 4) * ss * softness + std::sin(a - PI / 2) * cv;
    }
    if (i == 0) {
      out2 = p;
    } else if (i == n - 1) {
      out3 = rev(p);
    } else {
      out0.push_back(p.back());
      int start = clip_root ? trunc_i(rnd() * 4) : 0;
      int end = std::max(2, trunc_i(pn * (rnd() * 0.5_r + 0.5_r)));
      Poly q = slice(p, start, end);
      if (!q.empty()) out1.push_back(q);
    }
  }
  out0 = resample(out0, 3);
  for (size_t i = 0; i < out0.size(); i++) {
    real x = out0[i].x, y = out0[i].y;
    real d = (noise(x * 0.1_r, y * 0.1_r) * 6 - 3) * (softness / 10);
    out0[i].x += d;
    out0[i].y += d;
  }
  Poly o = concat(concat(out2, out0), out3);
  res.f.push_back(o);
  res.f.insert(res.f.end(), out1.begin(), out1.end());
  res.c = concat(o, rev(curve));
  return res;
}

// A spiny fin made of rigid spines with webbing between them.
Shape fin_b(const Poly& curve, real ang0, real ang1, const Profile& func, real dark = 1) {
  Shape res;
  int cn = (int)curve.size();
  if (cn < 2) return res;
  std::vector<real> angs = fin_angles(curve);

  Polys out0;  // each: p, b, c, q
  Polys out2, out3;
  for (int i = 0; i < cn; i++) {
    real t = (real)i / (real)(cn - 1);
    real aa = lerp(ang0, ang1, t);
    real a = angs[i] + aa;
    real w = func(t);
    real x0 = curve[i].x, y0 = curve[i].y;
    real x1 = x0 + std::cos(a) * w;
    real y1 = y0 + std::sin(a) * w;
    Poly quad(4);
    quad[0] = P(x0 + 1.8_r * std::cos(a - PI / 2), y0 + 1.8_r * std::sin(a - PI / 2));
    quad[1] = P(x1 + 0.5_r * std::cos(a - PI / 2), y1 + 0.5_r * std::sin(a - PI / 2));
    quad[2] = P(x1 + 0.5_r * std::cos(a + PI / 2), y1 + 0.5_r * std::sin(a + PI / 2));
    quad[3] = P(x0 + 1.8_r * std::cos(a + PI / 2), y0 + 1.8_r * std::sin(a + PI / 2));
    out0.push_back(quad);
  }

  const int n = 10;
  for (int i = 0; i < cn - 1; i++) {
    Pt a0 = out0[i][2], q0 = out0[i][3];
    Pt p1 = out0[i + 1][0], a1 = out0[i + 1][1];
    Pt b = lerp2d(a0, q0, 0.1_r);
    Pt c = lerp2d(a1, p1, 0.1_r);
    Poly o;
    real ang = std::atan2(c.y - b.y, c.x - b.x);
    for (int j = 0; j < n; j++) {
      real t = (real)j / (real)(n - 1);
      real d = std::sin(t * PI) * 2;
      Pt a = lerp2d(b, c, t);
      o.push_back(P(a.x + std::cos(ang + PI / 2) * d, a.y + std::sin(ang + PI / 2) * d));
    }
    out2.push_back(o);

    int m = trunc_i(std::min(dist(a0, q0), dist(a1, p1)) / 10 * dark);
    Pt e = lerp2d(curve[i], curve[i + 1], 0.5_r);
    for (int k = 0; k < m; k++) {
      Poly p;
      real s = (real)k / (real)m * 0.7_r;
      for (int j = 1; j < n - 1; j++) p.push_back(lerp2d(o[j], e, s));
      out3.push_back(p);
    }
  }

  Polys out4;
  if (out0.size() > 1) {
    Poly clipper = out0[0];
    out4.push_back(out0[0]);
    for (size_t i = 1; i < out0.size(); i++) {
      Region rc(clipper);
      clip(out0[i], rc, nullptr, &out4);
      clipper = poly_union(clipper, out0[i]);
    }
  }

  for (size_t i = 0; i < out2.size(); i++) res.c.insert(res.c.end(), out2[i].begin(), out2[i].end());
  Poly rc = rev(curve);
  res.c.insert(res.c.end(), rc.begin(), rc.end());
  res.f = out4;
  res.f.insert(res.f.end(), out2.begin(), out2.end());
  res.f.insert(res.f.end(), out3.begin(), out3.end());
  return res;
}

Shape finlet(const Poly& curve, real h, int dir = 1) {
  Shape res;
  int n = (int)curve.size();
  if (n < 2) return res;
  std::vector<real> angs = fin_angles(curve);
  Poly out0;
  for (int i = 0; i < n; i++) {
    real t = (real)i / (real)(n - 1);
    real a = angs[i];
    real w = (i + 1) % 3 ? 0 : h;
    if (dir > 0) {
      w *= (1 - t * 0.5_r);
    } else {
      w *= 0.5_r + t * 0.5_r;
    }
    out0.push_back(P(curve[i].x + std::cos(a) * w, curve[i].y + std::sin(a) * w));
  }
  out0 = resample(out0, 2);
  for (size_t i = 0; i < out0.size(); i++) {
    real x = out0[i].x, y = out0[i].y;
    real d = noise(x * 0.1_r, y * 0.1_r) * 2 - 3;
    out0[i].x += d;
    out0[i].y += d;
  }
  out0.push_back(curve.back());
  res.c = concat(out0, rev(curve));
  res.f.push_back(out0);
  return res;
}

Shape fin_adipose(const Poly& curve, real dx, real dy, real r) {
  Shape res;
  if (curve.empty()) return res;
  const int n = 20;
  Pt mid = curve[curve.size() / 2];
  real x = mid.x + dx, y = mid.y + dy;
  real x1 = curve[0].x, y1 = curve[0].y;
  real x2 = curve.back().x, y2 = curve.back().y;
  real d1 = dist(x, y, x1, y1);
  real d2 = dist(x, y, x2, y2);
  real a1 = std::acos(r / d1);
  real a2 = std::acos(r / d2);
  real a01 = std::atan2(y1 - y, x1 - x) + a1;
  real a02 = std::atan2(y2 - y, x2 - x) - a2;
  a02 -= PI * 2;
  while (a02 < a01) a02 += PI * 2;
  Poly out0;
  out0.push_back(P(x1, y1));
  for (int i = 0; i < n; i++) {
    real t = (real)i / (real)(n - 1);
    real a = lerp(a01, a02, t);
    out0.push_back(P(x + std::cos(a) * r, y + std::sin(a) * r));
  }
  out0.push_back(P(x2, y2));
  out0 = resample(out0, 3);
  int on = (int)out0.size();
  for (int i = 0; i < on; i++) {
    real t = (real)i / (real)(on - 1);
    real s = std::sin(t * PI);
    real px = out0[i].x, py = out0[i].y;
    real d = (noise(px * 0.01_r, py * 0.01_r) - 0.5_r) * s * 50;
    out0[i].x += d;
    out0[i].y += d;
  }
  res.c = concat(out0, rev(curve));
  Polys out1 = clip1(trsl_poly(out0, 0, 4), res.c, true);
  res.f.push_back(out0);
  for (size_t i = 0; i < out1.size(); i++) {
    binclip(out1[i], [](real, real, real t) { return rnd() < std::sin(t * PI); }, &res.f, nullptr);
  }
  return res;
}

// ---------------------------------------------------------------------------
// Head
// ---------------------------------------------------------------------------

Poly fish_lip(real x0, real y0, real x1, real y1, real w) {
  x0 += rnd() * 0.001_r - 0.0005_r;
  y0 += rnd() * 0.001_r - 0.0005_r;
  x1 += rnd() * 0.001_r - 0.0005_r;
  y1 += rnd() * 0.001_r - 0.0005_r;
  real h = dist(x0, y0, x1, y1);
  real a0 = std::atan2(y1 - y0, x1 - x0);
  const int n = 10;
  real ang = std::acos(w / h);
  real dx = std::cos(a0 + PI / 2) * 0.5_r;
  real dy = std::sin(a0 + PI / 2) * 0.5_r;
  Poly o;
  o.push_back(P(x0 - dx, y0 - dy));
  for (int i = 0; i < n; i++) {
    real t = (real)i / (real)(n - 1);
    real a = lerp(ang, PI * 2 - ang, t) + a0;
    o.push_back(P(-std::cos(a) * w + x1, -std::sin(a) * w + y1));
  }
  o.push_back(P(x0 + dx, y0 + dy));
  o = resample(o, 2.5_r);
  for (size_t i = 0; i < o.size(); i++) {
    real x = o[i].x, y = o[i].y;
    o[i].x += noise(x * 0.05_r, y * 0.05_r, -1) * 2 - 1;
    o[i].y += noise(x * 0.05_r, y * 0.05_r, -2) * 2 - 1;
  }
  return o;
}

Polys fish_teeth(real x0, real y0, real x1, real y1, real h, real dir, real sep) {
  int n = std::max(2, trunc_i(dist(x0, y0, x1, y1) / sep));
  real ang = std::atan2(y1 - y0, x1 - x0);
  Polys out;
  for (int i = 0; i < n; i++) {
    real t = (real)i / (real)(n - 1);
    Pt a = lerp2d(P(x0, y0), P(x1, y1), t);
    real w = h * t;
    Pt b = P(a.x + std::cos(ang + dir * PI / 2) * w, a.y + std::sin(ang + dir * PI / 2) * w);
    Pt c = P(a.x + std::cos(ang), a.y + std::sin(ang));
    Pt d = P(a.x + std::cos(ang + PI), a.y + std::sin(ang + PI));
    Pt e = lerp2d(c, b, 0.7_r);
    Pt f = lerp2d(d, b, 0.7_r);
    Pt g = P(a.x + std::cos(ang + dir * (PI / 2 + 0.15_r)) * w,
             a.y + std::sin(ang + dir * (PI / 2 + 0.15_r)) * w);
    Poly tooth(5);
    tooth[0] = c;
    tooth[1] = e;
    tooth[2] = g;
    tooth[3] = f;
    tooth[4] = d;
    out.push_back(tooth);
  }
  return out;
}

Polys fish_jaw(real x0, real y0, real x1, real y1, real x2, real y2) {
  (void)x1;
  (void)y1;
  const int n = 10;
  real ang = std::atan2(y2 - y0, x2 - x0);
  real d = dist(x0, y0, x2, y2);
  Poly o;
  for (int i = 0; i < n; i++) {
    real t = (real)i / (real)(n - 1);
    real s = std::sin(t * PI);
    real w = s * d / 20;
    Pt p = lerp2d(P(x2, y2), P(x0, y0), t);
    Pt q = P(p.x + std::cos(ang - PI / 2) * w, p.y + std::sin(ang - PI / 2) * w);
    o.push_back(P(q.x + (noise(q.x * 0.01_r, q.y * 0.01_r, 1) - 0.5_r) * 4 * s,
                  q.y + (noise(q.x * 0.01_r, q.y * 0.01_r, 4) - 0.5_r) * 4 * s));
  }
  Polys out;
  out.push_back(o);
  Polys v = vein_shape(o, 5);
  out.insert(out.end(), v.begin(), v.end());
  return out;
}

Shape fish_eye_a(real ex, real ey, real rad) {
  const int n = 20;
  Poly eye0, eye1, eye2;
  for (int i = 0; i < n; i++) {
    real t = (real)i / (real)(n - 1);
    real a = t * PI * 2 + PI / 4 * 3;
    eye0.push_back(P(ex + std::cos(a) * rad, ey + std::sin(a) * rad));
    if (t > 0.5_r) {
      eye1.push_back(P(ex + std::cos(a) * (rad * 0.8_r), ey + std::sin(a) * (rad * 0.8_r)));
    }
    eye2.push_back(P(ex + std::cos(a) * (rad * 0.4_r) - 0.75_r, ey + std::sin(a) * (rad * 0.4_r) - 0.75_r));
  }
  Polys ef = shade_shape(eye2, 2.7_r, 10, 10);
  Shape res;
  res.c = eye0;
  res.f.push_back(eye0);
  res.f.push_back(eye1);
  res.f.push_back(eye2);
  res.f.insert(res.f.end(), ef.begin(), ef.end());
  return res;
}

Shape fish_eye_b(real ex, real ey, real rad) {
  const int n = 20;
  Poly eye0, eye2;
  Polys eye1;
  for (int i = 0; i < n; i++) {
    real t = (real)i / (real)(n - 1);
    real a = t * PI * 2 + E_;
    eye0.push_back(P(ex + std::cos(a) * rad, ey + std::sin(a) * rad));
    eye2.push_back(P(ex + std::cos(a) * (rad * 0.4_r), ey + std::sin(a) * (rad * 0.4_r)));
  }
  int m = trunc_i((rad * 0.6_r) / 2);
  for (int i = 0; i < m; i++) {
    real r = rad - i * 2;
    Poly e;
    for (int k = 0; k < n; k++) {
      real t = (real)k / (real)(n - 1);
      real a = lerp(PI * 7 / 8, PI * 13 / 8, t);
      e.push_back(P(ex + std::cos(a) * r, ey + std::sin(a) * r));
    }
    eye1.push_back(e);
  }
  Poly trig;
  trig.push_back(P(ex + std::cos(-PI * 3 / 4) * (rad * 0.9_r), ey + std::sin(-PI * 3 / 4) * (rad * 0.9_r)));
  trig.push_back(P(ex + 1, ey + 1));
  trig.push_back(P(ex + std::cos(-PI * 11 / 12) * (rad * 0.9_r), ey + std::sin(-PI * 11 / 12) * (rad * 0.9_r)));
  trig = resample(trig, 3);
  for (size_t i = 0; i < trig.size(); i++) {
    real x = trig[i].x, y = trig[i].y;
    x += noise(x * 0.1_r, y * 0.1_r, 22) * 4 - 2;
    y += noise(x * 0.1_r, y * 0.1_r, 33) * 4 - 2;
    trig[i] = P(x, y);
  }
  Polys ef = fill_shape(eye2, 1.5_r);
  ef = clip_multi(ef, trig, false);
  eye1 = clip_multi(eye1, trig, false);
  Polys eye2c = clip1(eye2, trig, false);

  Shape res;
  res.c = eye0;
  res.f.push_back(eye0);
  res.f.insert(res.f.end(), eye1.begin(), eye1.end());
  res.f.insert(res.f.end(), eye2c.begin(), eye2c.end());
  res.f.insert(res.f.end(), ef.begin(), ef.end());
  return res;
}

Poly barbel(real x, real y, int n, real ang, real dd = 3) {
  Poly curve;
  curve.push_back(P(x, y));
  real sd = rnd() * PI * 2;
  real ar = 1;
  for (int i = 0; i < n; i++) {
    x += std::cos(ang) * dd;
    y += std::sin(ang) * dd;
    ang += (noise(i * 0.1_r, sd) - 0.5_r) * ar;
    if (i < n / 2.0_r) {
      ar *= 1.02_r;
    } else {
      ar *= 0.92_r;
    }
    curve.push_back(P(x, y));
  }
  Poly o0, o1;
  for (int i = 0; i < n - 1; i++) {
    real t = (real)i / (real)(n - 1);
    real w = 1.5_r * (1 - t);
    Pt b = curve[i];
    Pt c = curve[i + 1];
    real a1 = std::atan2(c.y - b.y, c.x - b.x);
    real a2;
    if (i > 0) {
      Pt a = curve[i - 1];
      real a0 = std::atan2(a.y - b.y, a.x - b.x);
      a1 -= PI * 2;
      while (a1 < a0) a1 += PI * 2;
      a2 = (a0 + a1) / 2;
    } else {
      a2 = a1 - PI / 2;
    }
    o0.push_back(P(b.x + std::cos(a2) * w, b.y + std::sin(a2) * w));
    o1.push_back(P(b.x + std::cos(a2 + PI) * w, b.y + std::sin(a2 + PI) * w));
  }
  o0.push_back(curve.back());
  return concat(o0, rev(o1));
}

struct Params {
  int body_curve_type;
  real body_curve_amount, body_length, body_height;
  int scale_type;
  real scale_scale;
  int pattern_type;
  real pattern_scale;
  int dorsal_texture_type, dorsal_type;
  real dorsal_length;
  int dorsal_start, dorsal_end;
  int wing_texture_type, wing_type, wing_start, wing_end;
  real wing_y, wing_length, wing_width;
  int pelvic_start, pelvic_end;
  real pelvic_length;
  int pelvic_type, pelvic_texture_type;
  int anal_start, anal_end;
  real anal_length;
  int anal_type, anal_texture_type;
  int tail_type;
  real tail_length;
  int finlet_type, neck_type;
  real nose_height;
  int mouth_size;
  real head_length;
  int head_texture_amount;
  int has_moustache, moustache_length, has_beard, has_teeth;
  real teeth_length, teeth_space;
  int beard_length;
  int eye_type;
  real eye_size, jaw_size, jaw_open;
};

Shape fish_head(real x0, real y0, real x1, real y1, real x2, real y2, Params& arg) {
  const int n = 20;
  Poly curve0, curve1, curve2;
  for (int i = 0; i < n; i++) {
    real t = (real)i / (real)(n - 1);
    real a = PI / 2 * t;
    real x = x1 - spow(std::cos(a), 1.5_r) * (x1 - x0);
    real y = y0 - spow(std::sin(a), 1.5_r) * (y0 - y1);
    real dx = (noise(x * 0.01_r, y * 0.01_r, 9) * 40 - 20) * (1.01_r - t);
    real dy = (noise(x * 0.01_r, y * 0.01_r, 8) * 40 - 20) * (1.01_r - t);
    curve0.push_back(P(x + dx, y + dy));
  }
  for (int i = 0; i < n; i++) {
    real t = (real)i / (real)(n - 1);
    real a = PI / 2 * t;
    real x = x2 - spow(std::cos(a), 0.8_r) * (x2 - x0);
    real y = y0 + spow(std::sin(a), 1.5_r) * (y2 - y0);
    real dx = (noise(x * 0.01_r, y * 0.01_r, 9) * 40 - 20) * (1.01_r - t);
    real dy = (noise(x * 0.01_r, y * 0.01_r, 8) * 40 - 20) * (1.01_r - t);
    curve1.push_back(P(x + dx, y + dy));
  }
  curve1 = rev(curve1);  // the original builds it with unshift
  real ang = std::atan2(y2 - y1, x2 - x1);
  for (int i = 1; i < n - 1; i++) {
    real t = (real)i / (real)(n - 1);
    Pt p = lerp2d(P(x1, y1), P(x2, y2), t);
    real s = spow(std::sin(t * PI), 0.5_r);
    real r = noise(t * 2, 1.2_r) * s * 20;
    curve2.push_back(P(p.x + std::cos(ang - PI / 2) * r, p.y + std::sin(ang - PI / 2) * r));
  }
  Poly outline = concat(concat(curve0, curve2), curve1);

  Poly inl = concat(slice(curve2, (int)curve2.size() / 3), slice(curve1, 0, (int)curve1.size() / 2));
  inl = slice(inl, 0, (int)curve0.size());
  int in = (int)inl.size();
  for (int i = 0; i < in; i++) {
    real t = (real)i / (real)(in - 1);
    real sn = std::sin(t * PI);
    real s = sn * sn * 0.1_r + 0.12_r;
    inl[i] = lerp2d(inl[i], curve0[i], s);
  }
  real dix = (x0 - inl.back().x) * 0.3_r;
  real diy = (y0 - inl.back().y) * 0.2_r;
  for (int i = 0; i < in; i++) {
    inl[i].x += dix;
    inl[i].y += diy;
  }

  const real par0 = 0.475_r, par1 = 0.375_r;
  real ex = x0 * par0 + x1 * par1 + x2 * (1 - par0 - par1);
  real ey = y0 * par0 + y1 * par1 + y2 * (1 - par0 - par1);
  real d0 = pt_seg_dist(P(ex, ey), P(x0, y0), P(x1, y1));
  real d1 = pt_seg_dist(P(ex, ey), P(x0, y0), P(x2, y2));
  if (d0 < arg.eye_size && d1 < arg.eye_size) {
    arg.eye_size = std::min(d0, d1);
  } else if (d0 < arg.eye_size) {
    real ea = std::atan2(y1 - y0, x1 - x0) + PI / 2;
    ex = x0 * 0.5_r + x1 * 0.5_r + std::cos(ea) * arg.eye_size;
    ey = y0 * 0.5_r + y1 * 0.5_r + std::sin(ea) * arg.eye_size;
  }

  Pt jaw_pt0 = curve1[18 - arg.mouth_size];
  Pt c18 = curve1[18];
  real jaw_l = dist(jaw_pt0, c18) * arg.jaw_size;
  real jaw_ang0 = std::atan2(c18.y - jaw_pt0.y, c18.x - jaw_pt0.x);
  real jaw_ang = jaw_ang0 - (arg.has_teeth * 0.5_r + 0.5_r) * arg.jaw_open * PI / 4;
  Pt jaw_pt1 = P(jaw_pt0.x + std::cos(jaw_ang) * jaw_l, jaw_pt0.y + std::sin(jaw_ang) * jaw_l);

  Shape eye = arg.eye_type ? fish_eye_b(ex, ey, arg.eye_size) : fish_eye_a(ex, ey, arg.eye_size);
  const Poly& eye0 = eye.c;
  Polys ef = clip_multi(eye.f, outline, true);

  Polys inlines = clip1(inl, eye0, false);

  Poly lip0 = fish_lip(jaw_pt0.x, jaw_pt0.y, c18.x, c18.y, 3);
  Poly lip1 = fish_lip(jaw_pt0.x, jaw_pt0.y, jaw_pt1.x, jaw_pt1.y, 3);

  Pt jb = curve1[15 - arg.mouth_size];
  Polys jaw = fish_jaw(jb.x, jb.y, jaw_pt0.x, jaw_pt0.y, jaw_pt1.x, jaw_pt1.y);
  jaw = clip_multi(jaw, lip1, false);
  jaw = clip_multi(jaw, outline, false);

  Polys teeth0s, teeth1s;
  if (arg.has_teeth) {
    Polys teeth0 = fish_teeth(jaw_pt0.x, jaw_pt0.y, c18.x, c18.y, arg.teeth_length, -1, arg.teeth_space);
    Polys teeth1 = fish_teeth(jaw_pt0.x, jaw_pt0.y, jaw_pt1.x, jaw_pt1.y, arg.teeth_length, 1, arg.teeth_space);
    teeth0s = clip_multi(teeth0, lip0, false);
    teeth1s = clip_multi(teeth1, lip1, false);
  }

  Polys olines = clip1(outline, lip0, false);
  Polys lip0s = clip1(lip0, lip1, false);

  Polys sh = shade_shape(outline, 6, -6, -6);
  sh = clip_multi(sh, lip0, false);
  sh = clip_multi(sh, eye0, false);

  Polys sh2 = vein_shape(outline, arg.head_texture_amount);
  sh2 = clip_multi(sh2, lip0, false);
  sh2 = clip_multi(sh2, eye0, false);

  Polys bbs;
  Polys lip1s;
  lip1s.push_back(lip1);

  if (arg.has_moustache) {
    Poly bb0 = barbel(jaw_pt0.x, jaw_pt0.y, arg.moustache_length, PI * 3 / 4, 1.5_r);
    lip1s = clip1(lip1, bb0, false);
    jaw = clip_multi(jaw, bb0, false);
    bbs.push_back(bb0);
  }

  if (arg.has_beard) {
    Pt jaw_pt;
    if (!jaw.empty() && !jaw[0].empty()) {
      jaw_pt = jaw[0][jaw[0].size() / 2];
    } else {
      jaw_pt = curve1[8];
    }
    Poly bb[3];
    for (int k = 0; k < 3; k++) {
      real a = PI * 0.6_r + rnd() * 0.4_r - 0.2_r;
      Poly raw = barbel(jaw_pt.x, jaw_pt.y, arg.beard_length, a);
      real tx = rnd() * 1 - 0.5_r;
      real ty = rnd() * 1 - 0.5_r;
      bb[k] = trsl_poly(raw, tx, ty);
    }
    Polys bb3c = clip1(bb[2], bb[1], false);
    bb3c = clip_multi(bb3c, bb[0], false);
    Polys bb2c = clip1(bb[1], bb[0], false);
    bbs.push_back(bb[0]);
    bbs.insert(bbs.end(), bb2c.begin(), bb2c.end());
    bbs.insert(bbs.end(), bb3c.begin(), bb3c.end());
  }

  Shape res;
  res.c.push_back(P(0, 0));
  res.c.push_back(P(curve0.back().x, 0));
  res.c.push_back(curve0.back());
  res.c.insert(res.c.end(), curve2.begin(), curve2.end());
  res.c.push_back(curve1[0]);
  res.c.push_back(P(curve1[0].x, 300));
  res.c.push_back(P(0, 300));

  const Polys* parts[] = {&olines, &inlines, &lip0s, &lip1s, &ef, &sh, &sh2, &bbs, &teeth0s, &teeth1s, &jaw};
  for (size_t k = 0; k < sizeof(parts) / sizeof(parts[0]); k++) {
    res.f.insert(res.f.end(), parts[k]->begin(), parts[k]->end());
  }
  return res;
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

// Picks an index with the given weights (all 1 if weights is null).
int choice(int count, const uint8_t* weights = nullptr) {
  int s = 0;
  for (int i = 0; i < count; i++) s += weights ? weights[i] : 1;
  real r = rnd() * s;
  s = 0;
  for (int i = 0; i < count; i++) {
    s += weights ? weights[i] : 1;
    if (r <= s) return i;
  }
  return count - 1;
}

real rndtri(real a, real b, real c) {
  real s0 = (b - a) / 2;
  real s1 = (c - b) / 2;
  real s = s0 + s1;
  real r = rnd() * s;
  if (r < s0) {
    real d = std::sqrt(2 * r * (b - a));
    return a + d;
  }
  real d = std::sqrt(std::max(0.0_r, 2 * (s - r) * (c - b)));
  return c - d;
}

Params generate_params() {
  Params arg;
  memset(&arg, 0, sizeof(arg));
  arg.jaw_open = 1;
  arg.body_curve_type = choice(2);
  arg.body_curve_amount = rndtri(0.5_r, 0.85_r, 0.98_r);
  arg.body_length = rndtri(200, 350, 420);
  arg.body_height = rndtri(45, 90, 150);
  arg.scale_type = choice(4);
  arg.scale_scale = rndtri(0.8_r, 1, 1.5_r);
  arg.pattern_type = choice(5);
  arg.pattern_scale = rndtri(0.5_r, 1, 2);
  arg.dorsal_texture_type = choice(2);
  arg.dorsal_type = choice(2);
  arg.dorsal_length = rndtri(30, 90, 180);
  if (arg.dorsal_type == 0) {
    arg.dorsal_start = trunc_i(rndtri(7, 8, 15));
    arg.dorsal_end = trunc_i(rndtri(20, 27, 28));
  } else {
    arg.dorsal_start = trunc_i(rndtri(11, 12, 16));
    arg.dorsal_end = trunc_i(rndtri(19, 21, 24));
  }
  arg.wing_texture_type = choice(2);
  arg.wing_type = choice(2);
  if (arg.wing_type == 0) {
    arg.wing_length = rndtri(40, 130, 200);
  } else {
    arg.wing_length = rndtri(40, 150, 350);
  }
  if (arg.wing_texture_type == 0) {
    arg.wing_width = rndtri(7, 10, 20);
    arg.wing_y = rndtri(0.45_r, 0.7_r, 0.85_r);
  } else {
    arg.wing_width = rndtri(20, 30, 50);
    arg.wing_y = rndtri(0.45_r, 0.65_r, 0.75_r);
  }
  arg.wing_start = trunc_i(rndtri(5, 6, 8));
  arg.wing_end = trunc_i(rndtri(5, 6, 8));

  arg.pelvic_texture_type = arg.dorsal_texture_type ? choice(2) : 0;
  arg.pelvic_type = choice(2);
  arg.pelvic_length = rndtri(30, 85, 140);
  if (arg.pelvic_type == 0) {
    arg.pelvic_start = trunc_i(rndtri(7, 9, 11));
    arg.pelvic_end = trunc_i(rndtri(13, 14, 15));
  } else {
    arg.pelvic_start = trunc_i(rndtri(7, 9, 12));
    arg.pelvic_end = arg.pelvic_start + 2;
  }

  arg.anal_texture_type = arg.dorsal_texture_type ? choice(2) : 0;
  arg.anal_type = choice(2);
  arg.anal_length = rndtri(20, 50, 80);
  arg.anal_start = trunc_i(rndtri(16, 19, 23));
  arg.anal_end = trunc_i(rndtri(25, 29, 31));

  arg.tail_type = choice(6);
  arg.tail_length = rndtri(50, 75, 180);

  arg.finlet_type = choice(4);

  arg.neck_type = choice(2);
  arg.nose_height = rndtri(-50, 0, 35);
  arg.head_length = rndtri(20, 30, 35);
  arg.mouth_size = trunc_i(rndtri(6, 8, 11));

  arg.head_texture_amount = trunc_i(rndtri(30, 60, 160));
  arg.has_moustache = choice(4) == 3 ? 1 : 0;
  arg.has_beard = choice(6) == 5 ? 1 : 0;
  arg.moustache_length = trunc_i(rndtri(10, 20, 40));
  arg.beard_length = trunc_i(rndtri(20, 30, 50));

  arg.eye_type = choice(2);
  arg.eye_size = rndtri(8, 10, 28);

  arg.jaw_size = rndtri(0.7_r, 1, 1.4_r);

  arg.has_teeth = choice(3) >= 1 ? 1 : 0;
  arg.teeth_length = rndtri(5, 8, 15);
  arg.teeth_space = rndtri(3, 3.5_r, 6);
  return arg;
}

real bean(real x) {
  real v = 0.25_r - (x - 0.5_r) * (x - 0.5_r);
  return std::sqrt(std::max(0.0_r, v)) * (2.6_r + 2.4_r * ppow(x, 1.5_r)) * 0.542_r;
}

inline real deviate(real n) { return rnd() * 2 * n - n; }

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

// Maps drawing units to pixels and feeds the sink.
struct Painter {
  real bx, by, s, ox, oy;
  LineSink* sink;
  uint32_t segments;

  void draw(const Poly& p) {
    bool have = false;
    int lx = 0, ly = 0;
    for (size_t i = 0; i < p.size(); i++) {
      if (!(std::isfinite(p[i].x) && std::isfinite(p[i].y))) {
        have = false;
        continue;
      }
      int x = (int)std::lround((p[i].x - bx) * s + ox);
      int y = (int)std::lround((p[i].y - by) * s + oy);
      if (have) {
        sink->line(lx, ly, x, y);
        segments++;
      }
      lx = x;
      ly = y;
      have = true;
    }
  }
};

// ---------------------------------------------------------------------------
// The fish
// ---------------------------------------------------------------------------

void fish(Params& arg, const Box& box, LineSink& sink, Stats* stats) {
  init_units();
  const int n = 32;
  Poly curve0, curve1;
  if (arg.body_curve_type == 0) {
    real s = arg.body_curve_amount;
    for (int i = 0; i < n; i++) {
      real t = (real)i / (real)(n - 1);
      real x = 225 + (t - 0.5_r) * arg.body_length;
      real y = 150 - (std::sin(t * PI) * lerp(0.5_r, 1, noise(t * 2, 1)) * s + (1 - s)) * arg.body_height;
      curve0.push_back(P(x, y));
    }
    for (int i = 0; i < n; i++) {
      real t = (real)i / (real)(n - 1);
      real x = 225 + (t - 0.5_r) * arg.body_length;
      real y = 150 + (std::sin(t * PI) * lerp(0.5_r, 1, noise(t * 2, 2)) * s + (1 - s)) * arg.body_height;
      curve1.push_back(P(x, y));
    }
  } else {
    for (int i = 0; i < n; i++) {
      real t = (real)i / (real)(n - 1);
      real x = 225 + (t - 0.5_r) * arg.body_length;
      real y = 150 - lerp(1 - arg.body_curve_amount, 1, lerp(0, 1, noise(t * 1.2_r, 1)) * bean(1 - t)) * arg.body_height;
      curve0.push_back(P(x, y));
    }
    for (int i = 0; i < n; i++) {
      real t = (real)i / (real)(n - 1);
      real x = 225 + (t - 0.5_r) * arg.body_length;
      real y = 150 + lerp(1 - arg.body_curve_amount, 1, lerp(0, 1, noise(t * 1.2_r, 2)) * bean(1 - t)) * arg.body_height;
      curve1.push_back(P(x, y));
    }
  }
  Poly outline = concat(curve0, rev(curve1));

  S.begin(G_SH);
  S.add(shade_shape(outline, 8, -12, -12));
  S.end();

  Pattern pattern;
  pattern.type = arg.pattern_type;
  pattern.scale = arg.pattern_scale;
  if (arg.pattern_type == 1) {
    poissondisk(500, 300, 20 * arg.pattern_scale, pattern.samples);
    for (size_t i = 0; i < pattern.samples.size(); i++) {
      pattern.rs.push_back((rnd() * 5 + 10) * arg.pattern_scale);
    }
  }

  S.begin(G_BD);
  if (arg.scale_type == 0) {
    fish_body_a(curve0, curve1, arg.scale_scale, pattern);
  } else if (arg.scale_type == 1) {
    fish_body_b(curve0, curve1, arg.scale_scale, pattern);
  } else if (arg.scale_type == 2) {
    fish_body_c(curve0, curve1, arg.scale_scale);
  } else {
    fish_body_d(curve0, curve1, arg.scale_scale);
  }
  S.end();

  // Dorsal fin
  Poly c0;
  {
    real a0, a1, cv;
    Profile func;
    if (arg.dorsal_type == 0) {
      a0 = 0.2_r + deviate(0.05_r);
      a1 = 0.3_r + deviate(0.05_r);
      cv = 0;
      func = [&arg](real t) { return (0.3_r + noise(t * 3) * 0.7_r) * arg.dorsal_length * ppow(std::sin(t * PI), 0.5_r); };
    } else {
      a0 = 0.6_r + deviate(0.05_r);
      a1 = 0.3_r + deviate(0.05_r);
      cv = arg.dorsal_length / 8;
      func = [&arg](real t) { return arg.dorsal_length * ((t - 1) * (t - 1) * 0.5_r + (1 - t) * 0.5_r); };
    }
    Shape f0;
    if (arg.dorsal_texture_type == 0) {
      f0 = fin_a(resample(slice(curve0, arg.dorsal_start, arg.dorsal_end), 5), a0, a1, func, false, cv, 0);
    } else {
      f0 = fin_b(resample(slice(curve0, arg.dorsal_start, arg.dorsal_end), 15), a0, a1, func);
    }
    S.begin(G_F0);
    S.add(f0.f);
    S.end();
    c0.swap(f0.c);
  }

  // Pectoral fin ("wing")
  Poly c1;
  {
    Pt f1_pt = lerp2d(curve0[arg.wing_start], curve1[arg.wing_end], arg.wing_y);
    Poly f1_curve;
    for (int i = 0; i < 10; i++) {
      real t = (real)i / 9;
      real y = lerp(f1_pt.y - arg.wing_width / 2, f1_pt.y + arg.wing_width / 2, t);
      f1_curve.push_back(P(f1_pt.x, y));
    }
    real a0, a1, soft, cv;
    Profile func;
    if (arg.wing_type == 0) {
      a0 = -0.4_r + deviate(0.05_r);
      a1 = 0.4_r + deviate(0.05_r);
      soft = 10;
      cv = 0;
      func = [&arg](real t) { return (40 + (20 + noise(t * 3) * 70) * ppow(std::sin(t * PI), 0.5_r)) / 130 * arg.wing_length; };
    } else {
      a0 = 0 + deviate(0.05_r);
      a1 = 0.4_r + deviate(0.05_r);
      soft = 5;
      cv = arg.wing_length / 25;
      func = [&arg](real t) { return arg.wing_length * (1 - t * 0.95_r); };
    }
    Shape f1;
    if (arg.wing_texture_type == 0) {
      f1 = fin_a(resample(f1_curve, 1.5_r), a0, a1, func, true, cv, 0, soft);
    } else {
      f1 = fin_b(resample(f1_curve, 4), a0, a1, func, 0.3_r);
    }
    S.begin(G_F1);
    S.add(f1.f);
    S.end();
    c1.swap(f1.c);
  }

  // Pelvic fin
  {
    real a0, a1;
    Profile func;
    if (arg.pelvic_type == 0) {
      a0 = -0.8_r + deviate(0.05_r);
      a1 = -0.5_r + deviate(0.05_r);
      func = [&arg](real t) { return (10 + (15 + noise(t * 3) * 60) * ppow(std::sin(t * PI), 0.5_r)) / 85 * arg.pelvic_length; };
    } else {
      a0 = -0.9_r + deviate(0.05_r);
      a1 = -0.3_r + deviate(0.05_r);
      func = [&arg](real t) { return (t * 0.5_r + 0.5_r) * arg.pelvic_length; };
    }
    Poly base = rev(slice(curve1, arg.pelvic_start, arg.pelvic_end));
    Shape f2;
    if (arg.pelvic_texture_type == 0) {
      f2 = fin_a(resample(base, arg.pelvic_type ? 2 : 5), a0, a1, func);
    } else {
      f2 = fin_b(resample(base, arg.pelvic_type ? 2 : 15), a0, a1, func);
    }
    S.begin(G_F2);
    S.add(f2.f);
    S.end();
  }

  // Anal fin
  {
    real a0 = -0.4_r + deviate(0.05_r);
    real a1 = -0.4_r + deviate(0.05_r);
    Profile func;
    if (arg.anal_type == 0) {
      func = [&arg](real t) { return (10 + (10 + noise(t * 3) * 30) * ppow(std::sin(t * PI), 0.5_r)) / 50 * arg.anal_length; };
    } else {
      func = [&arg](real t) { return arg.anal_length * (t * t * 0.8_r + 0.2_r); };
    }
    Poly base = rev(slice(curve1, arg.anal_start, arg.anal_end));
    Shape f3;
    if (arg.anal_texture_type == 0) {
      f3 = fin_a(resample(base, 5), a0, a1, func);
    } else {
      f3 = fin_b(resample(base, 15), a0, a1, func);
    }
    S.begin(G_F3);
    S.add(f3.f);
    S.end();
  }

  // Tail
  Poly c4;
  {
    real f4_r = dist(curve0[n - 2], curve1[n - 2]);
    int f4_n = trunc_i(f4_r / 1.5_r);
    f4_n = std::max(std::min(f4_n, 20), 8);
    real f4_d = f4_r / f4_n;
    Poly base(2);
    Shape f4;
    real tl = arg.tail_length;
    if (arg.tail_type == 0) {
      base[0] = curve0[n - 1];
      base[1] = curve1[n - 1];
      f4 = fin_a(resample(base, f4_d), -0.6_r, 0.6_r,
                 [tl](real t) { return (75 - (10 + noise(t * 3) * 10) * std::sin(3 * t * PI - PI)) / 75 * tl; }, true);
    } else if (arg.tail_type == 1) {
      base[0] = curve0[n - 2];
      base[1] = curve1[n - 2];
      f4 = fin_a(resample(base, f4_d), -0.6_r, 0.6_r,
                 [tl](real t) { return tl * (std::sin(t * PI) * 0.5_r + 0.5_r); }, true);
    } else if (arg.tail_type == 2) {
      base[0] = curve0[n - 1];
      base[1] = curve1[n - 1];
      real cv = tl / 8;
      f4 = fin_a(resample(base, f4_d * 0.7_r), -0.6_r, 0.6_r,
                 [tl](real t) { return (std::fabs(std::cos(PI * t)) * 0.8_r + 0.2_r) * tl; }, true, cv, -cv);
    } else if (arg.tail_type == 3) {
      base[0] = curve0[n - 2];
      base[1] = curve1[n - 2];
      f4 = fin_a(resample(base, f4_d), -0.6_r, 0.6_r,
                 [tl](real t) { return (1 - std::sin(t * PI) * 0.3_r) * tl; }, true);
    } else if (arg.tail_type == 4) {
      base[0] = curve0[n - 2];
      base[1] = curve1[n - 2];
      f4 = fin_a(resample(base, f4_d), -0.6_r, 0.6_r,
                 [tl](real t) { return (1 - std::sin(t * PI) * 0.6_r) * (1 - t * 0.45_r) * tl; }, true);
    } else {
      base[0] = curve0[n - 2];
      base[1] = curve1[n - 2];
      f4 = fin_a(resample(base, f4_d), -0.6_r, 0.6_r,
                 [tl](real t) { return (1 - ppow(std::sin(t * PI), 0.4_r) * 0.55_r) * tl; }, true);
    }
    S.begin(G_F4);
    S.add(f4.f);
    S.end();
    c4.swap(f4.c);
  }

  // Finlets. `outline` may grow here; the dorsal fin is clipped by the body
  // as it was before.
  Poly outline0 = outline;
  {
    Shape f5;
    if (arg.finlet_type == 1) {
      Poly cv = resample(slice(curve0, arg.dorsal_end, -2), 5);
      f5 = finlet(cv, 5);
      cv = resample(rev(slice(curve1, arg.anal_end, -2)), 5);
      if (cv.size() > 1) f5 = finlet(cv, 5);
    } else if (arg.finlet_type == 2) {
      Poly cv = resample(slice(curve0, 27, 30), 5);
      f5 = fin_adipose(cv, 20, -5, 6);
      outline = poly_union(outline, trsl_poly(f5.c, 0, -1));
    } else if (arg.finlet_type == 3) {
      Poly cv = resample(slice(curve0, arg.dorsal_end + 2, -3), 5);
      if (cv.size() > 2) {
        f5 = fin_a(cv, 0.2_r, 0.3_r, [&arg](real t) {
          return (0.3_r + noise(t * 3) * 0.7_r) * arg.dorsal_length * 0.6_r * ppow(std::sin(t * PI), 0.5_r);
        });
      }
    }
    S.begin(G_F5);
    S.add(f5.f);
    S.end();
  }

  // Head
  Poly cf;
  {
    Shape fh;
    if (arg.neck_type == 0) {
      fh = fish_head(50 - arg.head_length, 150 + arg.nose_height, curve0[6].x, curve0[6].y, curve1[5].x, curve1[5].y, arg);
    } else {
      fh = fish_head(50 - arg.head_length, 150 + arg.nose_height, curve0[5].x, curve0[5].y, curve1[6].x, curve1[6].y, arg);
    }
    S.begin(G_FH);
    S.add(fh.f);
    S.end();
    cf.swap(fh.c);
  }

  // Everything that hides something else now exists: cut away what is hidden.
  Poly c4s = trsl_poly(c4, 1, 0);
  Poly outline0s = trsl_poly(outline0, 0, 0.001_r);
  Region r_c1(c1), r_cf(cf), r_c4(c4s), r_out(outline0s);
  {
    const Region* bd[] = {&r_c1, &r_c4, &r_cf};
    S.clip_group(G_BD, bd, 3);
    const Region* f0[] = {&r_out, &r_c1};
    S.clip_group(G_F0, f0, 2);
    const Region* f1[] = {&r_cf};
    S.clip_group(G_F1, f1, 1);
    const Region* by_wing[] = {&r_c1};
    S.clip_group(G_F2, by_wing, 1);
    S.clip_group(G_F3, by_wing, 1);
    S.clip_group(G_F4, by_wing, 1);
    const Region* sh[] = {&r_cf, &r_c1};
    S.clip_group(G_SH, sh, 2);
  }

  // Scale the drawing to fill the box. Pattern shading still to come lies
  // inside shapes already drawn, so it cannot change the bounds.
  Painter painter;
  painter.sink = &sink;
  painter.segments = 0;
  {
    real xmin = INFINITY, ymin = INFINITY, xmax = -INFINITY, ymax = -INFINITY;
    Poly p;
    size_t pos = 0;
    while (pos < S.len) {
      S.read(pos, p);
      for (size_t i = 0; i < p.size(); i++) {
        xmin = std::min(xmin, p[i].x);
        ymin = std::min(ymin, p[i].y);
        xmax = std::max(xmax, p[i].x);
        ymax = std::max(ymax, p[i].y);
      }
    }
    real bw = xmax - xmin, bh = ymax - ymin;
    if (!(bw > 0)) bw = 1;
    if (!(bh > 0)) bh = 1;
    real s = std::min(box.w / bw, box.h / bh);
    painter.bx = xmin;
    painter.by = ymin;
    painter.s = s;
    painter.ox = box.x + (box.w - bw * s) / 2;
    painter.oy = box.y + (box.h - bh * s) / 2;
    pos = 0;
    while (pos < S.len) {
      S.read(pos, p);
      painter.draw(p);
    }
  }
  if (stats) {
    stats->arena_peak = (uint32_t)(S.peak * 2);
    stats->arena_size = (uint32_t)(S.cap * 2);
    stats->overflow = S.overflow;
  }
  S.release();  // the patterns below need the room

  // Pattern shading, drawn as it is generated.
  if (pattern.active()) {
    Poly shape;
    real step;
    if (arg.scale_type > 1) {
      shape = poly_union(outline, trsl_poly(c0, 0, 3));
      step = 3.5_r;
    } else {
      shape = c0;
      step = 4.5_r;
    }
    if (!shape.empty()) {
      Region r_shape(shape);
      BBox bbox = get_bbox(shape);
      bbox.x -= step;
      bbox.y -= step;
      bbox.w += step * 2;
      bbox.h += step * 2;
      Polys inside, on, a, b;
      Poly l(2);
      for (real i = -bbox.h / 2; i < bbox.w; i += step) {
        l[0] = P(bbox.x + i, bbox.y);
        l[1] = P(bbox.x + i + bbox.h / 2, bbox.y + bbox.h);
        inside.clear();
        clip(l, r_shape, &inside, nullptr);
        for (size_t k = 0; k < inside.size(); k++) {
          on.clear();
          binclip(resample(inside[k], 2), [&pattern](real x, real y, real) { return pattern(x, y); }, &on, nullptr);
          a.clear();
          for (size_t u = 0; u < on.size(); u++) clip(on[u], r_cf, nullptr, &a);
          b.clear();
          for (size_t u = 0; u < a.size(); u++) clip(a[u], r_c1, nullptr, &b);
          for (size_t u = 0; u < b.size(); u++) painter.draw(b[u]);
        }
      }
    }
  }

  if (arg.pattern_type == 4) {
    Poly shape = poly_union(outline, trsl_poly(c0, 0, 5));
    if (!shape.empty()) {
      Region r_shape(shape);
      BBox bbox = get_bbox(shape);
      Poly samples;
      poissondisk(bbox.w, bbox.h, 5 * arg.pattern_scale, samples);
      Poly unit[2];
      for (int k = 0; k < 2; k++) {
        for (int j = 0; j < 7; j++) {
          real a = (real)j / 6 * PI * 2;
          unit[k].push_back(P(std::cos(a) * 1 - k * 0.3_r, std::sin(a) * 0.5_r - k * 0.3_r));
        }
      }
      Polys a, b, c;
      for (size_t i = 0; i < samples.size(); i++) {
        real x = samples[i].x + bbox.x, y = samples[i].y + bbox.y;
        real t = (y > 0) ? (y / 300) : 0.5_r;
        if ((t > 0.4_r || y < 0) && t > rnd()) continue;
        for (int k = 0; k < 2; k++) {
          Poly o = trsl_poly(rot_poly(unit[k], rnd() * PI * 2), x, y);
          a.clear();
          clip(o, r_shape, &a, nullptr);
          b.clear();
          for (size_t u = 0; u < a.size(); u++) clip(a[u], r_c1, nullptr, &b);
          c.clear();
          for (size_t u = 0; u < b.size(); u++) clip(b[u], r_cf, nullptr, &c);
          for (size_t u = 0; u < c.size(); u++) painter.draw(c[u]);
        }
      }
    }
  }

  if (stats) stats->segments = painter.segments;
}

// ---------------------------------------------------------------------------
// Names and text
// ---------------------------------------------------------------------------

uint32_t str_to_seed(const char* str) {
  uint32_t n = 1;
  for (size_t i = 0; str[i]; i++) {
    uint32_t x = (uint32_t)(unsigned char)str[i] + 1;
    n ^= x << (7 + (i % 5));
    n = xorshift(n);
    n = n % 4294967295u;
  }
  return n;
}

std::string binomen() {
  static const char* const* const DATA[6] = {BINOMEN_0, BINOMEN_1, BINOMEN_2, BINOMEN_3, BINOMEN_4, BINOMEN_5};
  static const uint8_t* const FREQ[6] = {BINOMEN_FREQ_0, BINOMEN_FREQ_1, BINOMEN_FREQ_2,
                                         BINOMEN_FREQ_3, BINOMEN_FREQ_4, BINOMEN_FREQ_5};
  auto pick = [&](int k) { return std::string(DATA[k][choice(BINOMEN_COUNT[k], FREQ[k])]); };
  std::string name = pick(0);
  int n = trunc_i(rnd() * 3);
  for (int i = 0; i < n; i++) name += pick(1);
  name += pick(2);
  name += ' ';
  name += pick(3);
  n = trunc_i(rnd() * 3);
  for (int i = 0; i < n; i++) name += pick(4);
  name += pick(5);
  // Collapse any letter repeated three or more times to two.
  std::string out;
  for (size_t i = 0; i < name.size(); i++) {
    size_t k = out.size();
    if (k >= 2 && out[k - 1] == name[i] && out[k - 2] == name[i] && name[i] >= 'A' && name[i] <= 'Z') continue;
    out += name[i];
  }
  for (size_t i = 1; i < out.size(); i++) {
    if (out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
  }
  return out;
}

const char* glyph_for(char ch) {
  if (ch >= 'A' && ch <= 'Z') return HERSHEY_UPPER[ch - 'A'];
  if (ch >= 'a' && ch <= 'z') return HERSHEY_LOWER[ch - 'a'];
  if (ch == '.') return HERSHEY_DOT;
  return nullptr;
}

}  // namespace

void make_name(const char* seed_text, uint32_t salt, char* out, size_t out_size) {
  uint32_t s = str_to_seed(seed_text);
  for (uint32_t i = 0; i < salt; i++) s = xorshift(s ^ 0x9E3779B9u);
  if (s == 0) s = 0x5EED;
  jsr = s;
  std::string name = binomen();
  if (!out_size) return;
  size_t n = std::min(name.size(), out_size - 1);
  memcpy(out, name.data(), n);
  out[n] = 0;
}

bool draw_fish(const char* name, const Box& box, LineSink& sink, Stats* stats) {
  if (stats) memset(stats, 0, sizeof(*stats));
  if (!S.init(FISHDRAW_ARENA_BYTES)) return false;
  jsr = str_to_seed(name);
  perlin.clear();
  Params arg = generate_params();
  fish(arg, box, sink, stats);
  S.release();
  std::vector<real>().swap(perlin);
  UVerts().is.swap(g_verts0.is);
  UVerts().is.swap(g_verts1.is);
  return true;
}

float text_width(const char* text, float scale) {
  float x = 0;
  for (size_t i = 0; text[i]; i++) {
    if (text[i] == ' ') {
      x += 10;
      continue;
    }
    const char* g = glyph_for(text[i]);
    if (!g) continue;
    x += (float)(g[4] - g[3]);
  }
  return x * scale;
}

void draw_text(const char* text, float x0, float baseline_y, float scale, float shear, LineSink& sink) {
  float x = 0;
  for (size_t i = 0; text[i]; i++) {
    if (text[i] == ' ') {
      x += 10;
      continue;
    }
    const char* g = glyph_for(text[i]);
    if (!g) continue;
    int xmin = g[3] - 'R', xmax = g[4] - 'R';
    bool have = false;
    int lx = 0, ly = 0;
    for (const char* c = g + 5; c[0] && c[1]; c += 2) {
      if (c[0] == ' ' && c[1] == 'R') {
        have = false;
        continue;
      }
      float gx = (float)(c[0] - 'R') + x - (float)xmin;
      float gy = (float)(c[1] - 'R');
      int px = (int)lroundf(x0 + (gx + gy * shear) * scale);
      int py = (int)lroundf(baseline_y + gy * scale);
      if (have) sink.line(lx, ly, px, py);
      lx = px;
      ly = py;
      have = true;
    }
    x += (float)(xmax - xmin);
  }
}

}  // namespace fishdraw
