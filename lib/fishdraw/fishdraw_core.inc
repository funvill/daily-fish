// Shared by fishdraw.cpp and flowerdraw.cpp: random numbers, noise, and the
// polyline geometry (clipping, polygon union, resampling, hatching).
//
// Ported from fishdraw.js, Copyright (c) 2021 Lingdong Huang, MIT License.
//
// This file is included inside an anonymous namespace by each generator, so
// each gets its own copy of the state below. Only one runs at a time.

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"

// Upper bound on poisson-disk samples, which bounds the memory used by the
// "small dots" pattern.
#ifndef FISHDRAW_MAX_SAMPLES
#define FISHDRAW_MAX_SAMPLES 8000
#endif

typedef FISHDRAW_REAL real;

constexpr real operator"" _r(long double v) { return (real)v; }
constexpr real operator"" _r(unsigned long long v) { return (real)v; }

const real PI = 3.14159265358979323846_r;
const real E_ = 2.718281828459045_r;

struct Pt {
  real x, y;
};
typedef std::vector<Pt> Poly;
typedef std::vector<Poly> Polys;

inline Pt P(real x, real y) {
  Pt p;
  p.x = x;
  p.y = y;
  return p;
}

// ---------------------------------------------------------------------------
// Random numbers and noise (identical streams to the original)
// ---------------------------------------------------------------------------

uint32_t jsr = 0x5EED;

inline uint32_t xorshift(uint32_t j) {
  j ^= j << 17;
  j ^= (uint32_t)((int32_t)j >> 13);  // JavaScript's >> is an arithmetic shift
  j ^= j << 5;
  return j;
}

inline real to_unit(uint32_t j) { return (real)((double)j / 4294967295.0); }

inline real rnd() {
  jsr = xorshift(jsr);
  return to_unit(jsr);
}

// JavaScript's ~~x for the values that occur here: truncate, and 0 for NaN/Inf.
inline int trunc_i(real v) {
  if (!(v > -2.0e9_r && v < 2.0e9_r)) return 0;
  return (int)v;
}

const int PERLIN_YWRAPB = 4;
const int PERLIN_YWRAP = 1 << PERLIN_YWRAPB;
const int PERLIN_ZWRAPB = 8;
const int PERLIN_ZWRAP = 1 << PERLIN_ZWRAPB;
const int PERLIN_SIZE = 4095;

std::vector<real> perlin;

// Sine and cosine from a table. The ESP32-C3 has no floating point unit, and
// the library versions cost thousands of cycles each; the noise function
// alone asks for twelve cosines per call. Linear interpolation over 512 steps
// of a quarter turn is good to about one part in a million.
const int TRIG_STEPS = 512;
real g_quarter_sine[TRIG_STEPS + 1];
bool g_trig_ready = false;

void init_trig() {
  if (g_trig_ready) return;
  for (int i = 0; i <= TRIG_STEPS; i++) g_quarter_sine[i] = std::sin((real)i / TRIG_STEPS * (PI / 2));
  g_quarter_sine[TRIG_STEPS] = 1;
  g_trig_ready = true;
}

// sin(f * pi/2) for f in 0..1
inline real quarter_sine(real f) {
  real x = f * TRIG_STEPS;
  int i = (int)x;
  if (i >= TRIG_STEPS) return g_quarter_sine[TRIG_STEPS];
  if (i < 0) return 0;
  real t = x - i;
  return g_quarter_sine[i] + (g_quarter_sine[i + 1] - g_quarter_sine[i]) * t;
}

real fsin(real a) {
  if (!(a > -1.0e5_r && a < 1.0e5_r)) return std::sin(a);
  real t = a * (2 / PI);
  int k = (int)t;
  if (t < k) k--;
  real f = t - k;
  switch (k & 3) {
    case 0: return quarter_sine(f);
    case 1: return quarter_sine(1 - f);
    case 2: return -quarter_sine(f);
    default: return -quarter_sine(1 - f);
  }
}

inline real fcos(real a) { return fsin(a + PI / 2); }

inline real scaled_cosine(real i) { return 0.5_r * (1.0_r - fcos(i * PI)); }

