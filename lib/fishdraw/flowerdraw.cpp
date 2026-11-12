// Procedurally generated flowers, drawn in the same single-line plotter style
// as the fish.
//
// This is not a port: fishdraw has no flowers. It is built from the same
// parts (the noise, clipping and hatching in fishdraw_core.inc, which are
// ported from fishdraw.js, Copyright (c) 2021 Lingdong Huang, MIT License).
//
// A drawing is two to five stems of one made-up species. Everything is
// added to a scene from the front to the back; each new shape has the parts
// hidden behind earlier shapes cut away, then becomes something that hides
// what comes after it.

#include "fishdraw.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef FISHDRAW_REAL
#define FISHDRAW_REAL float
#endif

// The project's settings, when built as part of the firmware or the preview.
#if __has_include("config.h")
#include "config.h"
#endif

// Flowers are optional and off unless the build asks for them.
#ifndef ENABLE_FLOWERS
#define ENABLE_FLOWERS 0
#endif

#if ENABLE_FLOWERS

// A page with fewer points than this is thought too bare, and is tried again.
#ifndef FLOWERDRAW_MIN_POINTS
#define FLOWERDRAW_MIN_POINTS 1100
#endif

namespace fishdraw {
namespace {

#include "fishdraw_core.inc"

// ---------------------------------------------------------------------------
// The made-up species
// ---------------------------------------------------------------------------

enum { HEAD_RADIAL, HEAD_CUP, HEAD_UMBEL, HEAD_DOUBLE, HEAD_SPIKE };
enum { DISC_SEEDS, DISC_HATCH, DISC_STAMENS };

struct Species {
  int head;
  Blade petal;
  int petals;      // per ring
  int rings;
  real disc_r;     // radius of the centre, as a fraction of the petal length
  int disc_style;
  bool alternate;  // every other petal tucked under its neighbours
  int florets;     // umbel: number of small flowers
  Blade leaf;
  int leaf_style;
  int leaves;
  real stem_w;
};

Species make_species() {
  Species sp;
  memset(&sp, 0, sizeof(sp));
  static const int HEAD_W[] = {6, 3, 1, 3, 2};
  sp.head = pick(HEAD_W, 5);
  Blade& p = sp.petal;
  p.L = 1;  // heads are built at unit petal length and scaled when placed
  p.midrib = true;

  if (sp.head == HEAD_RADIAL) {
    static const int COUNT_W[] = {3, 3, 2, 2, 2};
    static const int COUNTS[] = {5, 6, 8, 13, 21};
    sp.petals = COUNTS[pick(COUNT_W, 5)];
    if (chance(0.25_r)) sp.petals += ri(-1, 2);
    // Fewer petals are broad; many are narrow.
    real fill = rr(0.75_r, 1.5_r);
    p.W = std::min(0.5_r, fill * PI / sp.petals * rr(0.55_r, 0.75_r));
    p.pos = rr(0.35_r, 0.75_r);
    p.base = rr(0.1_r, 0.45_r);
    p.bend = chance(0.3_r) ? rr(-0.12_r, 0.12_r) : 0;
    p.wobble = rr(0.05_r, 0.3_r);
    static const int TIP_W[] = {4, 4, 2};
    p.tip = pick(TIP_W, 3);
    if (sp.petals > 9 && p.tip == TIP_NOTCHED) p.tip = TIP_ROUND;  // a notch needs a broad petal
    p.veins = sp.petals > 14 ? ri(0, 1) : ri(1, 3);
    sp.rings = sp.petals <= 8 ? ri(1, 2) : (chance(0.35_r) ? 2 : 1);
    sp.disc_r = sp.petals > 10 ? rr(0.28_r, 0.5_r) : rr(0.14_r, 0.3_r);
    static const int DISC_W[] = {4, 2, 3};
    sp.disc_style = pick(DISC_W, 3);
    sp.alternate = chance(0.5_r);
    if (sp.petals > 12) {
      // Many narrow petals: any texture on them just turns the head black.
      p.veins = 0;
      p.midrib = chance(0.35_r);
      sp.rings = 1;
    }
  } else if (sp.head == HEAD_DOUBLE) {
    // Rings of broad petals, small in the middle and large outside, like a
    // rose or a peony.
    sp.rings = ri(3, 4);
    sp.petals = ri(5, 8);
    p.W = rr(0.36_r, 0.5_r);
    p.pos = rr(0.5_r, 0.7_r);
    p.base = rr(0.3_r, 0.5_r);
    p.bend = 0;
    p.wobble = rr(0.1_r, 0.3_r);
    p.tip = chance(0.6_r) ? TIP_ROUND : TIP_NOTCHED;
    p.veins = ri(0, 1);
    p.midrib = chance(0.5_r);
    sp.disc_r = rr(0.1_r, 0.16_r);
    sp.disc_style = chance(0.5_r) ? DISC_STAMENS : DISC_SEEDS;
  } else if (sp.head == HEAD_SPIKE) {
    // Small bells up the top of the stem, like a foxglove or a hyacinth.
    sp.florets = ri(9, 16);
    sp.petals = 3;
    p.W = rr(0.22_r, 0.3_r);
    p.pos = rr(0.5_r, 0.65_r);
    p.base = rr(0.35_r, 0.5_r);
    p.bend = 0;
    p.wobble = 0.1_r;
    p.tip = chance(0.5_r) ? TIP_POINTED : TIP_ROUND;
    p.veins = 0;
    sp.rings = 1;
  } else if (sp.head == HEAD_CUP) {
    sp.petals = ri(3, 5);
    p.W = rr(0.27_r, 0.4_r);
    p.pos = rr(0.35_r, 0.55_r);
    p.base = rr(0.25_r, 0.5_r);
    p.bend = 0;
    p.wobble = rr(0.05_r, 0.2_r);
    p.tip = chance(0.65_r) ? TIP_POINTED : TIP_ROUND;
    p.veins = ri(2, 4);
    sp.rings = 1;
  } else {
    sp.florets = ri(16, 28);
    sp.petals = ri(4, 6);
    p.W = rr(0.3_r, 0.42_r);
    p.pos = rr(0.5_r, 0.7_r);
    p.base = rr(0.2_r, 0.4_r);
    p.bend = 0;
    p.wobble = 0.1_r;
    p.tip = chance(0.5_r) ? TIP_ROUND : TIP_NOTCHED;
    p.veins = 0;
    sp.rings = 1;
  }

  Blade& l = sp.leaf;
  sp.leaf_style = (sp.head == HEAD_CUP && chance(0.7_r)) || chance(0.2_r) ? 1 : 0;
  if (sp.leaf_style == 1) {
    l.L = rr(110, 170);
    l.W = l.L * rr(0.05_r, 0.09_r);
    l.pos = rr(0.3_r, 0.45_r);
    l.bend = rr(0.1_r, 0.3_r);
  } else {
    l.L = rr(55, 105);
    l.W = l.L * rr(0.13_r, 0.26_r);
    l.pos = rr(0.3_r, 0.5_r);
    l.bend = rr(0.05_r, 0.22_r);
  }
  l.base = 0.06_r;
  l.wobble = rr(0.1_r, 0.35_r);
  l.tip = TIP_POINTED;
  l.veins = ri(4, 7);
  sp.leaves = ri(2, 4);
  sp.stem_w = rr(1.6_r, 3);
  return sp;
}

// ---------------------------------------------------------------------------
// Flower heads. Built around (0,0) with petals one unit long; `f` places them.
// ---------------------------------------------------------------------------

void add_disc(Scene& sc, const Species& sp, const Frame& f, real id) {
  real r0 = sp.disc_r;
  if (sp.disc_style == DISC_STAMENS) {
    // A small centre with stamens standing out of it.
    int k = ri(7, 16);
    Polys filaments;
    for (int i = 0; i < k; i++) {
      real a = (real)i / k * PI * 2 + rr(-0.2_r, 0.2_r);
      real r = r0 * rr(0.75_r, 1.5_r);
      Pt tip = P(std::cos(a) * r, std::sin(a) * r);
      sc.add(f.map(circle(tip.x, tip.y, 0.035_r, 7, 0, id)));
      Poly fl;
      for (int j = 0; j <= 4; j++) {
        real t = (real)j / 4;
        real sway = std::sin(t * PI) * 0.03_r;
        fl.push_back(P(tip.x * t - std::sin(a) * sway, tip.y * t + std::cos(a) * sway));
      }
      filaments.push_back(fl);
    }
    sc.add(f.map(filaments), nullptr);
    sc.add(f.map(circle(0, 0, r0 * 0.4_r, 14, 0.2_r, id)));
    return;
  }

  Poly outline = circle(0, 0, r0, 30, 0.12_r, id);
  Polys lines;
  Poly loop = outline;
  loop.push_back(loop[0]);
  lines.push_back(loop);
  if (sp.disc_style == DISC_SEEDS) {
    // Seeds in the spiral that sunflowers use.
    int k = std::max(12, trunc_i(r0 * r0 * 900));
    if (k > 160) k = 160;
    for (int i = 1; i <= k; i++) {
      real r = r0 * 0.92_r * std::sqrt((real)i / k);
      real a = i * 2.39996323_r;
      Pt c = P(std::cos(a) * r, std::sin(a) * r);
      Poly tick(2);
      real d = 0.012_r + 0.01_r * (real)i / k;
      tick[0] = P(c.x - std::cos(a) * d, c.y - std::sin(a) * d);
      tick[1] = P(c.x + std::cos(a) * d, c.y + std::sin(a) * d);
      lines.push_back(tick);
    }
  } else {
    Poly inner = circle(0, 0, r0 * 0.55_r, 18, 0.15_r, id + 3);
    Poly il = inner;
    il.push_back(il[0]);
    lines.push_back(il);
    Polys hatch = shade_shape(outline, r0 * 0.16_r, -r0 * 0.5_r, -r0 * 0.5_r);
    lines.insert(lines.end(), hatch.begin(), hatch.end());
  }
  sc.add(f.map(lines), nullptr);
  Poly mapped = f.map(outline);
  sc.add(Polys(), &mapped);
}

void add_radial_head(Scene& sc, const Species& sp, const Frame& f, real id) {
  add_disc(sc, sp, f, id);
  for (int ring = 0; ring < sp.rings; ring++) {
    Blade b = sp.petal;
    b.L = 1 - ring * 0.04_r + (ring ? 0.1_r : 0);
    real offset = rr(0, PI * 2) + ring * PI / sp.petals;
    real base_r = sp.disc_r * 0.55_r;
    // Passes: all petals in turn, or the even ones first and the odd ones
    // tucked beneath them.
    int passes = sp.alternate && sp.petals % 2 == 0 ? 2 : 1;
    for (int pass = 0; pass < passes; pass++) {
      for (int i = pass; i < sp.petals; i += passes) {
        real a = offset + (real)i / sp.petals * PI * 2 + rr(-0.05_r, 0.05_r);
        Blade bi = b;
        bi.L *= rr(0.92_r, 1.06_r);
        Pt o = P(std::cos(a) * base_r, std::sin(a) * base_r);
        BladeShape s = make_blade(bi, o, a, id + ring * 40 + i);
        Poly outline = f.map(s.outline);
        sc.add(f.map(s.lines), &outline);
      }
    }
  }
}

void add_double_head(Scene& sc, const Species& sp, const Frame& f, real id) {
  add_disc(sc, sp, f, id);
  for (int ring = 0; ring < sp.rings; ring++) {
    real frac = sp.rings > 1 ? (real)ring / (sp.rings - 1) : 1;
    Blade b = sp.petal;
    b.L = 0.42_r + 0.58_r * frac;
    int n = sp.petals + ring;
    // Wide enough that neighbours overlap all the way round.
    b.W = std::max(sp.petal.W * b.L * 1.1_r, 2.3_r * b.L / n);
    real offset = rr(0, PI * 2);
    real base_r = sp.disc_r * 0.5_r;
    for (int i = 0; i < n; i++) {
      real a = offset + (real)i / n * PI * 2 + rr(-0.06_r, 0.06_r);
      Blade bi = b;
      bi.L *= rr(0.93_r, 1.07_r);
      BladeShape s = make_blade(bi, P(std::cos(a) * base_r, std::sin(a) * base_r), a, id + ring * 40 + i, 10);
      Poly outline = f.map(s.outline);
      sc.add(f.map(s.lines), &outline);
    }
  }
}

// Seen from the side: a front petal, then the ones behind fanned out.
// `spread_scale` below 1 closes it up, as for a bud; `n` is the blade detail.
void add_cup_head(Scene& sc, const Species& sp, const Frame& f, real id, real spread_scale = 1, int n = 13) {
  const real up = -PI / 2;
  real spread = rr(0.22_r, 0.42_r) * spread_scale;
  int order = 0;
  for (int i = 0; i < sp.petals; i++) {
    // 0, +1, -1, +2, -2 ... steps of the fan, front to back.
    int step = (i + 1) / 2;
    int side = (i % 2) ? 1 : -1;
    real a = up + side * step * spread + rr(-0.04_r, 0.04_r);
    Blade b = sp.petal;
    b.L *= 1 + step * 0.03_r;
    b.bend = -side * step * 0.06_r;
    BladeShape s = make_blade(b, P(0, 0), a, id + order++, n);
    Poly outline = f.map(s.outline);
    sc.add(f.map(s.lines), &outline);
  }
}

void add_umbel_head(Scene& sc, const Species& sp, const Frame& f, real id) {
  // Small flowers scattered over a dome, each on its own stalk.
  Poly centres;
  real min_d = 1.55_r / std::sqrt((real)sp.florets);
  for (int tries = 0; tries < 600 && (int)centres.size() < sp.florets; tries++) {
    real a = rr(0, PI * 2), r = std::sqrt(rnd());
    Pt c = P(std::cos(a) * r, std::sin(a) * r * 0.62_r - 0.1_r);
    bool ok = true;
    for (size_t k = 0; k < centres.size(); k++) {
      if (dist(c, centres[k]) < min_d) ok = false;
    }
    if (ok) centres.push_back(c);
  }
  // Lowest first: those are nearest the viewer.
  std::sort(centres.begin(), centres.end(), [](const Pt& a, const Pt& b) { return a.y > b.y; });
  real fr = min_d * 0.62_r;
  for (size_t k = 0; k < centres.size(); k++) {
    const Pt& c = centres[k];
    size_t before = sc.occluders.size();
    sc.add(f.map(circle(c.x, c.y, fr * 0.2_r, 7, 0, id)));
    real off = rr(0, PI * 2);
    for (int i = 0; i < sp.petals; i++) {
      real a = off + (real)i / sp.petals * PI * 2;
      Blade b = sp.petal;
      b.L = fr;
      b.W = sp.petal.W * fr;
      BladeShape s = make_blade(b, P(c.x + std::cos(a) * fr * 0.1_r, c.y + std::sin(a) * fr * 0.1_r), a,
                                id + k * 9 + i, 6);
      Poly outline = f.map(s.outline);
      sc.add(f.map(s.lines), &outline);
    }
    // Its petals have hidden each other; from here on one round shape stands
    // in for the whole floret, which keeps hundreds of outlines out of memory.
    sc.drop_occluders_after(before);
    Poly disc = f.map(circle(c.x, c.y, fr * 0.97_r, 12, 0, id));
    sc.add(Polys(), &disc);
  }
  Pt hub = P(0, 0.95_r);
  Polys stalks;
  for (size_t k = 0; k < centres.size(); k++) {
    Poly st;
    Pt mid = P((hub.x + centres[k].x) / 2 + centres[k].x * 0.12_r, (hub.y + centres[k].y) / 2 + 0.08_r);
    for (int j = 0; j <= 6; j++) {
      real t = (real)j / 6;
      st.push_back(P((1 - t) * (1 - t) * hub.x + 2 * t * (1 - t) * mid.x + t * t * centres[k].x,
                     (1 - t) * (1 - t) * hub.y + 2 * t * (1 - t) * mid.y + t * t * centres[k].y));
    }
    stalks.push_back(st);
  }
  sc.add(f.map(stalks), nullptr);
}

// ---------------------------------------------------------------------------
// One flower: head, buds, leaves, stem
// ---------------------------------------------------------------------------

// A point part-way along a polyline of evenly spread points, and the
// direction of travel there.
Pt point_on(const Poly& path, real t, real* tangent) {
  int n = (int)path.size() - 1;
  real x = t * n;
  int i = std::min(n - 1, std::max(0, trunc_i(x)));
  real f = x - i;
  int a = std::max(0, i - 1), b = std::min(n, i + 2);
  if (tangent) *tangent = std::atan2(path[b].y - path[a].y, path[b].x - path[a].x);
  return lerp2d(path[i], path[i + 1], f);
}

// A closed bud on its own short stalk, branching from `from`.
void add_bud(Scene& sc, const Species& sp, const Pt& from, int side, real size, real id) {
  Pt end = P(from.x + side * size * rr(0.45_r, 0.7_r), from.y - size * rr(0.55_r, 0.85_r));
  Species closed = sp;
  closed.petals = 3;
  closed.petal.L = 1;
  closed.petal.W = std::min(0.3_r, std::max(0.2_r, sp.petal.W * 0.7_r));
  closed.petal.pos = 0.45_r;
  closed.petal.base = 0.4_r;
  closed.petal.tip = TIP_POINTED;
  closed.petal.veins = 0;
  closed.petal.midrib = true;
  Frame f;
  f.cx = end.x;
  f.cy = end.y;
  f.scale = size * 0.42_r;
  f.squash = 1;
  f.rot = side * rr(0.1_r, 0.45_r);
  add_cup_head(sc, closed, f, id, 0.45_r, 8);
  Polys stalk(1);
  Pt ctrl = P(from.x + side * size * 0.1_r, end.y + size * 0.15_r);
  for (int j = 0; j <= 8; j++) {
    real t = (real)j / 8;
    stalk[0].push_back(P((1 - t) * (1 - t) * from.x + 2 * t * (1 - t) * ctrl.x + t * t * end.x,
                         (1 - t) * (1 - t) * from.y + 2 * t * (1 - t) * ctrl.y + t * t * end.y));
  }
  sc.add(stalk, nullptr);
}

// Small bells up the top half of the stem, alternating sides, smaller towards
// the tip.
void add_spike(Scene& sc, const Species& sp, const Poly& mid, real size, real id) {
  int count = sp.florets;
  for (int k = 0; k < count; k++) {
    real u = count > 1 ? (real)k / (count - 1) : 0;
    real tang;
    Pt at = point_on(mid, lerp(0.46_r, 0.97_r, u), &tang);
    int side = (k % 2) ? 1 : -1;
    // Out from the stem and hanging a little.
    real dir = tang + side * rr(1.15_r, 1.5_r);
    Frame f;
    f.scale = size * lerp(0.36_r, 0.17_r, u);
    f.cx = at.x + std::cos(dir) * f.scale * 0.2_r;
    f.cy = at.y + std::sin(dir) * f.scale * 0.2_r;
    f.squash = 1;
    f.rot = dir + PI / 2;
    add_cup_head(sc, sp, f, id + k * 5, 1.7_r, 7);
  }
  // A closed tip.
  real tang;
  Pt top = point_on(mid, 1, &tang);
  Blade tip = sp.petal;
  tip.L = size * 0.2_r;
  tip.W = tip.L * 0.28_r;
  tip.tip = TIP_POINTED;
  BladeShape s = make_blade(tip, top, tang, id + 900, 6);
  sc.add(s.lines, &s.outline);
}

void add_flower(Scene& sc, const Species& sp, const Pt& base, const Pt& head, real head_size, real id) {
  Frame f;
  f.cx = head.x;
  f.cy = head.y;
  f.scale = head_size;
  f.rot = rr(-0.25_r, 0.25_r) + (head.x - base.x) * 0.004_r;
  f.squash = sp.head == HEAD_RADIAL ? rr(0.62_r, 1) : (sp.head == HEAD_DOUBLE ? rr(0.75_r, 1) : 1);

  // The stem: a curve from the ground to where it meets the head.
  Pt top = sp.head == HEAD_UMBEL ? f.map(P(0, 0.95_r)) : head;
  Pt ctrl = P(lerp(base.x, top.x, 0.35_r) + rr(-25, 25), lerp(base.y, top.y, 0.55_r));
  const int n = 26;
  Poly mid;
  for (int i = 0; i <= n; i++) {
    real t = (real)i / n;
    mid.push_back(P((1 - t) * (1 - t) * base.x + 2 * t * (1 - t) * ctrl.x + t * t * top.x,
                    (1 - t) * (1 - t) * base.y + 2 * t * (1 - t) * ctrl.y + t * t * top.y));
  }

  // Front to back: the head, a bud, the leaves, then the stem behind it all.
  if (sp.head == HEAD_RADIAL) {
    add_radial_head(sc, sp, f, id);
  } else if (sp.head == HEAD_DOUBLE) {
    add_double_head(sc, sp, f, id);
  } else if (sp.head == HEAD_CUP) {
    add_cup_head(sc, sp, f, id);
  } else if (sp.head == HEAD_UMBEL) {
    add_umbel_head(sc, sp, f, id);
  } else {
    add_spike(sc, sp, mid, head_size, id);
  }

  int side = chance(0.5_r) ? 1 : -1;
  bool spike = sp.head == HEAD_SPIKE;
  if (!spike && sp.head != HEAD_UMBEL && chance(0.45_r)) {
    add_bud(sc, sp, point_on(mid, rr(0.5_r, 0.72_r), nullptr), side, head_size * rr(0.8_r, 1.1_r), id + 300);
  }

  // Leaves, on alternate sides. A spike keeps its leaves below the flowers.
  for (int k = 0; k < sp.leaves; k++) {
    real tang;
    Pt at = point_on(mid, spike ? rr(0.08_r, 0.4_r) : rr(0.1_r, 0.62_r), &tang);
    Blade b = sp.leaf;
    b.L *= rr(0.75_r, 1.1_r);
    b.W *= rr(0.8_r, 1.1_r);
    // Leaves open away from the stem and curl over.
    real open = sp.leaf_style == 1 ? rr(0.2_r, 0.5_r) : rr(0.6_r, 1.1_r);
    b.bend = std::fabs(b.bend) * side;
    BladeShape s = make_leaf(b, at, tang + side * open, id + 70 + k, sp.leaf_style);
    sc.add(s.lines, &s.outline);
    side = -side;
  }

  Poly left, right;
  for (int i = 0; i <= n; i++) {
    int a = std::max(0, i - 1), b = std::min(n, i + 1);
    real tang = std::atan2(mid[b].y - mid[a].y, mid[b].x - mid[a].x);
    real w = sp.stem_w * (1.15_r - 0.3_r * (real)i / n);
    left.push_back(P(mid[i].x + std::cos(tang - PI / 2) * w, mid[i].y + std::sin(tang - PI / 2) * w));
    right.push_back(P(mid[i].x + std::cos(tang + PI / 2) * w, mid[i].y + std::sin(tang + PI / 2) * w));
  }
  Polys edges;
  edges.push_back(left);
  edges.push_back(right);
  Poly outline = left;
  for (int i = n; i >= 0; i--) outline.push_back(right[i]);
  sc.add(edges, &outline);
}

// Fills the scene with a bunch of one freshly made-up species. The page is
// 500 x 300 units, like the fish. Stems stand on the bottom edge and fan
// outwards; shorter ones stand in front.
void compose(Scene& sc) {
  Species sp = make_species();
  static const int STEMS_W[] = {3, 5, 3, 1};
  int stems = pick(STEMS_W, 4) + 2;  // 2 to 5
  if (sp.head == HEAD_UMBEL && stems > 3) stems = 3;

  real spacing = rr(80, 112);
  real size = sp.head == HEAD_CUP     ? rr(52, 68)
              : sp.head == HEAD_UMBEL ? rr(50, 64)
              : sp.head == HEAD_SPIKE ? rr(60, 80)
              : sp.head == HEAD_DOUBLE ? rr(44, 60)
                                       : rr(40, 62);
  if (stems >= 4) size *= 0.9_r;
  real tall = rr(190, 228);

  struct Stem {
    Pt base, head;
    real size;
  };
  Stem st[5];
  for (int i = 0; i < stems; i++) {
    real off = i - (stems - 1) / 2.0_r;
    real h = tall * (1 - 0.15_r * std::fabs(off)) * rr(0.86_r, 1.06_r);
    st[i].base = P(250 + off * spacing * 0.45_r + rr(-8, 8), 300);
    st[i].head = P(250 + off * spacing + rr(-12, 12), 300 - h);
    st[i].size = size * rr(0.82_r, 1.02_r);
  }
  // Shortest first: those are nearest the viewer.
  std::sort(st, st + stems, [](const Stem& a, const Stem& b) { return a.head.y > b.head.y; });
  for (int k = 0; k < stems; k++) add_flower(sc, sp, st[k].base, st[k].head, st[k].size, 11 + k * 131);
}

}  // namespace

bool draw_flower(const char* name, const Box& box, LineSink& sink, Stats* stats) {
  if (stats) memset(stats, 0, sizeof(*stats));
  jsr = seed_from(name);
  perlin.clear();
  init_trig();

  // Now and then a species comes out thin: a couple of stems of something
  // simple. Try again, a few times, for a page with more on it.
  Scene sc;
  for (int attempt = 0; attempt < 4; attempt++) {
    sc = Scene();
    sc.lines.reserve(1024);
    compose(sc);
    if (sc.points >= FLOWERDRAW_MIN_POINTS) break;
  }

  emit_scene(sc, box, sink, stats);
  std::vector<real>().swap(perlin);
  return true;
}

}  // namespace fishdraw

#else  // !ENABLE_FLOWERS

namespace fishdraw {
bool draw_flower(const char*, const Box&, LineSink&, Stats*) { return false; }
}  // namespace fishdraw

#endif  // ENABLE_FLOWERS
