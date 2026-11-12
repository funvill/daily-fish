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

// Upper bound on the points kept for one drawing.
#ifndef FLOWERDRAW_MAX_POINTS
#define FLOWERDRAW_MAX_POINTS 14000
#endif

// A page with fewer points than this is thought too bare, and is tried again.
#ifndef FLOWERDRAW_MIN_POINTS
#define FLOWERDRAW_MIN_POINTS 1100
#endif

namespace fishdraw {
namespace {

#include "fishdraw_core.inc"

// ---------------------------------------------------------------------------
// Random helpers
// ---------------------------------------------------------------------------

inline real rr(real a, real b) { return a + rnd() * (b - a); }

inline int ri(int a, int b) {  // inclusive
  int v = a + trunc_i(rnd() * (b - a + 1));
  return v > b ? b : v;
}

inline bool chance(real p) { return rnd() < p; }

// Picks an index with the given weights.
int pick(const int* weights, int count) {
  int s = 0;
  for (int i = 0; i < count; i++) s += weights[i];
  real r = rnd() * s;
  s = 0;
  for (int i = 0; i < count; i++) {
    s += weights[i];
    if (r <= s) return i;
  }
  return count - 1;
}

uint32_t seed_from(const char* str) {
  uint32_t n = 1;
  for (size_t i = 0; str[i]; i++) {
    uint32_t x = (uint32_t)(unsigned char)str[i] + 1;
    n ^= x << (7 + (i % 5));
    n = xorshift(n);
    n = n % 4294967295u;
  }
  return n ? n : 0x5EED;
}

// ---------------------------------------------------------------------------
// The scene: lines, and the shapes that hide lines behind them
// ---------------------------------------------------------------------------

struct Occluder {
  Poly p;
  real x0, y0, x1, y1;
};

struct Scene {
  Polys lines;
  std::vector<Occluder> occluders;
  size_t points;           // points held in `lines`
  size_t occluder_points;  // points held in `occluders`
  size_t peak;             // most points held at once, both kinds
  bool full;

  Scene() : points(0), occluder_points(0), peak(0), full(false) {}

  // Forgets the shapes added since there were `count` of them. Used to swap
  // the many small shapes of one floret for a single simple one.
  void drop_occluders_after(size_t count) {
    while (occluders.size() > count) {
      occluder_points -= occluders.back().p.size();
      occluders.pop_back();
    }
  }

  // Adds lines, keeping only what is not behind a shape already in the scene.
  // If `outline` is given, it then hides whatever is added later.
  void add(const Polys& ls, const Poly* outline) {
    Polys cur, nxt;
    for (size_t i = 0; i < ls.size(); i++) {
      if (ls[i].size() < 2) continue;
      cur.assign(1, ls[i]);
      for (size_t k = 0; k < occluders.size() && !cur.empty(); k++) {
        const Occluder& o = occluders[k];
        nxt.clear();
        for (size_t c = 0; c < cur.size(); c++) {
          real bx0 = INFINITY, by0 = INFINITY, bx1 = -INFINITY, by1 = -INFINITY;
          for (size_t j = 0; j < cur[c].size(); j++) {
            bx0 = std::min(bx0, cur[c][j].x);
            by0 = std::min(by0, cur[c][j].y);
            bx1 = std::max(bx1, cur[c][j].x);
            by1 = std::max(by1, cur[c][j].y);
          }
          if (bx1 < o.x0 || bx0 > o.x1 || by1 < o.y0 || by0 > o.y1) {
            nxt.push_back(cur[c]);
          } else {
            Region rg(o.p);
            clip(cur[c], rg, nullptr, &nxt);
          }
        }
        cur.swap(nxt);
      }
      for (size_t c = 0; c < cur.size(); c++) {
        if (cur[c].size() < 2) continue;
        if (points + occluder_points + cur[c].size() > FLOWERDRAW_MAX_POINTS) {
          full = true;
          continue;
        }
        points += cur[c].size();
        lines.push_back(cur[c]);
      }
    }
    if (outline && outline->size() >= 3 && points + occluder_points + outline->size() > FLOWERDRAW_MAX_POINTS) {
      full = true;
    } else if (outline && outline->size() >= 3) {
      Occluder o;
      o.p = *outline;
      o.x0 = o.y0 = INFINITY;
      o.x1 = o.y1 = -INFINITY;
      for (size_t j = 0; j < o.p.size(); j++) {
        o.x0 = std::min(o.x0, o.p[j].x);
        o.y0 = std::min(o.y0, o.p[j].y);
        o.x1 = std::max(o.x1, o.p[j].x);
        o.y1 = std::max(o.y1, o.p[j].y);
      }
      occluder_points += o.p.size();
      occluders.push_back(o);
    }
    if (points + occluder_points > peak) peak = points + occluder_points;
  }