real noise(real x, real y = 0, real z = 0) {
  if (perlin.empty()) {
    init_trig();
    perlin.resize(PERLIN_SIZE + 1);
    for (int i = 0; i < PERLIN_SIZE + 1; i++) perlin[i] = rnd();
  }
  if (x < 0) x = -x;
  if (y < 0) y = -y;
  if (z < 0) z = -z;
  if (!(x < 1.0e8_r)) x = 0;
  if (!(y < 1.0e8_r)) y = 0;
  if (!(z < 1.0e8_r)) z = 0;
  int xi = (int)x, yi = (int)y, zi = (int)z;
  real xf = x - xi, yf = y - yi, zf = z - zi;
  real r = 0, ampl = 0.5_r;
  for (int o = 0; o < 4; o++) {
    int of = xi + (yi << PERLIN_YWRAPB) + (zi << PERLIN_ZWRAPB);
    real rxf = scaled_cosine(xf), ryf = scaled_cosine(yf);
    real n1 = perlin[of & PERLIN_SIZE];
    n1 += rxf * (perlin[(of + 1) & PERLIN_SIZE] - n1);
    real n2 = perlin[(of + PERLIN_YWRAP) & PERLIN_SIZE];
    n2 += rxf * (perlin[(of + PERLIN_YWRAP + 1) & PERLIN_SIZE] - n2);
    n1 += ryf * (n2 - n1);
    of += PERLIN_ZWRAP;
    n2 = perlin[of & PERLIN_SIZE];
    n2 += rxf * (perlin[(of + 1) & PERLIN_SIZE] - n2);
    real n3 = perlin[(of + PERLIN_YWRAP) & PERLIN_SIZE];
    n3 += rxf * (perlin[(of + PERLIN_YWRAP + 1) & PERLIN_SIZE] - n3);
    n2 += ryf * (n3 - n2);
    n1 += scaled_cosine(zf) * (n2 - n1);
    r += n1 * ampl;
    ampl *= 0.5_r;
    xi <<= 1;
    xf *= 2;
    yi <<= 1;
    yf *= 2;
    zi <<= 1;
    zf *= 2;
    if (xf >= 1.0_r) {
      xi++;
      xf--;
    }
    if (yf >= 1.0_r) {
      yi++;
      yf--;
    }
    if (zf >= 1.0_r) {
      zi++;
      zf--;
    }
  }
  return r;
}

// ---------------------------------------------------------------------------
// Small geometry helpers
// ---------------------------------------------------------------------------

inline real dist(real x0, real y0, real x1, real y1) {
  real dx = x1 - x0, dy = y1 - y0;
  return std::sqrt(dx * dx + dy * dy);
}
inline real dist(const Pt& a, const Pt& b) { return dist(a.x, a.y, b.x, b.y); }
inline real lerp(real a, real b, real t) { return a * (1 - t) + b * t; }
inline Pt lerp2d(const Pt& a, const Pt& b, real t) {
  return P(a.x * (1 - t) + b.x * t, a.y * (1 - t) + b.y * t);
}

// Math.sign(a) * Math.pow(Math.abs(a), b)
inline real spow(real a, real b) {
  if (a > 0) return std::pow(a, b);
  if (a < 0) return -std::pow(-a, b);
  return 0;
}

// x ** b where the original only ever sees x >= 0. In single precision
// sin(pi) is slightly negative, which would give NaN.
inline real ppow(real a, real b) { return a > 0 ? std::pow(a, b) : 0; }

struct BBox {
  real x, y, w, h;
  bool empty;
};

BBox get_bbox(const Poly& pts) {
  BBox b;
  b.empty = pts.empty();
  real xmin = INFINITY, ymin = INFINITY, xmax = -INFINITY, ymax = -INFINITY;
  for (size_t i = 0; i < pts.size(); i++) {
    xmin = std::min(xmin, pts[i].x);
    ymin = std::min(ymin, pts[i].y);
    xmax = std::max(xmax, pts[i].x);
    ymax = std::max(ymax, pts[i].y);
  }
  b.x = xmin;
  b.y = ymin;
  b.w = xmax - xmin;
  b.h = ymax - ymin;
  return b;
}

Poly trsl_poly(const Poly& p, real x, real y) {
  Poly q(p.size());
  for (size_t i = 0; i < p.size(); i++) q[i] = P(p[i].x + x, p[i].y + y);
  return q;
}

Poly rot_poly(const Poly& p, real th) {
  Poly q(p.size());
  init_trig();
  real c = fcos(th), s = fsin(th);
  for (size_t i = 0; i < p.size(); i++) q[i] = P(p[i].x * c - p[i].y * s, p[i].x * s + p[i].y * c);
  return q;
}

Poly rev(const Poly& p) { return Poly(p.rbegin(), p.rend()); }

Poly concat(const Poly& a, const Poly& b) {
  Poly o;
  o.reserve(a.size() + b.size());
  o.insert(o.end(), a.begin(), a.end());
  o.insert(o.end(), b.begin(), b.end());
  return o;
}

