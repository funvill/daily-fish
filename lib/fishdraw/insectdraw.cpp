// Procedurally generated insects, seen from above like pinned specimens, in
// the same single-line plotter style as the fish.
//
// This is not a port: fishdraw has no insects. It is built from the same
// parts (the noise, clipping and hatching in fishdraw_core.inc, which are
// ported from fishdraw.js, Copyright (c) 2021 Lingdong Huang, MIT License).
//
// An insect is drawn head up and is the same on both sides: every part is
// made for the right-hand side and mirrored. Parts go into the scene from the
// top of the insect down, so each hides whatever lies beneath it: the body
// first, then wing covers or wings, then the legs.

#include "fishdraw.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#ifndef FISHDRAW_REAL
#define FISHDRAW_REAL float
#endif

// The project's settings, when built as part of the firmware or the preview.
#if __has_include("config.h")
#include "config.h"
#endif

#ifndef ENABLE_INSECTS
#define ENABLE_INSECTS 1
#endif

#if ENABLE_INSECTS

namespace fishdraw {
namespace {

#include "fishdraw_core.inc"

typedef std::function<real(real)> Profile;

inline real deg(real d) { return d * PI / 180; }

Poly closed(const Poly& p) {
  Poly l = p;
  if (!l.empty()) l.push_back(l[0]);
  return l;
}

Poly mirrored(const Poly& p) {
  Poly q(p.size());
  for (size_t i = 0; i < p.size(); i++) q[i] = P(-p[i].x, p[i].y);
  return q;
}

Polys mirrored(const Polys& ps) {
  Polys q(ps.size());
  for (size_t i = 0; i < ps.size(); i++) q[i] = mirrored(ps[i]);
  return q;
}

// Adds a shape made for the right-hand side, and its mirror image.
void add_both(Scene& sc, const Polys& lines, const Poly* outline) {
  sc.add(lines, outline);
  Polys ml = mirrored(lines);
  if (outline) {
    Poly mo = mirrored(*outline);
    sc.add(ml, &mo);
  } else {
    sc.add(ml, nullptr);
  }
}

// ---------------------------------------------------------------------------
// Body parts: symmetrical outlines from a half-width profile
// ---------------------------------------------------------------------------

// A rounded lump, 0 at both ends. `skew` below 1 puts the widest point
// nearer the front; a small `power` makes it blunt, a large one pointed.
inline real lobe(real t, real skew, real power) { return ppow(std::sin(PI * ppow(t, skew)), power); }

// A shield: `front` and `rear` are the widths at the ends as fractions of
// the widest point, with the corners rounded off.
inline real shield(real t, real front, real rear) {
  real f = t < 0.65_r ? lerp(front, 1, std::sin(PI / 2 * t / 0.65_r)) : lerp(1, rear, (t - 0.65_r) / 0.35_r);
  return f * ppow(std::sin(PI * t), 0.22_r);
}

struct Part {
  real y0, y1;
  Profile w;  // half width at t, 0 at the front to 1 at the back
  Poly outline;
  real y_at(real t) const { return lerp(y0, y1, t); }
};

Part make_part(real y0, real y1, Profile w, real id, int n = 18) {
  Part p;
  p.y0 = y0;
  p.y1 = y1;
  p.w = w;
  Poly right, left;
  for (int i = 0; i <= n; i++) {
    real t = (real)i / n;
    // The same small wobble on both sides keeps it symmetrical but hand-drawn.
    real ww = w(t) * (1 + (noise(t * 2.7_r, id) - 0.5_r) * 0.08_r);
    real y = lerp(y0, y1, t);
    right.push_back(P(ww, y));
    left.push_back(P(-ww, y));
  }
  p.outline = right;
  for (int i = n; i >= 0; i--) p.outline.push_back(left[i]);
  return p;
}

// Adds a part with lines of texture inside it. A `shade` above zero hatches
// the side away from the light, with lines that far apart.
void add_part(Scene& sc, const Part& part, const Polys& inside, real shade = 0) {
  Polys lines;
  lines.push_back(closed(part.outline));
  Region rg(part.outline);
  for (size_t i = 0; i < inside.size(); i++) clip(inside[i], rg, &lines, nullptr);
  if (shade > 0) {
    BBox bb = get_bbox(part.outline);
    real d = std::min(bb.w, bb.h) * 0.3_r;
    Polys sh = shade_shape(part.outline, shade, -d, -d);
    lines.insert(lines.end(), sh.begin(), sh.end());
  }
  sc.add(lines, &part.outline);
}

// A line across the part at t, sagging towards the back by `sag`.
Poly arc_across(const Part& part, real t, real sag) {
  real w = part.w(t) * 1.05_r, y = part.y_at(t);
  Poly a;
  for (int i = -8; i <= 8; i++) {
    real f = (real)i / 8;
    a.push_back(P(w * f, y + sag * (1 - f * f)));
  }
  return a;
}

// The joints between `count` segments.
Polys segments(const Part& part, int count, real sag, real t0 = 0, real t1 = 1) {
  Polys out;
  for (int k = 1; k < count; k++) out.push_back(arc_across(part, lerp(t0, t1, (real)k / count), sag));
  return out;
}

// Dark bands on alternate segments.
Polys stripes(const Part& part, int count, real sag, real step) {
  Polys out;
  for (int k = 0; k < count; k += 2) {
    Poly a = arc_across(part, (real)k / count + 0.02_r, sag);
    Poly b = arc_across(part, (real)(k + 1) / count, sag);
    Poly band = a;
    for (size_t i = b.size(); i-- > 0;) band.push_back(b[i]);
    Polys h = fill_shape(band, step);
    out.insert(out.end(), h.begin(), h.end());
  }
  return out;
}

// ---------------------------------------------------------------------------
// Legs, antennae, jaws
// ---------------------------------------------------------------------------

struct Leg {
  real a1, a2, a3;  // directions of thigh, shin and foot, for the right side
  real l1, l2, l3;  // their lengths
  real w;           // thickness of the thigh
};

// A tapered limb segment with a rounded end.
Poly limb(const Pt& a, const Pt& b, real w0, real w1) {
  real ang = std::atan2(b.y - a.y, b.x - a.x);
  real nx = std::cos(ang + PI / 2), ny = std::sin(ang + PI / 2);
  Poly p;
  p.push_back(P(a.x + nx * w0, a.y + ny * w0));
  p.push_back(P(b.x + nx * w1, b.y + ny * w1));
  p.push_back(P(b.x + std::cos(ang) * w1, b.y + std::sin(ang) * w1));
  p.push_back(P(b.x - nx * w1, b.y - ny * w1));
  p.push_back(P(a.x - nx * w0, a.y - ny * w0));
  return p;
}

void add_legs(Scene& sc, const Pt& at, const Leg& g) {
  Pt p1 = P(at.x + std::cos(g.a1) * g.l1, at.y + std::sin(g.a1) * g.l1);
  Pt p2 = P(p1.x + std::cos(g.a2) * g.l2, p1.y + std::sin(g.a2) * g.l2);
  Pt p3 = P(p2.x + std::cos(g.a3) * g.l3, p2.y + std::sin(g.a3) * g.l3);

  Poly thigh = limb(at, p1, g.w, g.w * 0.75_r);
  Poly shin = limb(p1, p2, g.w * 0.55_r, g.w * 0.35_r);
  Polys t(1, closed(thigh)), s(1, closed(shin));
  add_both(sc, t, &thigh);
  add_both(sc, s, &shin);

  // The foot: a thin jointed line ending in a pair of claws.
  Polys foot;
  Poly line;
  const int joints = 4;
  for (int i = 0; i <= joints; i++) line.push_back(lerp2d(p2, p3, (real)i / joints));
  foot.push_back(line);
  for (int side = -1; side <= 1; side += 2) {
    Poly claw(2);
    claw[0] = p3;
    claw[1] = P(p3.x + std::cos(g.a3 + side * 0.7_r) * g.w * 0.8_r, p3.y + std::sin(g.a3 + side * 0.7_r) * g.w * 0.8_r);
    foot.push_back(claw);
  }
  for (int i = 1; i < joints; i++) {
    Pt c = lerp2d(p2, p3, (real)i / joints);
    Poly tick(2);
    tick[0] = P(c.x + std::cos(g.a3 + PI / 2) * g.w * 0.3_r, c.y + std::sin(g.a3 + PI / 2) * g.w * 0.3_r);
    tick[1] = P(c.x - std::cos(g.a3 + PI / 2) * g.w * 0.3_r, c.y - std::sin(g.a3 + PI / 2) * g.w * 0.3_r);
    foot.push_back(tick);
  }
  add_both(sc, foot, nullptr);
}

// Three pairs of legs: the front pair reaches forwards, the middle pair
// sideways, the hind pair back. `reach` scales the length, `w` the thickness.
void add_six_legs(Scene& sc, const Pt& front, const Pt& mid, const Pt& hind, real reach, real w) {
  Leg g;
  g.w = w;
  g.a1 = deg(rr(-58, -40));
  g.a2 = g.a1 - deg(rr(20, 45));
  g.a3 = g.a2 + deg(rr(10, 35));
  g.l1 = reach * rr(0.8_r, 1);
  g.l2 = reach * rr(0.8_r, 1);
  g.l3 = reach * rr(0.5_r, 0.7_r);
  add_legs(sc, front, g);

  g.a1 = deg(rr(-12, 10));
  g.a2 = g.a1 + deg(rr(30, 60));
  g.a3 = g.a2 - deg(rr(10, 30));
  g.l1 = reach * rr(0.9_r, 1.1_r);
  g.l2 = reach * rr(0.9_r, 1.1_r);
  g.l3 = reach * rr(0.5_r, 0.7_r);
  add_legs(sc, mid, g);

  g.a1 = deg(rr(25, 45));
  g.a2 = g.a1 + deg(rr(25, 50));
  g.a3 = g.a2 - deg(rr(5, 25));
  g.l1 = reach * rr(1, 1.25_r);
  g.l2 = reach * rr(1.05_r, 1.35_r);
  g.l3 = reach * rr(0.55_r, 0.75_r);
  add_legs(sc, hind, g);
}

enum { ANT_THREAD, ANT_CLUB, ANT_FEATHER, ANT_ELBOW, ANT_LONG, ANT_FAN };

// A curve that starts at `at` heading `ang` and turns by `turn` over `len`.
Poly curl(const Pt& at, real ang, real len, real turn, int n = 12) {
  Poly p;
  Pt c = at;
  p.push_back(c);
  for (int i = 0; i < n; i++) {
    real a = ang + turn * (real)i / n;
    c = P(c.x + std::cos(a) * len / n, c.y + std::sin(a) * len / n);
    p.push_back(c);
  }
  return p;
}

void add_antennae(Scene& sc, const Pt& at, int kind, real len) {
  Polys lines;
  if (kind == ANT_ELBOW) {
    // A straight first joint, then a sharp bend.
    real a = deg(rr(-35, -15));
    Pt elbow = P(at.x + std::cos(a) * len * 0.4_r, at.y + std::sin(a) * len * 0.4_r);
    Poly first(2);
    first[0] = at;
    first[1] = elbow;
    lines.push_back(first);
    lines.push_back(curl(elbow, a - deg(rr(35, 55)), len * 0.6_r, deg(rr(10, 40))));
    add_both(sc, lines, nullptr);
    return;
  }
  real start = kind == ANT_LONG ? deg(rr(-70, -50)) : deg(rr(-80, -55));
  real turn = kind == ANT_LONG ? deg(rr(110, 150)) : deg(rr(15, 70));
  Poly stem = curl(at, start, len, turn, kind == ANT_LONG ? 24 : 12);
  Pt tip = stem.back();
  if (kind == ANT_CLUB) {
    Poly knob = circle(tip.x, tip.y, len * 0.07_r + 1.2_r, 9, 0, 1);
    Polys k(1, closed(knob));
    add_both(sc, k, &knob);
  }
  lines.push_back(stem);
  if (kind == ANT_THREAD || kind == ANT_LONG) {
    // Little marks for the joints.
    for (size_t i = 2; i + 1 < stem.size(); i += 2) {
      real a = std::atan2(stem[i + 1].y - stem[i - 1].y, stem[i + 1].x - stem[i - 1].x) + PI / 2;
      Poly tick(2);
      tick[0] = P(stem[i].x + std::cos(a) * 1.3_r, stem[i].y + std::sin(a) * 1.3_r);
      tick[1] = P(stem[i].x - std::cos(a) * 1.3_r, stem[i].y - std::sin(a) * 1.3_r);
      lines.push_back(tick);
    }
  } else if (kind == ANT_FEATHER) {
    for (size_t i = 1; i + 1 < stem.size(); i++) {
      real a = std::atan2(stem[i + 1].y - stem[i - 1].y, stem[i + 1].x - stem[i - 1].x);
      real l = len * 0.22_r * std::sin(PI * (real)i / stem.size());
      for (int side = -1; side <= 1; side += 2) {
        Poly barb(2);
        barb[0] = stem[i];
        barb[1] = P(stem[i].x + std::cos(a + side * deg(60)) * l, stem[i].y + std::sin(a + side * deg(60)) * l);
        lines.push_back(barb);
      }
    }
  } else if (kind == ANT_FAN) {
    // A few flat plates fanned out at the tip.
    real a = std::atan2(tip.y - stem[stem.size() - 2].y, tip.x - stem[stem.size() - 2].x);
    for (int k = -1; k <= 1; k++) {
      Poly plate(2);
      plate[0] = tip;
      plate[1] = P(tip.x + std::cos(a + k * deg(22)) * len * 0.3_r, tip.y + std::sin(a + k * deg(22)) * len * 0.3_r);
      lines.push_back(plate);
    }
  }
  add_both(sc, lines, nullptr);
}

// Jaws: a pair of curved horns reaching forwards from the front of the head.
void add_jaws(Scene& sc, const Pt& at, real len, bool toothed) {
  Blade b;
  memset(&b, 0, sizeof(b));
  b.L = len;
  b.W = std::max(1.6_r, len * 0.13_r);
  b.pos = 0.3_r;
  b.base = 0.7_r;
  b.bend = -0.28_r;  // curl inwards
  b.tip = TIP_POINTED;
  BladeShape s = make_blade(b, at, deg(rr(-82, -66)), 3, 9);
  if (toothed) {
    // One tooth on the inner edge.
    Pt m = along(at, deg(-74), blade_mid(b, 0.55_r) - blade_width(b, 0.55_r), len * 0.55_r);
    Poly tooth(3);
    tooth[0] = P(m.x + 1, m.y + len * 0.07_r);
    tooth[1] = P(m.x - len * 0.13_r, m.y);
    tooth[2] = P(m.x + 1, m.y - len * 0.07_r);
    s.lines.push_back(tooth);
  }
  add_both(sc, s.lines, &s.outline);
}

void add_eyes(Scene& sc, real x, real y, real r) {
  Poly eye = circle(x, y, r, 12, 0, 5);
  Polys lines(1, closed(eye));
  lines.push_back(closed(circle(x, y, r * 0.45_r, 8, 0, 6)));
  add_both(sc, lines, &eye);
}

// ---------------------------------------------------------------------------
// Wings
// ---------------------------------------------------------------------------

struct WingStyle {
  int veins;      // veins fanning out from the base
  bool border;    // a dark band along the outer edge
  int spots;
  bool rings;     // spots are ringed, like eyes
  bool ladder;    // short cross veins, as on a dragonfly
  bool solid;     // hides what is beneath it; clear wings do not
};

// A pair of wings. `b` and `ang` describe the right-hand one.
void add_wings(Scene& sc, const Blade& b, const Pt& base, real ang, const WingStyle& st, real id) {
  Blade plain = b;
  plain.veins = 0;
  plain.midrib = false;
  BladeShape s = make_blade(plain, base, ang, id, 30);
  Region wing(s.outline);

  // Spots sit on top of the wing's other markings.
  for (int k = 0; k < st.spots; k++) {
    real t = rr(0.45_r, 0.78_r), f = rr(-0.4_r, 0.4_r);
    Pt c = along(base, ang, blade_mid(b, t) + f * blade_width(b, t), t * b.L);
    real r = blade_width(b, t) * rr(0.16_r, 0.28_r);
    Poly spot = circle(c.x, c.y, r, 12, 0.1_r, id + k);
    Polys lines;
    clip(closed(spot), wing, &lines, nullptr);
    if (st.rings) {
      clip(closed(circle(c.x, c.y, r * 0.55_r, 9, 0, id)), wing, &lines, nullptr);
    } else {
      Polys h = fill_shape(spot, 2.6_r);
      for (size_t i = 0; i < h.size(); i++) clip(h[i], wing, &lines, nullptr);
    }
    if (st.solid) {
      add_both(sc, lines, &spot);
    } else {
      add_both(sc, lines, nullptr);
    }
  }

  Polys lines;
  lines.push_back(closed(s.outline));
  int n = (int)s.outline.size();
  // Veins run from the base to points spread round the far edge.
  for (int k = 0; k < st.veins; k++) {
    real u = (real)(k + 1) / (st.veins + 1);
    int i = std::min(n - 1, std::max(0, trunc_i(lerp(n * 0.2_r, n * 0.8_r, u))));
    Pt far = lerp2d(base, s.outline[i], 0.97_r);
    Pt mid = lerp2d(base, far, 0.5_r);
    mid.x += (noise(k * 1.3_r, id) - 0.5_r) * b.W * 0.25_r;
    mid.y += (noise(k * 1.3_r, id + 4) - 0.5_r) * b.W * 0.25_r;
    Poly v;
    for (int j = 0; j <= 8; j++) {
      real t = (real)j / 8;
      v.push_back(P((1 - t) * (1 - t) * base.x + 2 * t * (1 - t) * mid.x + t * t * far.x,
                    (1 - t) * (1 - t) * base.y + 2 * t * (1 - t) * mid.y + t * t * far.y));
    }
    clip(v, wing, &lines, nullptr);
  }
  if (st.ladder) {
    // Long veins down the wing, with rungs between them.
    for (int k = -1; k <= 1; k++) {
      Poly v;
      for (int j = 1; j <= 10; j++) {
        real t = 0.08_r + 0.86_r * (real)j / 10;
        v.push_back(along(base, ang, blade_mid(b, t) + k * 0.42_r * blade_width(b, t), t * b.L));
      }
      lines.push_back(v);
    }
    int rungs = std::max(6, trunc_i(b.L / 7));
    for (int j = 1; j < rungs; j++) {
      if (chance(0.2_r)) continue;
      real t = (real)j / rungs;
      Poly r(2);
      r[0] = along(base, ang, blade_mid(b, t) - blade_width(b, t), t * b.L);
      r[1] = along(base, ang, blade_mid(b, t) + blade_width(b, t), t * b.L);
      clip(r, wing, &lines, nullptr);
    }
    // The dark cell near the tip of the leading edge.
    Poly cell;
    for (int j = 0; j < 2; j++) {
      real t = 0.82_r + 0.07_r * j;
      cell.push_back(along(base, ang, blade_mid(b, t) - blade_width(b, t), t * b.L));
    }
    for (int j = 1; j >= 0; j--) {
      real t = 0.82_r + 0.07_r * j;
      cell.push_back(along(base, ang, blade_mid(b, t) - 0.45_r * blade_width(b, t), t * b.L));
    }
    Polys h = fill_shape(cell, 1.6_r);
    lines.insert(lines.end(), h.begin(), h.end());
  }
  if (st.border) {
    // The wing's own shape, shrunk towards the base, marks off a border,
    // and the border is hatched.
    Poly inner(s.outline.size());
    for (size_t i = 0; i < s.outline.size(); i++) inner[i] = lerp2d(base, s.outline[i], 0.84_r);
    clip(closed(inner), wing, &lines, nullptr);
    Polys h = fill_shape(s.outline, 3.4_r);
    Polys edge = clip_multi(h, inner, false);
    lines.insert(lines.end(), edge.begin(), edge.end());
  }
  add_both(sc, lines, st.solid ? &s.outline : nullptr);
}

// ---------------------------------------------------------------------------
// The kinds of insect
// ---------------------------------------------------------------------------

enum { BEETLE_GROUND, BEETLE_LADYBIRD, BEETLE_STAG, BEETLE_LONGHORN, BEETLE_SCARAB };

void build_beetle(Scene& sc) {
  static const int KIND_W[] = {4, 2, 2, 2, 2};
  int kind = pick(KIND_W, 5);

  real ew = rr(38, 56), el = rr(105, 140);  // wing covers: half width, length
  real pl = rr(34, 52);                     // length of the shield behind the head
  if (kind == BEETLE_LADYBIRD) {
    ew = rr(58, 70);
    el = rr(100, 118);
    pl = rr(22, 30);
  } else if (kind == BEETLE_LONGHORN) {
    ew = rr(26, 34);
    el = rr(130, 160);
  } else if (kind == BEETLE_SCARAB) {
    ew = rr(52, 64);
    el = rr(95, 120);
  }
  real pw = kind == BEETLE_LADYBIRD ? ew * rr(0.55_r, 0.65_r) : ew * rr(0.72_r, 0.95_r);
  real hw = kind == BEETLE_STAG ? pw * rr(0.75_r, 0.9_r) : pw * rr(0.48_r, 0.68_r);
  real hl = kind == BEETLE_STAG ? rr(30, 38) : rr(22, 32);

  real front = rr(0.55_r, 0.8_r), rear = rr(0.8_r, 0.95_r);
  Part shield_part = make_part(-pl, 3, [=](real t) { return pw * shield(t, front, rear); }, 1);
  Part head = make_part(-pl - hl * 0.78_r, -pl + hl * 0.22_r, [=](real t) { return hw * lobe(t, 1, 0.5_r); }, 2, 12);
  real skew = rr(0.6_r, 0.85_r), power = kind == BEETLE_LADYBIRD ? 0.5_r : rr(0.45_r, 0.7_r);
  Part covers = make_part(-3, el, [=](real t) { return ew * lobe(0.04_r + 0.96_r * t, skew, power); }, 3, 22);

  // From the top down: the shield, the head it overlaps, the wing covers.
  Polys none;
  Polys sh = shade_shape(shield_part.outline, 5, -7, -7);
  add_part(sc, shield_part, sh);
  add_eyes(sc, hw * 0.82_r, head.y_at(0.55_r), hw * 0.24_r);
  add_part(sc, head, none, 3.5_r);
  Pt jaw = P(hw * 0.4_r, head.y_at(0.12_r));
  if (kind == BEETLE_STAG) {
    add_jaws(sc, jaw, rr(45, 75), true);
  } else {
    add_jaws(sc, jaw, rr(8, 14), false);
  }
  Pt ant = P(hw * 0.7_r, head.y_at(0.3_r));
  if (kind == BEETLE_LONGHORN) {
    add_antennae(sc, ant, ANT_LONG, el * rr(1.1_r, 1.6_r));
  } else if (kind == BEETLE_SCARAB || kind == BEETLE_STAG) {
    add_antennae(sc, ant, kind == BEETLE_STAG ? ANT_ELBOW : ANT_FAN, rr(24, 34));
  } else if (kind == BEETLE_LADYBIRD) {
    add_antennae(sc, ant, ANT_CLUB, rr(14, 20));
  } else {
    add_antennae(sc, ant, chance(0.5_r) ? ANT_THREAD : ANT_CLUB, rr(30, 55));
  }

  Polys tex;
  // The seam down the middle, and the small triangle where the covers meet.
  Poly seam(2);
  seam[0] = P(0, 8);
  seam[1] = P(0, el);
  tex.push_back(seam);
  Poly tri(3);
  tri[0] = P(-5, 0);
  tri[1] = P(0, 11);
  tri[2] = P(5, 0);
  tex.push_back(tri);

  // Spots hide the other markings, so they go in first.
  if (kind == BEETLE_LADYBIRD || (kind == BEETLE_GROUND && chance(0.25_r))) {
    int spots = ri(2, 4);
    Region rg(covers.outline);
    for (int k = 0; k < spots; k++) {
      real t = rr(0.15_r, 0.8_r);
      real x = covers.w(t) * rr(0.3_r, 0.62_r), y = covers.y_at(t);
      real r = ew * rr(0.12_r, 0.2_r);
      Poly spot = circle(x, y, r, 12, 0.12_r, 20 + k);
      Polys lines;
      clip(closed(spot), rg, &lines, nullptr);
      Polys h = fill_shape(spot, 2.4_r);
      for (size_t i = 0; i < h.size(); i++) clip(h[i], rg, &lines, nullptr);
      add_both(sc, lines, &spot);
    }
  } else {
    // Ridges running the length of each cover.
    int ridges = kind == BEETLE_SCARAB ? ri(4, 6) : ri(2, 5);
    bool dotted = chance(0.3_r);
    for (int side = -1; side <= 1; side += 2) {
      for (int k = 1; k <= ridges; k++) {
        real f = (real)k / (ridges + 1);
        Poly ridge;
        for (int j = 0; j <= 16; j++) {
          real t = 0.07_r + 0.86_r * (real)j / 16;
          ridge.push_back(P(side * covers.w(t) * f, covers.y_at(t)));
        }
        if (dotted) {
          for (size_t j = 0; j + 1 < ridge.size(); j += 2) {
            Poly dash(2);
            dash[0] = ridge[j];
            dash[1] = lerp2d(ridge[j], ridge[j + 1], 0.45_r);
            tex.push_back(dash);
          }
        } else {
          tex.push_back(ridge);
        }
      }
    }
    Polys shade = shade_shape(covers.outline, 6, -9, -9);
    tex.insert(tex.end(), shade.begin(), shade.end());
  }
  add_part(sc, covers, tex);

  real reach = (kind == BEETLE_SCARAB ? 0.75_r : kind == BEETLE_LONGHORN ? 1.5_r : 1) * ew * rr(0.85_r, 1.1_r);
  add_six_legs(sc, P(pw * 0.6_r, -pl * 0.45_r), P(covers.w(0.12_r) * 0.7_r, covers.y_at(0.12_r)),
               P(covers.w(0.36_r) * 0.7_r, covers.y_at(0.36_r)), reach, kind == BEETLE_SCARAB ? rr(6, 8) : rr(3.5_r, 5.5_r));
}

void build_butterfly(Scene& sc) {
  bool moth = chance(0.4_r);
  real tw = moth ? rr(10, 14) : rr(6, 9);
  Part thorax = make_part(-18, 18, [=](real t) { return tw * lobe(t, 0.9_r, 0.6_r); }, 1, 12);
  Part head = make_part(-30, -14, [=](real t) { return tw * 0.7_r * lobe(t, 1, 0.5_r); }, 2, 10);
  real al = rr(55, 80), aw = tw * rr(0.75_r, 0.95_r);
  Part abdomen = make_part(14, 14 + al, [=](real t) { return aw * lobe(0.12_r + 0.88_r * t, 0.75_r, 0.8_r); }, 3, 14);

  Polys none;
  Polys fuzz;
  if (moth) fuzz = vein_shape(thorax.outline, 30);
  add_part(sc, thorax, fuzz, moth ? 0 : 2.5_r);
  add_eyes(sc, tw * 0.62_r, head.y_at(0.5_r), tw * 0.26_r);
  add_part(sc, head, none);
  add_antennae(sc, P(tw * 0.3_r, head.y_at(0.15_r)), moth ? ANT_FEATHER : ANT_CLUB, rr(45, 70));
  add_part(sc, abdomen, segments(abdomen, ri(6, 9), 2), 2.5_r);

  WingStyle st;
  st.veins = ri(5, 8);
  st.border = chance(0.6_r);
  st.spots = ri(0, 3);
  st.rings = chance(0.5_r);
  st.ladder = false;
  st.solid = true;

  Blade fore;
  memset(&fore, 0, sizeof(fore));
  fore.L = rr(120, 165);
  fore.W = fore.L * rr(0.3_r, 0.42_r);
  fore.pos = rr(0.6_r, 0.75_r);
  fore.base = 0.1_r;
  fore.bend = rr(-0.1_r, 0.05_r);
  fore.wobble = rr(0.05_r, 0.25_r);
  fore.tip = chance(0.6_r) ? TIP_ROUND : TIP_POINTED;
  Blade hind = fore;
  hind.L = fore.L * rr(0.6_r, 0.8_r);
  hind.W = hind.L * rr(0.42_r, 0.55_r);
  hind.pos = rr(0.55_r, 0.7_r);
  hind.bend = rr(0, 0.12_r);
  hind.tip = TIP_ROUND;

  // A moth holds its front wings swept back; a butterfly's are spread.
  real fa = moth ? deg(rr(-15, 15)) : deg(rr(-50, -28));
  real ha = moth ? deg(rr(45, 65)) : deg(rr(28, 52));
  add_wings(sc, fore, P(tw * 0.5_r, -6), fa, st, 10);
  WingStyle hs = st;
  hs.spots = ri(0, 2);
  add_wings(sc, hind, P(tw * 0.5_r, 6), ha, hs, 30);
}

void build_dragonfly(Scene& sc) {
  bool damsel = chance(0.3_r);
  real tw = damsel ? rr(8, 10) : rr(12, 16);
  Part thorax = make_part(-22, 20, [=](real t) { return tw * lobe(t, 0.8_r, 0.5_r); }, 1, 12);
  real hw = tw * (damsel ? 1.2_r : 1.05_r);
  Part head = make_part(-36, -18, [=](real t) { return hw * 0.6_r * lobe(t, 1, 0.5_r); }, 2, 10);
  real al = rr(150, 195), aw = damsel ? rr(3, 4) : rr(5, 7);
  Part abdomen = make_part(16, 16 + al, [=](real t) { return aw * (1.25_r - 0.4_r * t) * ppow(std::sin(PI * (0.06_r + 0.9_r * t)), 0.15_r); }, 3, 16);

  Polys none;
  Poly split(2);
  split[0] = P(0, -20);
  split[1] = P(0, 18);
  add_part(sc, thorax, Polys(1, split), 3);
  // Eyes that take up most of the head.
  add_eyes(sc, hw * (damsel ? 0.85_r : 0.48_r), head.y_at(0.45_r), hw * (damsel ? 0.34_r : 0.5_r));
  add_part(sc, head, none);
  add_part(sc, abdomen, segments(abdomen, 10, 1));

  WingStyle st;
  st.veins = 0;
  st.border = false;
  st.spots = 0;
  st.rings = false;
  st.ladder = true;
  st.solid = false;
  Blade w;
  memset(&w, 0, sizeof(w));
  w.L = rr(150, 195);
  w.W = w.L * (damsel ? rr(0.09_r, 0.11_r) : rr(0.12_r, 0.16_r));
  w.pos = rr(0.5_r, 0.65_r);
  w.base = damsel ? 0.15_r : 0.4_r;
  w.bend = rr(-0.03_r, 0.03_r);
  w.wobble = 0.05_r;
  w.tip = TIP_ROUND;
  real spread = damsel ? deg(rr(18, 30)) : deg(rr(5, 12));
  add_wings(sc, w, P(tw * 0.6_r, -8), -spread, st, 10);
  Blade h = w;
  h.L *= rr(0.92_r, 1);
  if (!damsel) h.W *= rr(1.1_r, 1.3_r);
  add_wings(sc, h, P(tw * 0.6_r, 6), spread, st, 30);

  // Short legs, held forwards.
  add_six_legs(sc, P(tw * 0.5_r, -16), P(tw * 0.6_r, -8), P(tw * 0.6_r, 0), rr(12, 18), rr(1.6_r, 2.4_r));
}

void build_bee(Scene& sc) {
  static const int KIND_W[] = {3, 3, 2};
  int kind = pick(KIND_W, 3);  // 0 bee, 1 wasp, 2 fly
  real tw = kind == 1 ? rr(15, 19) : rr(19, 25);
  Part thorax = make_part(-20, 22, [=](real t) { return tw * lobe(t, 0.95_r, 0.5_r); }, 1, 14);
  real hw = tw * rr(0.7_r, 0.85_r);
  Part head = make_part(-36, -15, [=](real t) { return hw * lobe(t, 1, 0.5_r); }, 2, 12);
  real al = kind == 1 ? rr(85, 110) : rr(60, 85);
  real aw = kind == 1 ? tw * rr(1, 1.2_r) : tw * rr(1.1_r, 1.35_r);
  real waist = kind == 1 ? 0.02_r : 0.14_r;
  real point = kind == 1 ? 0.95_r : 0.6_r;
  Part abdomen = make_part(18, 18 + al, [=](real t) { return aw * lobe(waist + (1 - waist) * t, 0.7_r, point); }, 3, 18);

  Polys none;
  Polys fuzz;
  if (kind == 0) fuzz = vein_shape(thorax.outline, 45);
  add_part(sc, thorax, fuzz, kind == 0 ? 0 : 3.5_r);
  add_eyes(sc, hw * 0.7_r, head.y_at(0.5_r), hw * (kind == 2 ? 0.42_r : 0.3_r));
  add_part(sc, head, none);
  add_antennae(sc, P(hw * 0.3_r, head.y_at(0.14_r)), kind == 2 ? ANT_THREAD : ANT_ELBOW, kind == 2 ? rr(8, 12) : rr(26, 38));

  // Clear wings lie over the abdomen without hiding it.
  WingStyle st;
  st.veins = ri(3, 5);
  st.border = false;
  st.spots = 0;
  st.rings = false;
  st.ladder = false;
  st.solid = false;
  Blade w;
  memset(&w, 0, sizeof(w));
  w.L = al * rr(1.15_r, 1.5_r);
  w.W = w.L * rr(0.2_r, 0.27_r);
  w.pos = rr(0.6_r, 0.72_r);
  w.base = 0.12_r;
  w.bend = rr(0, 0.08_r);
  w.wobble = 0.05_r;
  w.tip = TIP_ROUND;
  real sweep = deg(rr(30, 62));
  add_wings(sc, w, P(tw * 0.55_r, -4), sweep, st, 10);
  if (kind != 2) {
    Blade h = w;
    h.L *= rr(0.62_r, 0.75_r);
    h.W = h.L * rr(0.2_r, 0.26_r);
    WingStyle hs = st;
    hs.veins = 2;
    add_wings(sc, h, P(tw * 0.55_r, 6), sweep + deg(rr(12, 22)), hs, 30);
  }

  int segs = ri(5, 7);
  Polys tex = segments(abdomen, segs, 3);
  if (kind != 2 || chance(0.4_r)) {
    Polys bands = stripes(abdomen, segs, 3, 2.6_r);
    tex.insert(tex.end(), bands.begin(), bands.end());
  }
  add_part(sc, abdomen, tex);

  add_six_legs(sc, P(tw * 0.6_r, -12), P(tw * 0.8_r, 2), P(tw * 0.6_r, 14), tw * rr(1.1_r, 1.5_r), rr(2.6_r, 3.8_r));
}

void build_ant(Scene& sc) {
  real hw = rr(20, 28);
  Part head = make_part(-78, -40, [=](real t) { return hw * lobe(t, 1, 0.45_r); }, 1, 14);
  real tw = rr(10, 14);
  // Two lumps, the front one larger.
  Part thorax = make_part(-44, 14, [=](real t) { return tw * (0.62_r + 0.38_r * std::cos(PI * 2 * t)) * ppow(std::sin(PI * t), 0.25_r) + tw * 0.35_r * (1 - t) * ppow(std::sin(PI * t), 0.25_r); }, 2, 16);
  real gl = rr(58, 78), gw = rr(22, 30);
  Part gaster = make_part(26, 26 + gl, [=](real t) { return gw * lobe(t, 0.8_r, 0.6_r); }, 3, 16);

  Polys none;
  add_eyes(sc, hw * 0.78_r, head.y_at(0.5_r), hw * 0.16_r);
  add_part(sc, head, none, 4);
  add_jaws(sc, P(hw * 0.35_r, head.y_at(0.08_r)), rr(12, 22), chance(0.5_r));
  add_antennae(sc, P(hw * 0.5_r, head.y_at(0.2_r)), ANT_ELBOW, rr(55, 80));
  add_part(sc, thorax, none, 3);
  // The narrow waist: one small bead.
  Poly node = circle(0, 20, rr(5, 7), 10, 0.1_r, 7);
  sc.add(node);
  Polys tex = segments(gaster, ri(4, 5), 4);
  Polys shade = shade_shape(gaster.outline, 5, -7, -7);
  tex.insert(tex.end(), shade.begin(), shade.end());
  add_part(sc, gaster, tex);

  add_six_legs(sc, P(tw * 0.7_r, -32), P(tw * 0.5_r, -14), P(tw * 0.5_r, 4), rr(38, 52), rr(2.2_r, 3.2_r));
}

}  // namespace

bool draw_insect(const char* name, const Box& box, LineSink& sink, Stats* stats) {
  if (stats) memset(stats, 0, sizeof(*stats));
  jsr = seed_from(name);
  perlin.clear();
  init_trig();

  Scene sc;
  sc.lines.reserve(512);
  for (int i = 0; i < 8; i++) rnd();  // stir, so similar names do not give similar insects
  static const int KIND_W[] = {5, 4, 2, 3, 2};
  switch (pick(KIND_W, 5)) {
    case 0: build_beetle(sc); break;
    case 1: build_butterfly(sc); break;
    case 2: build_dragonfly(sc); break;
    case 3: build_bee(sc); break;
    default: build_ant(sc); break;
  }
  emit_scene(sc, box, sink, stats);
  std::vector<real>().swap(perlin);
  return true;
}

}  // namespace fishdraw

#else  // !ENABLE_INSECTS

namespace fishdraw {
bool draw_insect(const char*, const Box&, LineSink&, Stats*) { return false; }
}  // namespace fishdraw

#endif