  void add(const Poly& closed) {
    Polys ls;
    Poly loop = closed;
    if (!loop.empty()) loop.push_back(loop[0]);
    ls.push_back(loop);
    add(ls, &closed);
  }
};

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------

// Where a flower head sits: scaled, squashed (a head tilted away from the
// viewer), turned, and moved.
struct Frame {
  real cx, cy, scale, squash, rot;
  Pt map(const Pt& p) const {
    real x = p.x * scale, y = p.y * scale * squash;
    real c = std::cos(rot), s = std::sin(rot);
    return P(cx + x * c - y * s, cy + x * s + y * c);
  }
  Poly map(const Poly& p) const {
    Poly q(p.size());
    for (size_t i = 0; i < p.size(); i++) q[i] = map(p[i]);
    return q;
  }
  Polys map(const Polys& ps) const {
    Polys q(ps.size());
    for (size_t i = 0; i < ps.size(); i++) q[i] = map(ps[i]);
    return q;
  }
};

// A point given as (across, along) a blade that starts at `o` and points in
// direction `ang`.
inline Pt along(const Pt& o, real ang, real u, real v) {
  real c = std::cos(ang), s = std::sin(ang);
  return P(o.x + v * c - u * s, o.y + v * s + u * c);
}

// ---------------------------------------------------------------------------
// Petals and leaves: one blade shape, many proportions
// ---------------------------------------------------------------------------

enum { TIP_ROUND, TIP_POINTED, TIP_NOTCHED };

struct Blade {
  real L;      // length
  real W;      // half width at the widest point
  real pos;    // where along the length the widest point is, 0..1
  real base;   // width at the base, as a fraction of W
  real bend;   // sideways curl, as a fraction of L
  real wobble; // unevenness of the edge
  int tip;
  int veins;   // lines of texture each side of the middle
  bool midrib; // a line down the middle
};

real blade_width(const Blade& b, real t) {
  real s;
  if (t <= b.pos) {
    s = ppow(std::sin(PI / 2 * t / b.pos), 0.8_r);
  } else {
    real u = (t - b.pos) / (1 - b.pos);
    if (b.tip == TIP_POINTED) {
      s = ppow(std::cos(PI / 2 * u), 1.4_r);
    } else if (b.tip == TIP_NOTCHED) {
      s = ppow(std::cos(PI / 2 * u * 0.78_r), 0.5_r);
    } else {
      s = ppow(std::cos(PI / 2 * u), 0.5_r);
    }
  }
  return (b.base + (1 - b.base) * s) * b.W;
}

inline real blade_mid(const Blade& b, real t) { return b.bend * b.L * t * t; }

struct BladeShape {
  Poly outline;  // closed
  Polys lines;   // outline (as a loop) and texture
};

// Builds a blade at `o` pointing along `ang`. `id` varies the wobble from one
// blade to the next; `n` is the number of steps along each edge.
BladeShape make_blade(const Blade& b, const Pt& o, real ang, real id, int n = 13) {
  BladeShape res;
  Poly left, right;
  for (int i = 0; i <= n; i++) {
    real t = (real)i / n;
    real w = blade_width(b, t);
    real m = blade_mid(b, t);
    real jl = (noise(t * 3.1_r, id * 1.7_r + 0.3_r) - 0.5_r) * b.wobble * b.W * t * 2;
    real jr = (noise(t * 3.1_r, id * 1.7_r + 9.1_r) - 0.5_r) * b.wobble * b.W * t * 2;
    left.push_back(along(o, ang, m - w + jl, t * b.L));
    right.push_back(along(o, ang, m + w + jr, t * b.L));
  }
  res.outline = left;
  if (b.tip == TIP_NOTCHED) {
    // Two rounded lobes with a dip between them.
    real m = blade_mid(b, 1), w = blade_width(b, 1);
    res.outline.push_back(along(o, ang, m - w * 0.62_r, b.L * 1.035_r));
    res.outline.push_back(along(o, ang, m - w * 0.2_r, b.L * 0.99_r));
    res.outline.push_back(along(o, ang, m, b.L * 0.92_r));
    res.outline.push_back(along(o, ang, m + w * 0.2_r, b.L * 0.99_r));
    res.outline.push_back(along(o, ang, m + w * 0.62_r, b.L * 1.035_r));
  }
  for (int i = n; i >= 0; i--) res.outline.push_back(right[i]);

  Poly loop = res.outline;
  loop.push_back(loop[0]);
  res.lines.push_back(loop);

  for (int k = -b.veins; k <= b.veins; k++) {
    if (k == 0 && !b.midrib) continue;
    real f = b.veins ? (real)k / (b.veins + 1) : 0;
    real t0 = k == 0 ? 0.04_r : rr(0.05_r, 0.3_r);
    real t1 = k == 0 ? rr(0.6_r, 0.85_r) : rr(0.45_r, 0.9_r);
    if (b.veins == 0 && k == 0 && b.L < 12) continue;  // too small to bother
    Poly v;
    const int m = 8;
    for (int i = 0; i <= m; i++) {
      real t = lerp(t0, t1, (real)i / m);
      v.push_back(along(o, ang, blade_mid(b, t) + f * blade_width(b, t), t * b.L));
    }
    res.lines.push_back(v);
  }
  return res;
}

// A leaf: a pointed blade with a midrib and side veins that run out to the edge.
BladeShape make_leaf(const Blade& b, const Pt& o, real ang, real id, int style) {
  Blade plain = b;
  plain.veins = 0;
  plain.midrib = true;
  BladeShape res = make_blade(plain, o, ang, id);
  if (style == 1) {
    // Strap leaf: long lines running the length.
    for (int k = -1; k <= 1; k += 2) {
      Poly v;
      for (int i = 0; i <= 10; i++) {
        real t = lerp(0.08_r, rr(0.6_r, 0.85_r), (real)i / 10);
        v.push_back(along(o, ang, blade_mid(b, t) + k * 0.4_r * blade_width(b, t), t * b.L));
      }
      res.lines.push_back(v);
    }
    return res;
  }
  int m = b.veins;
  for (int side = -1; side <= 1; side += 2) {
    for (int k = 1; k <= m; k++) {
      real t = (real)k / (m + 1) * 0.86_r + rr(-0.02_r, 0.02_r);
      Pt a = along(o, ang, blade_mid(b, t), t * b.L);
      // Out and towards the tip, curving a little.
      Poly v;
      v.push_back(a);
      real reach = b.W * 1.6_r;
      v.push_back(along(o, ang, blade_mid(b, t) + side * reach * 0.5_r, t * b.L + reach * 0.42_r));
      v.push_back(along(o, ang, blade_mid(b, t) + side * reach, t * b.L + reach * 0.7_r));
      Polys in = clip1(v, res.outline, true);
      if (!in.empty()) res.lines.push_back(in[0]);
    }
  }
  return res;
}

Poly circle(real cx, real cy, real r, int n, real wob, real id) {
  Poly p;
  for (int i = 0; i < n; i++) {
    real a = (real)i / n * PI * 2;
    real rr2 = r * (1 + (noise(std::cos(a) + 2, std::sin(a) + 2, id) - 0.5_r) * wob);
    p.push_back(P(cx + std::cos(a) * rr2, cy + std::sin(a) * rr2));
  }
  return p;
}

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