// Array.prototype.slice
Poly slice(const Poly& p, int start, int end) {
  int n = (int)p.size();
  if (start < 0) start = std::max(0, n + start);
  if (end < 0) end = std::max(0, n + end);
  start = std::min(start, n);
  end = std::min(end, n);
  if (start >= end) return Poly();
  return Poly(p.begin() + start, p.begin() + end);
}
Poly slice(const Poly& p, int start) { return slice(p, start, (int)p.size()); }

inline real pt_in_pl(real x, real y, real x0, real y0, real x1, real y1) {
  real dx = x1 - x0, dy = y1 - y0;
  return (x - x0) * dy - (y - y0) * dx;
}

// Intersection of segment p0-p1 with segment q0-q1 (p treated as a ray if
// is_ray). t is the position along p, s along q.
inline bool seg_isect(real p0x, real p0y, real p1x, real p1y, real q0x, real q0y, real q1x,
                      real q1y, bool is_ray, real* t_out, real* s_out) {
  real d0x = p1x - p0x, d0y = p1y - p0y;
  real d1x = q1x - q0x, d1y = q1y - q0y;
  real vc = d0x * d1y - d0y * d1x;
  if (vc == 0) return false;
  real vcn = vc * vc;
  real qx = q0x - p0x, qy = q0y - p0y;
  real vc_vcn = vc / vcn;
  real t = (qx * d1y - qy * d1x) * vc_vcn;
  if (!(0 <= t && (is_ray || t < 1))) return false;
  real s = (qx * d0y - qy * d0x) * vc_vcn;
  if (!(0 <= s && s < 1)) return false;
  *t_out = t;
  *s_out = s;
  return true;
}

// Comparing two floats costs a library call on a chip without an FPU, and
// clipping is mostly comparisons that rule edges out. A float's bits, with
// the negative range flipped, order the same way as the float does, so those
// tests are done on integers instead.
static_assert(sizeof(real) == 4, "the comparison keys assume 32-bit floats");

inline int32_t fkey(real v) {
  int32_t b;
  memcpy(&b, &v, sizeof(b));
  if (b == (int32_t)0x80000000) return 0;  // -0 and +0 are the same number
  return b ^ ((b >> 31) & 0x7fffffff);
}

// A polygon to clip against, with its bounding box and comparison keys cached.
struct Region {
  const Poly* p;
  real x0, y0, x1, y1;
  int32_t kx0, ky0, kx1, ky1;
  std::vector<int32_t> kx, ky;
  explicit Region(const Poly& poly) : p(&poly), kx(poly.size()), ky(poly.size()) {
    kx0 = ky0 = INT32_MAX;
    kx1 = ky1 = INT32_MIN;
    for (size_t i = 0; i < poly.size(); i++) {
      int32_t a = fkey(poly[i].x), b = fkey(poly[i].y);
      kx[i] = a;
      ky[i] = b;
      if (a < kx0) kx0 = a;
      if (a > kx1) kx1 = a;
      if (b < ky0) ky0 = b;
      if (b > ky1) ky1 = b;
    }
    x0 = y0 = INFINITY;
    x1 = y1 = -INFINITY;
    for (size_t i = 0; i < poly.size(); i++) {
      if (fkey(poly[i].x) == kx0) x0 = poly[i].x;
      if (fkey(poly[i].x) == kx1) x1 = poly[i].x;
      if (fkey(poly[i].y) == ky0) y0 = poly[i].y;
      if (fkey(poly[i].y) == ky1) y1 = poly[i].y;
    }
  }
};

struct Hit {
  real t;
  Pt xy;
};
std::vector<Hit> g_hits;

// Crossings of segment a-b with the polygon, sorted along the segment.
void seg_isect_poly(const Pt& a, const Pt& b, const Region& rg, std::vector<Hit>& hits) {
  hits.clear();
  int32_t kax = fkey(a.x), kbx = fkey(b.x), kay = fkey(a.y), kby = fkey(b.y);
  int32_t sx0 = std::min(kax, kbx), sx1 = std::max(kax, kbx);
  int32_t sy0 = std::min(kay, kby), sy1 = std::max(kay, kby);
  if (sx1 < rg.kx0 || sx0 > rg.kx1 || sy1 < rg.ky0 || sy0 > rg.ky1) return;
  const Poly& poly = *rg.p;
  size_t n = poly.size();
  const int32_t* kx = rg.kx.data();
  const int32_t* ky = rg.ky.data();
  for (size_t i = 0; i < n; i++) {
    size_t i1 = i + 1 == n ? 0 : i + 1;
    // An edge wholly to one side of the segment's box cannot cross it.
    int32_t cx = kx[i], dx = kx[i1];
    if ((cx < sx0 && dx < sx0) || (cx > sx1 && dx > sx1)) continue;
    int32_t cy = ky[i], dy = ky[i1];
    if ((cy < sy0 && dy < sy0) || (cy > sy1 && dy > sy1)) continue;
    const Pt& c = poly[i];
    const Pt& d = poly[i1];
    real t, s;
    if (seg_isect(a.x, a.y, b.x, b.y, c.x, c.y, d.x, d.y, false, &t, &s)) {
      Hit h;
      h.t = t;
      h.xy = P(b.x * t + a.x * (1 - t), b.y * t + a.y * (1 - t));
      hits.push_back(h);
    }
  }
  // Stable insertion sort by t; there are rarely more than two.
  for (size_t i = 1; i < hits.size(); i++) {
    Hit h = hits[i];
    size_t j = i;
    while (j > 0 && hits[j - 1].t > h.t) {
      hits[j] = hits[j - 1];
      j--;
    }
    hits[j] = h;
  }
}

// Is the point inside the polygon? Counts crossings of a ray in the original's
// fixed direction (e, pi).
bool point_inside(const Pt& a, const Region& rg) {
  int32_t kax = fkey(a.x), kay = fkey(a.y);
  // Outside the polygon's box is outside the polygon.
  if (kax > rg.kx1 || kay > rg.ky1 || kax < rg.kx0 || kay < rg.ky0) return false;
  const Poly& poly = *rg.p;
  size_t n = poly.size();
  const int32_t* kx = rg.kx.data();
  const int32_t* ky = rg.ky.data();
  int count = 0;
  real bx = a.x + E_, by = a.y + PI;
  for (size_t i = 0; i < n; i++) {
    size_t i1 = i + 1 == n ? 0 : i + 1;
    // The ray only travels +x, +y: edges behind it cannot be hit.
    if ((kx[i] < kax && kx[i1] < kax) || (ky[i] < kay && ky[i1] < kay)) continue;
    const Pt& c = poly[i];
    const Pt& d = poly[i1];
    real t, s;
    if (seg_isect(a.x, a.y, bx, by, c.x, c.y, d.x, d.y, true, &t, &s)) count++;
  }
  return (count & 1) != 0;
}

// Splits a polyline into the parts inside (T) and outside (F) a polygon.
// Either output may be null.
void clip(const Poly& polyline, const Region& rg, Polys* T, Polys* F) {
  if (polyline.empty()) return;
  bool io = point_inside(polyline[0], rg);
  Poly cur;
  size_t n = polyline.size();
  for (size_t i = 0; i < n; i++) {
    const Pt& a = polyline[i];
    cur.push_back(a);
    if (i + 1 == n) break;
    const Pt& b = polyline[i + 1];
    seg_isect_poly(a, b, rg, g_hits);
    for (size_t j = 0; j < g_hits.size(); j++) {
      cur.push_back(g_hits[j].xy);
      Polys* dst = io ? T : F;
      if (dst) dst->push_back(cur);
      io = !io;
      cur.clear();
      cur.push_back(g_hits[j].xy);
    }
  }
  Polys* dst = io ? T : F;
  if (dst && !cur.empty()) dst->push_back(cur);
}

Polys clip_multi(const Polys& polylines, const Poly& polygon, bool want_inside) {
  Region rg(polygon);
  Polys out;
  for (size_t i = 0; i < polylines.size(); i++) {
    clip(polylines[i], rg, want_inside ? &out : nullptr, want_inside ? nullptr : &out);
  }
  return out;
}

Polys clip1(const Poly& polyline, const Poly& polygon, bool want_inside) {
  Region rg(polygon);
  Polys out;
  clip(polyline, rg, want_inside ? &out : nullptr, want_inside ? nullptr : &out);
  return out;
}

// Splits a polyline by a per-point test instead of a polygon.
template <class Fn>
void binclip(const Poly& polyline, Fn func, Polys* T, Polys* F) {
  size_t n = polyline.size();
  if (!n) return;
  std::vector<char> bins(n);
  for (size_t i = 0; i < n; i++) {
    real t = n > 1 ? (real)i / (real)(n - 1) : (real)NAN;
    bins[i] = func(polyline[i].x, polyline[i].y, t) ? 1 : 0;
  }
  bool io = bins[0] != 0;
  Poly cur;
  for (size_t i = 0; i < n; i++) {
    cur.push_back(polyline[i]);
    if (i + 1 == n) break;
    if (bins[i] != bins[i + 1]) {
      Pt pt = lerp2d(polyline[i], polyline[i + 1], 0.5_r);
      cur.push_back(pt);
      Polys* dst = io ? T : F;
      if (dst) dst->push_back(cur);
      io = !io;
      cur.clear();
      cur.push_back(pt);
    }
  }
  Polys* dst = io ? T : F;
  if (dst && !cur.empty()) dst->push_back(cur);
}