  // Scale to fill the box.
  real xmin = INFINITY, ymin = INFINITY, xmax = -INFINITY, ymax = -INFINITY;
  for (size_t i = 0; i < sc.lines.size(); i++) {
    for (size_t j = 0; j < sc.lines[i].size(); j++) {
      const Pt& p = sc.lines[i][j];
      if (!(std::isfinite(p.x) && std::isfinite(p.y))) continue;
      xmin = std::min(xmin, p.x);
      ymin = std::min(ymin, p.y);
      xmax = std::max(xmax, p.x);
      ymax = std::max(ymax, p.y);
    }
  }
  real bw = xmax - xmin, bh = ymax - ymin;
  if (!(bw > 0)) bw = 1;
  if (!(bh > 0)) bh = 1;
  real s = std::min(box.w / bw, box.h / bh);
  real ox = box.x + (box.w - bw * s) / 2, oy = box.y + (box.h - bh * s) / 2;
  uint32_t segments = 0;
  for (size_t i = 0; i < sc.lines.size(); i++) {
    bool have = false;
    int lx = 0, ly = 0;
    for (size_t j = 0; j < sc.lines[i].size(); j++) {
      const Pt& p = sc.lines[i][j];
      if (!(std::isfinite(p.x) && std::isfinite(p.y))) {
        have = false;
        continue;
      }
      int x = (int)std::lround((p.x - xmin) * s + ox);
      int y = (int)std::lround((p.y - ymin) * s + oy);
      if (have) {
        sink.line(lx, ly, x, y);
        segments++;
      }
      lx = x;
      ly = y;
      have = true;
    }
  }
  if (stats) {
    stats->segments = segments;
    stats->arena_peak = (uint32_t)(sc.peak * sizeof(Pt));
    stats->arena_size = (uint32_t)(FLOWERDRAW_MAX_POINTS * sizeof(Pt));
    stats->overflow = sc.full;
  }
  std::vector<real>().swap(perlin);
  return true;
}

}  // namespace fishdraw