// ---------------------------------------------------------------------------
// Polygon union (only the two-polygon case the fish needs)
// ---------------------------------------------------------------------------

struct Isect {
  real t, s;
  Pt xy;
  int16_t other;  // edge index on the other polygon
  int16_t mir;    // index of the same crossing in the other polygon's list
  int8_t side;
};

struct UVerts {
  std::vector<Isect> is;        // grouped by vertex, sorted by t within a vertex
  std::vector<uint16_t> start;  // is[start[i] .. start[i+1]) belongs to vertex i
  int count(int i) const { return start[i + 1] - start[i]; }
  Isect& at(int i, int j) { return is[start[i] + j]; }
  const Isect& at(int i, int j) const { return is[start[i] + j]; }
};

Poly poly_bridge(const Poly& poly0, const Poly& poly1) {
  if (poly0.empty()) return poly1;
  if (poly1.empty()) return poly0;
  real dmin = INFINITY;
  int i0 = 0, j0 = 0;
  for (size_t i = 0; i < poly0.size(); i++) {
    for (size_t j = 0; j < poly1.size(); j++) {
      real dx = poly0[i].x - poly1[j].x, dy = poly0[i].y - poly1[j].y;
      real d2 = dx * dx + dy * dy;
      if (d2 < dmin) {
        dmin = d2;
        i0 = (int)i;
        j0 = (int)j;
      }
    }
  }
  Poly u;
  u.reserve(poly0.size() + poly1.size());
  u.insert(u.end(), poly0.begin(), poly0.begin() + i0);
  u.insert(u.end(), poly1.begin() + j0, poly1.end());
  u.insert(u.end(), poly1.begin(), poly1.begin() + j0);
  u.insert(u.end(), poly0.begin() + i0, poly0.end());
  return u;
}

bool build_vertices(const Poly& poly, const Poly& other, UVerts& out, const UVerts* oout) {
  int n = (int)poly.size(), m = (int)other.size();
  bool has = false;
  Region ob(other);
  out.is.clear();
  out.start.assign(n + 1, 0);
  for (int i = 0; i < n; i++) {
    out.start[i] = (uint16_t)out.is.size();
    const Pt& a = poly[i];
    const Pt& b = poly[(i + 1) % n];
    // On the first pass, an edge clear of the other polygon's box has no
    // crossings. The second pass mirrors the first, so it must not skip.
    if (!oout) {
      int32_t kax = fkey(a.x), kbx = fkey(b.x);
      if ((kax < ob.kx0 && kbx < ob.kx0) || (kax > ob.kx1 && kbx > ob.kx1)) continue;
      int32_t kay = fkey(a.y), kby = fkey(b.y);
      if ((kay < ob.ky0 && kby < ob.ky0) || (kay > ob.ky1 && kby > ob.ky1)) continue;
    }
    size_t first = out.is.size();
    for (int j = 0; j < m; j++) {
      const Pt& c = other[j];
      const Pt& d = other[(j + 1) % m];
      Isect xx;
      bool found = false;
      if (oout) {
        int cnt = oout->count(j);
        for (int k = 0; k < cnt; k++) {
          const Isect& ox = oout->at(j, k);
          if (ox.other == i) {
            xx.t = ox.s;
            xx.s = ox.t;
            xx.xy = ox.xy;
            xx.side = pt_in_pl(a.x, a.y, b.x, b.y, c.x, c.y) < 0 ? 1 : -1;
            found = true;
            break;
          }
        }
      }
      // The second pass only mirrors what the first found. Testing each pair
      // again (as the original does) costs a great deal for a large polygon
      // and can only turn up a crossing that the first pass would dispute.
      if (!found && !oout) {
        real t, s;
        if (seg_isect(a.x, a.y, b.x, b.y, c.x, c.y, d.x, d.y, false, &t, &s)) {
          xx.t = t;
          xx.s = s;
          xx.xy = P(b.x * t + a.x * (1 - t), b.y * t + a.y * (1 - t));
          xx.side = pt_in_pl(a.x, a.y, b.x, b.y, c.x, c.y) < 0 ? 1 : -1;
          found = true;
        }
      }
      if (found) {
        has = true;
        xx.other = (int16_t)j;
        xx.mir = -1;
        out.is.push_back(xx);
      }
    }
    for (size_t p = first + 1; p < out.is.size(); p++) {
      Isect h = out.is[p];
      size_t q = p;
      while (q > first && out.is[q - 1].t > h.t) {
        out.is[q] = out.is[q - 1];
        q--;
      }
      out.is[q] = h;
    }
  }
  out.start[n] = (uint16_t)out.is.size();
  // Vertices skipped by `continue` got their start set; fix any that were not
  // reached in order (none are, but keep the table monotonic).
  for (int i = n - 1; i >= 0; i--) {
    if (out.start[i] > out.start[i + 1]) out.start[i] = out.start[i + 1];
  }
  return has;
}

void mirror_isects(UVerts& v, const UVerts& w) {
  int n = (int)v.start.size() - 1;
  for (int i = 0; i < n; i++) {
    int cnt = v.count(i);
    for (int j = 0; j < cnt; j++) {
      Isect& q = v.at(i, j);
      int k = q.other;
      int wc = w.count(k);
      q.mir = -1;
      for (int z = 0; z < wc; z++) {
        if (w.at(k, z).other == i) {
          q.mir = (int16_t)z;
          break;
        }
      }
    }
  }
}

UVerts g_verts0, g_verts1;

Poly poly_union(const Poly& poly0, const Poly& poly1) {
  if (poly0.empty()) return poly1;
  if (poly1.empty()) return poly0;
  if (poly0.size() > 30000 || poly1.size() > 30000) return Poly();
  UVerts& verts0 = g_verts0;
  UVerts& verts1 = g_verts1;
  bool h0 = build_vertices(poly0, poly1, verts0, nullptr);
  bool h1 = build_vertices(poly1, poly0, verts1, &verts0);
  if (!h0 && !h1) return poly_bridge(poly0, poly1);
  mirror_isects(verts0, verts1);
  mirror_isects(verts1, verts0);

  // Start from the leftmost vertex, which is certainly on the outline.
  real xmin = INFINITY;
  int aidx = 0, ai = 0;
  for (size_t i = 0; i < poly0.size(); i++) {
    if (poly0[i].x < xmin) {
      xmin = poly0[i].x;
      aidx = 0;
      ai = (int)i;
    }
  }
  for (size_t i = 0; i < poly1.size(); i++) {
    if (poly1[i].x < xmin) {
      xmin = poly1[i].x;
      aidx = 1;
      ai = (int)i;
    }
  }
  const Poly& ap = aidx ? poly1 : poly0;
  int an = (int)ap.size();
  const Pt& ca = ap[(ai - 1 + an) % an];
  const Pt& cb = ap[ai];
  const Pt& cc = ap[(ai + 1) % an];
  int cw = pt_in_pl(ca.x, ca.y, cb.x, cb.y, cc.x, cc.y) < 0 ? 1 : -1;

  Poly out;
  int idx = aidx, i0 = ai, j0 = -1, dir = cw;
  const int zidx = idx, zi = i0, zj = j0;
  bool first = true;
  size_t guard = 8 * (poly0.size() + poly1.size() + verts0.is.size() + verts1.is.size()) + 64;
  for (;;) {
    if (!first && idx == zidx && i0 == zi && j0 == zj) break;
    first = false;
    if (guard-- == 0) return Poly();
    UVerts& verts = idx ? verts1 : verts0;
    const Poly& poly = idx ? poly1 : poly0;
    int n = (int)poly.size();
    int i1 = (i0 + dir + n) % n;
    if (j0 == -1) {
      out.push_back(poly[i0]);
      if (dir < 0) {
        j0 = verts.count(i1) - 1;
        i0 = i1;
      } else if (!verts.count(i0)) {
        i0 = i1;
      } else {
        j0 = 0;
      }
    } else if (j0 >= verts.count(i0)) {
      i0 = i1;
      j0 = -1;
    } else if (j0 < -1) {
      return Poly();
    } else {
      const Isect& q = verts.at(i0, j0);
      out.push_back(q.xy);
      if (q.mir < 0) return Poly();
      int k = q.other;
      if (q.side * dir < 0) {
        j0 = q.mir - 1;
        dir = -1;
      } else {
        j0 = q.mir + 1;
        dir = 1;
      }
      i0 = k;
      idx = 1 - idx;
    }
  }
  if (out.size() < 3) return Poly();
  return out;
}

// ---------------------------------------------------------------------------
// Resampling
// ---------------------------------------------------------------------------

// Returns the parameter along x0,y0-x1,y1 where it meets the circle, or NaN's
// sibling `false` when it does not.
bool isect_circ_line(real cx, real cy, real r, real x0, real y0, real x1, real y1, real* t_out) {
  real dx = x1 - x0, dy = y1 - y0;
  real fx = x0 - cx, fy = y0 - cy;
  real a = dx * dx + dy * dy;
  real b = 2 * (fx * dx + fy * dy);
  real c = (fx * fx + fy * fy) - r * r;
  real disc = b * b - 4 * a * c;
  if (disc < 0) return false;
  disc = std::sqrt(disc);
  real t0 = (-b - disc) / (2 * a);
  if (0 <= t0 && t0 <= 1) {
    *t_out = t0;
    return true;
  }
  real t = (-b + disc) / (2 * a);
  if (t > 1 || t < 0) return false;
  *t_out = t;
  return true;
}

// Re-spaces a polyline so its points are `step` apart.
Poly resample(const Poly& in, real step) {
  if (in.size() < 2) return in;
  Poly polyline = in;
  Poly out;
  out.push_back(polyline[0]);
  size_t i = 0;
  while (i < polyline.size() - 1) {
    Pt a = polyline[i];
    Pt b = polyline[i + 1];
    real dx = b.x - a.x, dy = b.y - a.y;
    real d = std::sqrt(dx * dx + dy * dy);
    if (d == 0) {
      i++;
      continue;
    }
    int n = trunc_i(d / step);
    if (n > 100000) n = 100000;
    real rest = (n * step) / d;
    real rpx = a.x * (1 - rest) + b.x * rest;
    real rpy = a.y * (1 - rest) + b.y * rest;
    for (int j = 1; j <= n; j++) {
      real t = (real)j / (real)n;
      out.push_back(P(a.x * (1 - t) + rpx * t, a.y * (1 - t) + rpy * t));
    }
    bool found = false;
    size_t next = 0;
    for (size_t j = i + 2; j < polyline.size(); j++) {
      Pt pb = polyline[j - 1];
      Pt pc = polyline[j];
      if (pb.x == pc.x && pb.y == pc.y) continue;
      real t;
      if (!isect_circ_line(rpx, rpy, step, pb.x, pb.y, pc.x, pc.y, &t)) continue;
      Pt q = P(pb.x * (1 - t) + pc.x * t, pb.y * (1 - t) + pc.y * t);
      out.push_back(q);
      polyline[j - 1] = q;
      next = j - 1;
      found = true;
      break;
    }
    if (!found) break;
    i = next;
  }
  if (out.size() > 1) {
    const Pt& l = out.back();
    const Pt& m = polyline.back();
    real d = std::sqrt((m.x - l.x) * (m.x - l.x) + (m.y - l.y) * (m.y - l.y));
    if (d < step * 0.5_r) out.pop_back();
  }
  out.push_back(polyline.back());
  return out;
}

real pt_seg_dist(const Pt& p, const Pt& p0, const Pt& p1) {
  real A = p.x - p0.x, B = p.y - p0.y, C = p1.x - p0.x, D = p1.y - p0.y;
  real dot = A * C + B * D;
  real len_sq = C * C + D * D;
  real param = -1;
  if (len_sq != 0) param = dot / len_sq;
  real xx, yy;
  if (param < 0) {
    xx = p0.x;
    yy = p0.y;
  } else if (param > 1) {
    xx = p1.x;
    yy = p1.y;
  } else {
    xx = p0.x + param * C;
    yy = p0.y + param * D;
  }
  real dx = p.x - xx, dy = p.y - yy;
  return std::sqrt(dx * dx + dy * dy);
}

// ---------------------------------------------------------------------------
// Poisson disk sampling
// ---------------------------------------------------------------------------

void poissondisk(real W, real H, real r, Poly& samples) {
  real w = r / 1.4142135624_r;
  real r2 = r * r;
  int cols = trunc_i(W / w);
  int rows = trunc_i(H / w);
  if (cols < 0) cols = 0;
  if (rows < 0) rows = 0;
  if ((long)cols * rows > 60000L) {
    // Too fine for the memory we have; coarsen the grid's extent rather than fail.
    rows = (int)(60000L / std::max(cols, 1));
  }
  init_trig();
  std::vector<int16_t> grid((size_t)cols * rows, -1);
  std::vector<uint16_t> active;
  samples.push_back(P(W / 2, H / 2));
  for (size_t i = 0; i < samples.size(); i++) {
    int col = trunc_i(samples[i].x / w);
    int row = trunc_i(samples[i].y / w);
    if (col >= 0 && row >= 0 && col < cols && row < rows) grid[col + row * cols] = (int16_t)i;
    active.push_back((uint16_t)i);
  }
  while (!active.empty()) {
    size_t ridx = (size_t)trunc_i(rnd() * active.size());
    if (ridx >= active.size()) ridx = active.size() - 1;
    Pt pos = samples[active[ridx]];
    bool found = false;
    for (int n = 0; n < 30; n++) {
      real sr = r + rnd() * r;
      real sa = 6.2831853072_r * rnd();
      real sx = pos.x + sr * fcos(sa);
      real sy = pos.y + sr * fsin(sa);
      int col = trunc_i(sx / w);
      int row = trunc_i(sy / w);
      if (col > 0 && row > 0 && col < cols - 1 && row < rows - 1 && grid[col + row * cols] == -1) {
        bool ok = true;
        for (int i = -1; i <= 1; i++) {
          for (int j = -1; j <= 1; j++) {
            int nbr = grid[(row + i) * cols + col + j];
            if (nbr != -1) {
              real ddx = sx - samples[nbr].x, ddy = sy - samples[nbr].y;
              if (ddx * ddx + ddy * ddy < r2) ok = false;
            }
          }
        }
        if (ok && samples.size() < FISHDRAW_MAX_SAMPLES) {
          found = true;
          grid[row * cols + col] = (int16_t)samples.size();
          active.push_back((uint16_t)samples.size());
          samples.push_back(P(sx, sy));
        }
      }
    }
    if (!found) active.erase(active.begin() + ridx);
  }
}

// ---------------------------------------------------------------------------
// Shading
// ---------------------------------------------------------------------------

Polys shade_shape(const Poly& poly, real step, real dx, real dy) {
  Polys lines;
  if (poly.empty()) return lines;
  BBox bbox = get_bbox(poly);
  bbox.x -= step;
  bbox.y -= step;
  bbox.w += step * 2;
  bbox.h += step * 2;
  for (real i = -bbox.h; i < bbox.w; i += step) {
    Poly l;
    l.push_back(P(bbox.x + i, bbox.y));
    l.push_back(P(bbox.x + i + bbox.h, bbox.y + bbox.h));
    lines.push_back(l);
  }
  lines = clip_multi(lines, poly, true);
  Poly carve = trsl_poly(poly, -dx, -dy);
  lines = clip_multi(lines, carve, false);
  for (size_t i = 0; i < lines.size(); i++) {
    real s = rnd() * 0.5_r;
    if (lines[i].size() < 2) continue;
    Pt a = lines[i][0], b = lines[i][1];
    if (dy > 0) {
      lines[i][0] = lerp2d(a, b, s);
    } else {
      lines[i][1] = lerp2d(b, a, s);
    }
  }
  return lines;
}

Polys fill_shape(const Poly& poly, real step) {
  Polys lines;
  if (poly.empty()) return lines;
  BBox bbox = get_bbox(poly);
  bbox.x -= step;
  bbox.y -= step;
  bbox.w += step * 2;
  bbox.h += step * 2;
  for (real i = 0; i < bbox.w + bbox.h / 2; i += step) {
    Poly l;
    l.push_back(P(bbox.x + i, bbox.y));
    l.push_back(P(bbox.x + i - bbox.h / 2, bbox.y + bbox.h));
    lines.push_back(l);
  }
  return clip_multi(lines, poly, true);
}

Polys vein_shape(const Poly& poly, int n) {
  Polys out;
  if (poly.empty()) return out;
  BBox bbox = get_bbox(poly);
  for (int i = 0; i < n; i++) {
    real x = bbox.x + rnd() * bbox.w;
    real y = bbox.y + rnd() * bbox.h;
    Poly o;
    o.push_back(P(x, y));
    for (int j = 0; j < 15; j++) {
      real dx = (noise(x * 0.1_r, y * 0.1_r, 7) - 0.5_r) * 4;
      real dy = (noise(x * 0.1_r, y * 0.1_r, 6) - 0.5_r) * 4;
      x += dx;
      y += dy;
      o.push_back(P(x, y));
    }
    out.push_back(o);
  }
  return clip_multi(out, poly, true);
}


// ===========================================================================
// A scene of overlapping shapes, and the blade shape that petals, leaves and
// wings are made from. Not from fishdraw: written for this project, and used
// by the flower and insect generators.
// ===========================================================================

// Upper bound on the points kept for one drawing.
#ifndef SCENE_MAX_POINTS
#define SCENE_MAX_POINTS 14000
#endif

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
        if (points + occluder_points + cur[c].size() > SCENE_MAX_POINTS) {
          full = true;
          continue;
        }
        points += cur[c].size();
        lines.push_back(cur[c]);
      }
    }
    if (outline && outline->size() >= 3 && points + occluder_points + outline->size() > SCENE_MAX_POINTS) {
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


// Scales a finished scene to fill `box` and sends it to the sink.
void emit_scene(const Scene& sc, const Box& box, LineSink& sink, Stats* stats) {
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
    stats->arena_size = (uint32_t)(SCENE_MAX_POINTS * sizeof(Pt));
    stats->overflow = sc.full;
  }
}
