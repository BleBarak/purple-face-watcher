#pragma once
// Purple's robot face on the Wio Terminal's 320x240 screen.
//
// Same data and motion as packages/purple_face: every face is a list of
// shapes (from faces_data.h). Changing face morphs shapes that match (springs
// on their numbers) and cross-fades the rest. The frame is drawn in
// horizontal bands with anti-aliased polygons, so no full-screen buffer is
// needed; the glow is a blurred quarter-resolution mask under the shapes.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "FaceTypes.h"
#include "faces_data.h"
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

#pragma GCC push_options
#pragma GCC optimize("O3")

namespace purple {
template <class A, class B> static inline auto min(A a, B b) -> decltype(a + b) { return a < b ? a : b; }
template <class A, class B> static inline auto max(A a, B b) -> decltype(a + b) { return a > b ? a : b; }
template <class T, class A, class B> static inline T constrain(T x, A lo, B hi) { return x < (T)lo ? (T)lo : (x > (T)hi ? (T)hi : x); }

// Everything in PSRAM: internal RAM belongs to audio, TLS and the speech model.
static inline void *faceAlloc(size_t n) {
#ifdef ESP_PLATFORM
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
  return p ? p : malloc(n);
#else
  return malloc(n);
#endif
}
static inline void *bigAlloc(size_t n) {
#ifdef ESP_PLATFORM
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
  return p ? p : malloc(n);
#else
  return malloc(n);
#endif
}

// Where finished strips of the frame go (RGB565, w x h, row-major).
typedef void (*FaceSink)(void *ctx, int x, int y, int w, int h, const uint16_t *pixels);

// ------------------------------------------------------------------ motion numbers
// (FACE_SPEC.md "Motion", face_motion_constants.dart)
static const float SHAPE_K = 210, SHAPE_C = 15;
static const float GAZE_K = 520, GAZE_C = 38;
static const float COLOUR_K = 90, COLOUR_C = 19;
static const float TALK_K = 600, TALK_C = 34;
static const float SQUASH_K = 260, SQUASH_C = 11;
static const float SQUASH_AMOUNT = 0.1f;
static const float BLINK_SQUASH_Y = 0.9f, BLINK_STRETCH_X = 0.12f;
static const float PERSPECTIVE_DIV = 700, MOUTH_PARALLAX = 0.6f;
static const float BREATH_AMOUNT = 0.015f, BREATH_PERIOD = 4.0f;
static const float PIVOT_X = 320, PIVOT_Y = 360;
static const float DRIFT = 2.0f;
static const float TALK_LOWER = 46, TALK_UPPER = 4, TALK_NARROW = 0.15f, TALK_LIFT = 3;

// ------------------------------------------------------------------ screen layout
// Screen size: the Watcher's round 412 x 412 by default. Other boards can
// set PURPLE_FACE_W / PURPLE_FACE_H before including this file.
#ifndef PURPLE_FACE_W
#define PURPLE_FACE_W 412
#endif
#ifndef PURPLE_FACE_H
#define PURPLE_FACE_H 412
#endif
static const int SCR_W = PURPLE_FACE_W, SCR_H = PURPLE_FACE_H;
static const int BAND_H = 16;
// Round screen: show design units 320 +- 306 (same framing as the
// Watcher animation PNGs), so the listening arcs just fit.
static const float VIEW_S = (SCR_W < SCR_H ? SCR_W : SCR_H) / 613.0f;
static const float VIEW_X = SCR_W / 2 - 320 * VIEW_S;
static const float VIEW_Y = SCR_H / 2 - 320 * VIEW_S;

static const int GLOW_DIV = 8;  // glow grid: one cell per 8x8 pixels (it is a blur anyway)
static const int GLOW_W = (SCR_W + GLOW_DIV - 1) / GLOW_DIV, GLOW_H = (SCR_H + GLOW_DIV - 1) / GLOW_DIV;  // 52 x 52

static const int MAX_LIVE = 72;
static const int MAX_CUTS = 16;
static const int MAX_PTS = 4200;
static const int MAX_CONT = 400;
static const int MAX_PREP = 96;

struct Affine {
  float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  Affine() {}
  Affine(float a_, float b_, float c_, float d_, float e_, float f_) : a(a_), b(b_), c(c_), d(d_), e(e_), f(f_) {}
  static Affine translate(float x, float y) { return Affine(1, 0, 0, 1, x, y); }
  static Affine scaleAround(float sx, float sy, float cx, float cy) {
    return Affine(sx, 0, 0, sy, cx - sx * cx, cy - sy * cy);
  }
  static Affine rotateAround(float deg, float cx, float cy) {
    float r = deg * (float)M_PI / 180, co = cosf(r), si = sinf(r);
    return Affine(co, si, -si, co, cx - co * cx + si * cy, cy - si * cx - co * cy);
  }
  // this after o
  Affine operator*(const Affine &o) const {
    return Affine(a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d,
                  a * o.e + c * o.f + e, b * o.e + d * o.f + f);
  }
  void apply(float x, float y, float &ox, float &oy) const {
    ox = a * x + c * y + e;
    oy = b * x + d * y + f;
  }
  float scale() const { return sqrtf(fabsf(a * d - b * c)); }
};

static inline void spring(float &x, float &v, float target, float k, float c, float h) {
  v += (k * (target - x) - c * v) * h;
  x += v * h;
}

struct Live {
  const ShapeDef *def;
  float p[NPARAM], v[NPARAM];
  float alpha, alphaV, alphaTarget;
  bool masked;
};

// A shape ready to rasterize this frame, in screen pixels.
struct Prep {
  uint16_t contStart, contCount;
  float minx, miny, maxx, maxy;
  float halfWidth;
  uint32_t fill, stroke;
  float fillA, strokeA;   // 0..1, includes cross-fade
  uint8_t group, side, flags;
  bool forceClose;
};
struct PCont {
  uint16_t start, count;
  bool closed;
};

class FaceRenderer {
 public:
  FaceRenderer(FaceSink sink, void *ctx) : _sink(sink), _ctx(ctx) {}

  bool begin() {
    _band = (uint16_t *)faceAlloc(SCR_W * BAND_H * 2);
    _covA = (uint8_t *)faceAlloc(SCR_W * BAND_H);
    _covB = (uint8_t *)faceAlloc(SCR_W * BAND_H);
    _scratch = (uint8_t *)faceAlloc(SCR_W * BAND_H);
    _pts = (float *)bigAlloc(MAX_PTS * 2 * sizeof(float));
    if (!_band || !_covA || !_covB || !_scratch || !_pts) return false;
    snapTo(0);
    return true;
  }

  int faceIndex() const { return _face; }
  const FaceDef &face() const { return FACES[_face]; }

  static int findFace(const char *id) {
    for (int i = 0; i < FACE_COUNT; i++)
      if (strcmp(FACES[i].id, id) == 0) return i;
    return -1;
  }

  // Jump to a face with no animation.
  void snapTo(int fi) {
    _face = fi;
    const FaceDef &f = FACES[fi];
    _nLive = 0;
    for (int i = 0; i < f.shapeCount && _nLive < MAX_LIVE; i++) addLive(_live, _nLive, &FACE_SHAPES[f.shapeStart + i], 1);
    _nCuts = 0;
    for (int i = 0; i < f.cutCount && _nCuts < MAX_CUTS; i++) addLive(_cuts, _nCuts, &FACE_SHAPES[f.cutStart + i], 1);
    for (int i = 0; i < 3; i++) _col[i] = (f.accent >> (16 - 8 * i)) & 255, _colV[i] = 0;
    _glow = f.glowOpacity;
    _glowV = 0;
    _tilt = f.tilt;
    _tiltV = 0;
  }

  // Morph to a face (matching shapes spring, the rest cross-fade).
  void morphTo(int fi) {
    if (fi == _face) return;
    _face = fi;
    const FaceDef &f = FACES[fi];
    morphList(_live, _nLive, MAX_LIVE, f.shapeStart, f.shapeCount);
    morphList(_cuts, _nCuts, MAX_CUTS, f.cutStart, f.cutCount);
  }

  // ---------------------------------------------------------------- per-frame motion inputs
  float gazeX = 0, gazeY = 0;      // design units (already sprung by the controller)
  float blinkL = 0, blinkR = 0;     // 0 open .. 1 shut
  float anticX = 1, anticY = 1, anticDrop = 0;
  float squash = 0;                 // squash spring value
  float breath = 1;
  float talk = 0;                   // 0..1 mouth opening
  float deviceTilt = 0;             // degrees, added to the face's own tilt
  int labelRows = 0;                // bottom rows left alone (for a caption)

  void step(float dt) {
    if (dt > 0.05f) dt = 0.05f;
    float h = dt / 2;
    const FaceDef &f = FACES[_face];
    for (int s = 0; s < 2; s++) {
      for (int i = 0; i < 3; i++) spring(_col[i], _colV[i], (f.accent >> (16 - 8 * i)) & 255, COLOUR_K, COLOUR_C, h);
      spring(_glow, _glowV, f.glowOpacity, COLOUR_K, COLOUR_C, h);
      spring(_tilt, _tiltV, f.tilt + deviceTilt, SHAPE_K, SHAPE_C, h);
      stepList(_live, _nLive, h);
      stepList(_cuts, _nCuts, h);
    }
    prune(_live, _nLive);
    prune(_cuts, _nCuts);
    // An eye that lost its mask keeps it while cuts on its side fade out.
    for (int i = 0; i < _nLive; i++) {
      Live &l = _live[i];
      if (l.masked && !(l.def->flags & F_MASKED)) {
        bool any = false;
        for (int c = 0; c < _nCuts; c++)
          if (_cuts[c].alpha > 0.01f && (_cuts[c].def->side == l.def->side || _cuts[c].def->side == S_C)) any = true;
        if (!any) l.masked = false;
      }
    }
  }

  bool canBlink() const { return FACES[_face].canBlink; }

  // ---------------------------------------------------------------- drawing
  void render() {
    buildTransforms();
    _nPrep = 0;
    _nPts = 0;
    _nCont = 0;
    for (int i = 0; i < _nLive; i++) prepare(_live[i], partTransform(_live[i].def), false, 0);
    _firstCut[0] = _nPrep;
    for (int i = 0; i < _nCuts; i++)
      if (_cuts[i].def->side != S_R) prepare(_cuts[i], _eye[S_L], true, S_L);
    _firstCut[1] = _nPrep;
    for (int i = 0; i < _nCuts; i++)
      if (_cuts[i].def->side != S_L) prepare(_cuts[i], _eye[S_R], true, S_R);
    _endCut = _nPrep;
    // The glow is a soft blur: rebuilding it every other frame is invisible
    // and saves a good share of the work. Always rebuild after a full redraw.
    if (_fullRedraw || (++_glowFrame & 1)) buildGlow();

    // Where the face is this frame: its shapes plus the glow around them.
    int limit = SCR_H - labelRows;
    int cx0 = SCR_W, cy0 = SCR_H, cx1 = 0, cy1 = 0;
    for (int i = 0; i < _firstCut[0]; i++) {
      const Prep &p = _prep[i];
      cx0 = min(cx0, (int)floorf(p.minx));
      cy0 = min(cy0, (int)floorf(p.miny));
      cx1 = max(cx1, (int)ceilf(p.maxx) + 1);
      cy1 = max(cy1, (int)ceilf(p.maxy) + 1);
    }
    if (_glowX1 > _glowX0) {
      cx0 = min(cx0, _glowX0 * GLOW_DIV - GLOW_DIV);
      cy0 = min(cy0, _glowY0 * GLOW_DIV - GLOW_DIV);
      cx1 = max(cx1, _glowX1 * GLOW_DIV + GLOW_DIV);
      cy1 = max(cy1, _glowY1 * GLOW_DIV + GLOW_DIV);
    }
    cx0 = max(cx0, 0);
    cy0 = max(cy0, 0);
    cx1 = min(cx1, SCR_W);
    cy1 = min(cy1, limit);

    // Redraw that, and wherever the face was last frame (to clear it).
    int x0 = cx0, y0 = cy0, x1 = cx1, y1 = cy1;
    if (_fullRedraw) {
      x0 = 0, y0 = 0, x1 = SCR_W, y1 = limit;
      _fullRedraw = false;
    } else if (_lastX1 > _lastX0 && _lastY1 > _lastY0) {
      if (x1 <= x0 || y1 <= y0) x0 = _lastX0, y0 = _lastY0, x1 = _lastX1, y1 = _lastY1;
      else {
        x0 = min(x0, _lastX0);
        y0 = min(y0, _lastY0);
        x1 = max(x1, _lastX1);
        y1 = max(y1, _lastY1);
      }
    }
    _lastX0 = cx0, _lastY0 = cy0, _lastX1 = cx1, _lastY1 = cy1;
    y1 = min(y1, limit);
    if (x1 <= x0 || y1 <= y0) return;

    int w = x1 - x0;
    int rows = (SCR_W * BAND_H) / w;
    for (int y = y0; y < y1; y += rows) {
      int h = min(rows, y1 - y);
      drawBand(x0, w, y, h);
      _sink(_ctx, x0, y, w, h, _band);
    }
  }

  void markAllDirty() { _fullRedraw = true; }


 private:
  FaceSink _sink;
  void *_ctx;
  uint16_t *_band = nullptr;
  uint8_t *_covA = nullptr, *_covB = nullptr, *_scratch = nullptr;
  float *_pts = nullptr;
  uint8_t _glowMask[GLOW_W * GLOW_H];
  uint8_t _glowRowMax[GLOW_H];
  bool _fullRedraw = true;
  uint32_t _glowFrame = 0;
  int _lastX0 = 0, _lastY0 = 0, _lastX1 = 0, _lastY1 = 0;
  int _glowX0 = 0, _glowY0 = 0, _glowX1 = 0, _glowY1 = 0;  // glow cells in use

  Live _morphTmp[MAX_LIVE], _morphFresh[MAX_LIVE];
  bool _morphClaimed[MAX_LIVE];
  Live _live[MAX_LIVE];
  int _nLive = 0;
  Live _cuts[MAX_CUTS];
  int _nCuts = 0;
  int _face = 0;
  float _col[3], _colV[3];
  float _glow = 0.75f, _glowV = 0;
  float _tilt = 0, _tiltV = 0;

  Affine _faceT, _eye[3], _brow, _mouth, _extra;

  Prep _prep[MAX_PREP];
  int _nPrep = 0, _firstCut[2] = {0, 0}, _endCut = 0;
  PCont _cont[MAX_CONT];
  int _nCont = 0, _nPts = 0;

  // ---------------------------------------------------------------- morph
  static void addLive(Live *arr, int &n, const ShapeDef *d, float alpha) {
    Live &l = arr[n++];
    l.def = d;
    for (int k = 0; k < NPARAM; k++) l.p[k] = d->params[k], l.v[k] = 0;
    l.alpha = alpha;
    l.alphaV = 0;
    l.alphaTarget = 1;
    l.masked = d->flags & F_MASKED;
  }

  static bool sameLook(const ShapeDef *a, const ShapeDef *b) {
    if (a->kind != b->kind || a->role != b->role || a->side != b->side || a->group != b->group) return false;
    if (a->fill != b->fill || a->stroke != b->stroke) return false;
    if ((a->flags & (F_CLOSED | F_ROUND | F_CUT)) != (b->flags & (F_CLOSED | F_ROUND | F_CUT))) return false;
    if (a->kind == K_POLY && a->contourStart != b->contourStart) return false;
    return true;
  }

  void morphList(Live *arr, int &n, int maxN, int start, int count) {
    Live *tmp = _morphTmp;
    bool *claimed = _morphClaimed;
    for (int i = 0; i < n; i++) claimed[i] = false;
    // New face's shapes, each taking over a matching old one if there is one.
    Live *fresh = _morphFresh;
    int nf = 0;
    for (int s = 0; s < count && nf < maxN; s++) {
      const ShapeDef *d = &FACE_SHAPES[start + s];
      int found = -1;
      for (int i = 0; i < n; i++)
        if (!claimed[i] && sameLook(arr[i].def, d)) { found = i; break; }
      if (found >= 0) {
        claimed[found] = true;
        Live l = arr[found];
        l.def = d;
        l.alphaTarget = 1;
        l.masked = l.masked || (d->flags & F_MASKED);
        fresh[nf++] = l;
      } else {
        addLive(fresh, nf, d, 0);
      }
    }
    // Leftovers fade out underneath.
    int nt = 0;
    for (int i = 0; i < n && nt < maxN; i++)
      if (!claimed[i]) {
        tmp[nt] = arr[i];
        tmp[nt].alphaTarget = 0;
        nt++;
      }
    for (int i = 0; i < nf && nt < maxN; i++) tmp[nt++] = fresh[i];
    for (int i = 0; i < nt; i++) arr[i] = tmp[i];
    n = nt;
  }

  static void stepList(Live *arr, int n, float h) {
    for (int i = 0; i < n; i++) {
      Live &l = arr[i];
      if (l.alphaTarget > 0)
        for (int k = 0; k < NPARAM; k++) spring(l.p[k], l.v[k], l.def->params[k], SHAPE_K, SHAPE_C, h);
      spring(l.alpha, l.alphaV, l.alphaTarget, COLOUR_K, COLOUR_C, h);
    }
  }

  static void prune(Live *arr, int &n) {
    int j = 0;
    for (int i = 0; i < n; i++)
      if (!(arr[i].alphaTarget == 0 && arr[i].alpha < 0.01f)) arr[j++] = arr[i];
    n = j;
  }

  // ---------------------------------------------------------------- transforms
  void buildTransforms() {
    const FaceDef &f = FACES[_face];
    Affine view(VIEW_S, 0, 0, VIEW_S, VIEW_X, VIEW_Y);
    float q = squash;
    Affine motionFace = Affine::translate(0, -TALK_LIFT * talk) *
                        Affine::scaleAround(breath * (1 + SQUASH_AMOUNT * q), breath * (1 - SQUASH_AMOUNT * q), PIVOT_X, PIVOT_Y);
    // The face's own tilt (and the device tilt) is sprung in _tilt, around (320, 320).
    _faceT = view * motionFace * Affine::rotateAround(_tilt, 320, 320);
    for (int s = 0; s < 2; s++) {
      float cx = f.eyeCentre[s * 2], cy = f.eyeCentre[s * 2 + 1];
      float looked = s == S_L ? -gazeX : gazeX;
      float persp = 1 + (looked > 0 ? looked : 0) / PERSPECTIVE_DIV;
      float b = s == S_L ? blinkL : blinkR;
      _eye[s] = _faceT * Affine::translate(gazeX, gazeY + anticDrop) *
                Affine::scaleAround(persp * (1 + BLINK_STRETCH_X * b) * anticX, persp * (1 - BLINK_SQUASH_Y * b) * anticY, cx, cy);
    }
    _brow = _faceT * Affine::translate(gazeX, gazeY);
    _eye[S_C] = _brow;
    _mouth = _faceT * Affine::translate(gazeX * MOUTH_PARALLAX, gazeY * MOUTH_PARALLAX);
    _extra = _faceT;
  }

  const Affine &partTransform(const ShapeDef *d) const {
    switch (d->role) {
      case R_EYE: return _eye[d->side];
      case R_BROW: return _brow;
      case R_MOUTH: return _mouth;
      default: return _extra;
    }
  }

  // ---------------------------------------------------------------- shape -> screen points
  bool pushPt(const Affine &m, float x, float y) {
    if (_nPts >= MAX_PTS) return false;
    m.apply(x, y, _pts[_nPts * 2], _pts[_nPts * 2 + 1]);
    _nPts++;
    return true;
  }

  void beginContour() {
    if (_nCont < MAX_CONT) {
      _cont[_nCont].start = _nPts;
      _cont[_nCont].count = 0;
      _cont[_nCont].closed = false;
    }
  }
  void endContour(bool closed) {
    if (_nCont >= MAX_CONT) return;
    _cont[_nCont].count = _nPts - _cont[_nCont].start;
    _cont[_nCont].closed = closed;
    if (_cont[_nCont].count > 0) _nCont++;
  }

  void quadPts(const Affine &m, float x0, float y0, float cx, float cy, float x1, float y1, int n, bool first) {
    for (int i = first ? 0 : 1; i <= n; i++) {
      float t = (float)i / n, u = 1 - t;
      pushPt(m, u * u * x0 + 2 * u * t * cx + t * t * x1, u * u * y0 + 2 * u * t * cy + t * t * y1);
    }
  }

  void prepare(const Live &l, const Affine &part, bool cut, uint8_t side) {
    if (_nPrep >= MAX_PREP) return;
    float alpha = l.alpha < 0 ? 0 : (l.alpha > 1 ? 1 : l.alpha);
    if (alpha <= 0.004f) return;
    const ShapeDef *d = l.def;
    const float *g = l.p;
    Affine m = part * Affine(g[7], g[8], g[9], g[10], g[11], g[12]);
    Prep &p = _prep[_nPrep];
    p.contStart = _nCont;
    p.forceClose = false;
    bool talking = !cut && talk > 0.01f && d->role == R_MOUTH && d->kind == K_QUAD && !(d->flags & F_CLOSED) && d->stroke != C_NONE;

    if (d->kind == K_RRECT) {
      float x = g[0], y = g[1], w = g[2] > 0 ? g[2] : 0, h = g[3] > 0 ? g[3] : 0;
      float rx = constrain(g[4], 0.0f, w / 2), ry = constrain(g[5], 0.0f, h / 2);
      if (w < 0.01f || h < 0.01f) return;
      beginContour();
      float sr = max(rx, ry) * m.scale();
      int n = sr < 1 ? 1 : (sr < 4 ? 3 : (sr < 12 ? 5 : 8));
      const float cxs[4] = {x + w - rx, x + w - rx, x + rx, x + rx};
      const float cys[4] = {y + ry, y + h - ry, y + h - ry, y + ry};
      const float a0[4] = {-M_PI / 2, 0, M_PI / 2, M_PI};
      for (int c = 0; c < 4; c++)
        for (int i = 0; i <= n; i++) {
          float a = a0[c] + (M_PI / 2) * i / n;
          pushPt(m, cxs[c] + rx * cosf(a), cys[c] + ry * sinf(a));
        }
      endContour(true);
    } else if (d->kind == K_QUAD) {
      int n = constrain((int)((fabsf(g[0] - g[2]) + fabsf(g[2] - g[4]) + fabsf(g[1] - g[3]) + fabsf(g[3] - g[5])) * m.scale() / 3), 4, 24);
      beginContour();
      if (talking) {
        float mid = (g[0] + g[4]) / 2, k = 1 - TALK_NARROW * talk;
        float x0 = mid + (g[0] - mid) * k, x1 = mid + (g[4] - mid) * k;
        quadPts(m, x0, g[1], g[2], g[3] - 2 * TALK_UPPER * talk, x1, g[5], n, true);
        quadPts(m, x1, g[5], g[2], g[3] + 2 * TALK_LOWER * talk, x0, g[1], n, false);
        endContour(true);
      } else {
        quadPts(m, g[0], g[1], g[2], g[3], g[4], g[5], n, true);
        endContour(d->flags & F_CLOSED);
      }
    } else {
      for (int c = 0; c < d->contourCount; c++) {
        const Contour &ct = FACE_CONTOURS[d->contourStart + c];
        beginContour();
        for (int i = 0; i < ct.count; i++)
          pushPt(m, FACE_POINTS[(ct.start + i) * 2] * 0.25f, FACE_POINTS[(ct.start + i) * 2 + 1] * 0.25f);
        endContour(ct.closed);
      }
    }
    p.contCount = _nCont - p.contStart;
    if (p.contCount == 0) return;

    p.halfWidth = g[6] * m.scale() * 0.5f;
    p.group = d->group;
    p.side = cut ? side : d->side;
    p.flags = d->flags | (l.masked ? F_MASKED : 0);
    if (cut) {
      p.fill = C_ACCENT;  // only the coverage is used
      p.stroke = C_NONE;
      p.fillA = alpha;
      p.strokeA = 0;
      p.forceClose = true;
    } else if (talking) {
      p.fill = d->stroke;
      p.stroke = d->stroke;
      p.fillA = p.strokeA = alpha * d->strokeOpacity / 255.0f;
    } else {
      p.fill = d->fill;
      p.stroke = (d->stroke != C_NONE && g[6] > 0) ? d->stroke : C_NONE;
      p.fillA = alpha * d->fillOpacity / 255.0f;
      p.strokeA = alpha * d->strokeOpacity / 255.0f;
    }
    float pad = (p.stroke != C_NONE ? p.halfWidth : 0) + 1.5f;
    p.minx = p.miny = 1e9f;
    p.maxx = p.maxy = -1e9f;
    for (int c = p.contStart; c < p.contStart + p.contCount; c++)
      for (int i = _cont[c].start; i < _cont[c].start + _cont[c].count; i++) {
        float x = _pts[i * 2], y = _pts[i * 2 + 1];
        if (x < p.minx) p.minx = x;
        if (x > p.maxx) p.maxx = x;
        if (y < p.miny) p.miny = y;
        if (y > p.maxy) p.maxy = y;
      }
    p.minx -= pad;
    p.miny -= pad;
    p.maxx += pad;
    p.maxy += pad;
    _nPrep++;
  }

  // ---------------------------------------------------------------- rasterizing
  // Coverage target: w x h bytes for rows y0.., points multiplied by `s`.
  // Coverage target: the screen rect x0..x0+w, y0..y0+h, one byte a pixel.
  struct Target {
    uint8_t *cov;
    int w, h, y0;
    float s;
    int x0;
    uint8_t *row(int r) const { return cov + (r - y0) * w - x0; }  // index by screen x
  };

  void clearRect(const Target &t, int x0, int x1, int r0, int r1) {
    for (int r = r0; r < r1; r++) memset(t.row(r) + x0, 0, x1 - x0);
  }

  // Clip a prep's box to the target. False if nothing to draw.
  bool box(const Prep &p, const Target &t, int &x0, int &x1, int &r0, int &r1) {
    x0 = max(t.x0, (int)floorf(p.minx * t.s));
    x1 = min(t.x0 + t.w, (int)ceilf(p.maxx * t.s) + 1);
    r0 = max(t.y0, (int)floorf(p.miny * t.s));
    r1 = min(t.y0 + t.h, (int)ceilf(p.maxy * t.s) + 1);
    return x0 < x1 && r0 < r1;
  }

  // Non-zero fill, 4 sub-rows per row, exact horizontal coverage.
  void fillCov(const Prep &p, const Target &t, int r0, int r1) {
    const int subs = t.s < 1 ? 1 : 4;  // the blurred glow needs no anti-aliasing
    const int amount = subs == 1 ? 255 : 64;  // addc clamps 4 x 64 to 255
    static float xs[96];
    static int8_t ds[96];
    for (int r = r0; r < r1; r++) {
      uint8_t *row = t.row(r);
      for (int sub = 0; sub < subs; sub++) {
        float sy = (r + (sub + 0.5f) / subs) / t.s;
        int n = 0;
        for (int c = p.contStart; c < p.contStart + p.contCount; c++) {
          const PCont &ct = _cont[c];
          if (ct.count < 2) continue;
          for (int i = 0; i < ct.count; i++) {
            int j = i + 1 == ct.count ? 0 : i + 1;  // fills always close
            float ax = _pts[(ct.start + i) * 2], ay = _pts[(ct.start + i) * 2 + 1];
            float bx = _pts[(ct.start + j) * 2], by = _pts[(ct.start + j) * 2 + 1];
            if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
              if (n < 96) {
                xs[n] = (ax + (sy - ay) * (bx - ax) / (by - ay)) * t.s;
                ds[n] = by > ay ? 1 : -1;
                n++;
              }
            }
          }
        }
        if (n < 2) continue;
        for (int i = 1; i < n; i++) {  // insertion sort
          float x = xs[i];
          int8_t dd = ds[i];
          int j = i - 1;
          while (j >= 0 && xs[j] > x) {
            xs[j + 1] = xs[j];
            ds[j + 1] = ds[j];
            j--;
          }
          xs[j + 1] = x;
          ds[j + 1] = dd;
        }
        int wind = 0;
        float start = 0;
        for (int i = 0; i < n; i++) {
          int was = wind;
          wind += ds[i];
          if (was == 0 && wind != 0) start = xs[i];
          else if (was != 0 && wind == 0) span(row, t.x0, t.x0 + t.w, start, xs[i], amount);
        }
      }
    }
  }

  static void span(uint8_t *row, int lo, int hi, float xa, float xb, int amt) {
    if (xa < lo) xa = lo;
    if (xb > hi) xb = hi;
    if (xb <= xa) return;
    int ia = (int)xa, ib = (int)xb;
    if (ia == ib) {
      addc(row[ia], (int)(amt * (xb - xa)));
      return;
    }
    addc(row[ia], (int)(amt * (ia + 1 - xa)));
    for (int i = ia + 1; i < ib; i++) addc(row[i], amt);
    if (ib < hi) addc(row[ib], (int)(amt * (xb - ib)));
  }
  static inline void addc(uint8_t &c, int v) {
    int s = c + v;
    c = s > 255 ? 255 : s;
  }

  // Round-capped stroke: coverage from the distance to each segment.
  void strokeCov(const Prep &p, const Target &t, int x0, int x1, int r0, int r1) {
    float hw = p.halfWidth * t.s;
    if (hw < 0.35f) hw = 0.35f;
    for (int c = p.contStart; c < p.contStart + p.contCount; c++) {
      const PCont &ct = _cont[c];
      int segs = (ct.closed || p.forceClose) ? ct.count : ct.count - 1;
      if (ct.count == 1) segs = 1;
      for (int i = 0; i < segs; i++) {
        int j = (i + 1) % ct.count;
        float ax = _pts[(ct.start + i) * 2] * t.s, ay = _pts[(ct.start + i) * 2 + 1] * t.s;
        float bx = _pts[(ct.start + j) * 2] * t.s, by = _pts[(ct.start + j) * 2 + 1] * t.s;
        int sx0 = max(x0, (int)floorf(min(ax, bx) - hw - 1));
        int sx1 = min(x1, (int)ceilf(max(ax, bx) + hw + 1));
        int sr0 = max(r0, (int)floorf(min(ay, by) - hw - 1));
        int sr1 = min(r1, (int)ceilf(max(ay, by) + hw + 1));
        float dx = bx - ax, dy = by - ay;
        float len2 = dx * dx + dy * dy;
        float inv = len2 > 1e-6f ? 1 / len2 : 0;
        for (int r = sr0; r < sr1; r++) {
          uint8_t *row = t.row(r);
          float py = r + 0.5f;
          for (int x = sx0; x < sx1; x++) {
            float px = x + 0.5f;
            float u = ((px - ax) * dx + (py - ay) * dy) * inv;
            u = u < 0 ? 0 : (u > 1 ? 1 : u);
            float ex = ax + u * dx - px, ey = ay + u * dy - py;
            float cov = hw + 0.5f - sqrtf(ex * ex + ey * ey);
            if (cov <= 0) continue;
            int v = cov >= 1 ? 255 : (int)(cov * 255);
            if (v > row[x]) row[x] = v;
          }
        }
      }
    }
  }

  // Masked eye: multiply its coverage by (1 - cut coverage) of its side.
  void applyCuts(const Prep &p, const Target &t, uint8_t *cutBuf, int x0, int x1, int r0, int r1) {
    if (p.side > S_R) return;
    int from = _firstCut[p.side], to = p.side == S_L ? _firstCut[1] : _endCut;
    int live[MAX_CUTS], n = 0;
    for (int i = from; i < to; i++) {
      const Prep &c = _prep[i];
      if (c.maxx * t.s < x0 || c.minx * t.s > x1 || c.maxy * t.s < r0 || c.miny * t.s > r1) continue;
      live[n++] = i;
    }
    if (n == 0) return;
    Target ct = t;
    ct.cov = cutBuf;
    clearRect(ct, x0, x1, r0, r1);
    if (n == 1) {
      // Usual case: one cut, straight into the cut buffer.
      const Prep &c = _prep[live[0]];
      fillCov(c, ct, r0, r1);
      int a = (int)(c.fillA * 256);
      for (int r = r0; r < r1; r++) {
        uint8_t *e = t.row(r), *cc = ct.row(r);
        for (int x = x0; x < x1; x++) {
          int k = cc[x];
          if (k) e[x] = (e[x] * (256 - ((k * a) >> 8))) >> 8;
        }
      }
      return;
    }
    // Several cuts: fill each into the scratch, keep the strongest.
    Target one = t;
    one.cov = _scratch;
    for (int k = 0; k < n; k++) {
      const Prep &c = _prep[live[k]];
      clearRect(one, x0, x1, r0, r1);
      fillCov(c, one, r0, r1);
      int a = (int)(c.fillA * 256);
      for (int r = r0; r < r1; r++) {
        uint8_t *src = one.row(r), *dst = ct.row(r);
        for (int x = x0; x < x1; x++) {
          int v = (src[x] * a) >> 8;
          if (v > dst[x]) dst[x] = v;
        }
      }
    }
    for (int r = r0; r < r1; r++) {
      uint8_t *e = t.row(r), *c = ct.row(r);
      for (int x = x0; x < x1; x++)
        if (c[x]) e[x] = (e[x] * (256 - c[x])) >> 8;
    }
  }

  // Coverage of a prep's fill (with cuts) into t.cov over its box.
  bool fillInto(const Prep &p, const Target &t, uint8_t *cutBuf, int &x0, int &x1, int &r0, int &r1) {
    if (!box(p, t, x0, x1, r0, r1)) return false;
    clearRect(t, x0, x1, r0, r1);
    fillCov(p, t, r0, r1);
    if (p.flags & F_MASKED) applyCuts(p, t, cutBuf, x0, x1, r0, r1);
    return true;
  }

  bool strokeInto(const Prep &p, const Target &t, uint8_t *cutBuf, int &x0, int &x1, int &r0, int &r1) {
    if (!box(p, t, x0, x1, r0, r1)) return false;
    clearRect(t, x0, x1, r0, r1);
    strokeCov(p, t, x0, x1, r0, r1);
    if (p.flags & F_MASKED) applyCuts(p, t, cutBuf, x0, x1, r0, r1);
    return true;
  }

  // ---------------------------------------------------------------- glow
  void buildGlow() {
    memset(_glowMask, 0, sizeof(_glowMask));
    memset(_glowRowMax, 0, sizeof(_glowRowMax));
    _glowX0 = _glowY0 = _glowX1 = _glowY1 = 0;
    if (_glow < 0.02f) return;
    Target t{_covA, GLOW_W, GLOW_H, 0, 1.0f / GLOW_DIV, 0};
    int bx0 = GLOW_W, by0 = GLOW_H, bx1 = 0, by1 = 0;
    for (int i = 0; i < _firstCut[0]; i++) {
      const Prep &p = _prep[i];
      if (p.group != G_GLOW) continue;
      for (int pass = 0; pass < 2; pass++) {
        uint32_t col = pass == 0 ? p.fill : p.stroke;
        if (col != C_ACCENT) continue;
        float a = pass == 0 ? p.fillA : p.strokeA;
        int x0, x1, r0, r1;
        bool ok = pass == 0 ? fillInto(p, t, _covB, x0, x1, r0, r1) : strokeInto(p, t, _covB, x0, x1, r0, r1);
        if (!ok) continue;
        bx0 = min(bx0, x0), by0 = min(by0, r0), bx1 = max(bx1, x1), by1 = max(by1, r1);
        int ai = (int)(a * 256);
        for (int r = r0; r < r1; r++)
          for (int x = x0; x < x1; x++) {
            int v = (_covA[r * GLOW_W + x] * ai) >> 8;
            uint8_t &g = _glowMask[r * GLOW_W + x];
            if (v > g) g = v;
          }
      }
    }
    if (bx1 <= bx0) return;
    // σ ≈ 14 design units ≈ 6.6 px ≈ 1.7 glow cells: three [1 1 2 1 1]/6
    // passes, only over the cells the glow can reach.
    bx0 = max(0, bx0 - 4), by0 = max(0, by0 - 4), bx1 = min(GLOW_W, bx1 + 4), by1 = min(GLOW_H, by1 + 4);
    static uint8_t line[(GLOW_W > GLOW_H ? GLOW_W : GLOW_H) + 4];
    for (int pass = 0; pass < 2; pass++) {  // 2 passes at 1/8 res ~ the app's sigma
      for (int r = by0; r < by1; r++) {
        uint8_t *row = _glowMask + r * GLOW_W;
        line[0] = line[1] = 0;
        memcpy(line + 2, row + bx0, bx1 - bx0);
        line[bx1 - bx0 + 2] = line[bx1 - bx0 + 3] = 0;
        for (int i = 0; i < bx1 - bx0; i++) {
          const uint8_t *l = line + i;
          row[bx0 + i] = ((l[0] + l[1] + 2 * l[2] + l[3] + l[4]) * 43) >> 8;
        }
      }
      for (int x = bx0; x < bx1; x++) {
        line[0] = line[1] = 0;
        for (int r = by0; r < by1; r++) line[r - by0 + 2] = _glowMask[r * GLOW_W + x];
        line[by1 - by0 + 2] = line[by1 - by0 + 3] = 0;
        for (int i = 0; i < by1 - by0; i++) {
          const uint8_t *l = line + i;
          _glowMask[(by0 + i) * GLOW_W + x] = ((l[0] + l[1] + 2 * l[2] + l[3] + l[4]) * 43) >> 8;
        }
      }
    }
    _glowX0 = GLOW_W, _glowY0 = GLOW_H;
    for (int r = by0; r < by1; r++) {
      uint8_t m = 0;
      for (int x = bx0; x < bx1; x++) {
        uint8_t v = _glowMask[r * GLOW_W + x];
        if (v > 2) {
          _glowX0 = min(_glowX0, x);
          _glowX1 = max(_glowX1, x + 1);
        }
        m = max(m, v);
      }
      _glowRowMax[r] = m;
      if (m > 2) {
        _glowY0 = min(_glowY0, r);
        _glowY1 = r + 1;
      }
    }
    if (_glowX1 <= _glowX0) _glowX0 = _glowY0 = _glowX1 = _glowY1 = 0;
  }

  // ---------------------------------------------------------------- band
  uint32_t resolve(uint32_t c) const {
    if (c == C_ACCENT) return ((uint32_t)_col[0] << 16) | ((uint32_t)_col[1] << 8) | (uint32_t)_col[2];
    return c & 0xFFFFFF;
  }

  static inline uint16_t rgb565(int r, int g, int b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }

  void drawBand(int bx, int bw, int y0, int h) {
    // Background: black plus the glow. Glow colour for each level from a table.
    static uint16_t lut[256];
    int ar = constrain((int)_col[0], 0, 255), ag = constrain((int)_col[1], 0, 255), ab = constrain((int)_col[2], 0, 255);
    int go = (int)(constrain(_glow, 0.0f, 1.0f) * 256);
    for (int v = 0; v < 256; v++) {
      int a = (v * go) >> 8;
      lut[v] = v < 2 ? 0 : rgb565((ar * a) >> 8, (ag * a) >> 8, (ab * a) >> 8);
    }
    static int16_t mix[GLOW_W + 1];  // this row's glow per cell (x16)
    const int step = 256 / GLOW_DIV;  // cell position per pixel, x256
    // Glow only exists inside this box (cells -> pixels, with the blur margin).
    int gx0 = max(bx, _glowX0 * GLOW_DIV - GLOW_DIV), gx1 = min(bx + bw, _glowX1 * GLOW_DIV + GLOW_DIV);
    for (int r = 0; r < h; r++) {
      uint16_t *out = _band + r * bw - bx;  // index by screen x
      int pos = ((2 * (y0 + r) + 1) * 128) / GLOW_DIV - 128;  // ((y + .5) / DIV - .5) cells, x256
      int gy0 = pos >> 8, wy = pos & 255;
      int ya = constrain(gy0, 0, GLOW_H - 1), yb = constrain(gy0 + 1, 0, GLOW_H - 1);
      if ((_glowRowMax[ya] <= 2 && _glowRowMax[yb] <= 2) || gx1 <= gx0) {
        memset(out + bx, 0, bw * 2);
        continue;
      }
      if (gx0 > bx) memset(out + bx, 0, (gx0 - bx) * 2);
      if (gx1 < bx + bw) memset(out + gx1, 0, (bx + bw - gx1) * 2);
      const uint8_t *ra = _glowMask + ya * GLOW_W, *rb = _glowMask + yb * GLOW_W;
      int c0 = max(0, gx0 / GLOW_DIV - 1), c1 = min(GLOW_W - 1, gx1 / GLOW_DIV + 1);
      for (int c = c0; c <= c1; c++) mix[c] = (ra[c] * (256 - wy) + rb[c] * wy) >> 4;
      // Between cell centres the glow is a straight line: step along it.
      int x = gx0;
      int px = ((2 * x + 1) * 128) / GLOW_DIV - 128;
      while (x < gx1) {
        int cx = px >> 8, wx = px & 255;
        int n = (256 - wx + step - 1) / step;  // pixels until the next cell
        int end = min(gx1, x + max(n, 1));
        int xa = constrain(cx, 0, GLOW_W - 1), xb = constrain(cx + 1, 0, GLOW_W - 1);
        int va = mix[xa], vb = mix[xb];
        int v = va * 256 + (vb - va) * wx;  // x256 x16
        int dv = (vb - va) * step;
        int cnt = end - x;
        if (va <= 32 && vb <= 32) {  // < 2 in 0..255 terms
          memset(out + x, 0, cnt * 2);
        } else {
          for (int i = 0; i < cnt; i++, v += dv) out[x + i] = lut[(v >> 12) & 255];
        }
        x = end;
        px += cnt * step;
      }
    }

    Target t{_covA, bw, h, y0, 1.0f, bx};
    for (int i = 0; i < _firstCut[0]; i++) {
      const Prep &p = _prep[i];
      if (p.maxy < y0 || p.miny >= y0 + h) continue;
      for (int pass = 0; pass < 2; pass++) {
        uint32_t col = pass == 0 ? p.fill : p.stroke;
        if (col == C_NONE) continue;
        float a = pass == 0 ? p.fillA : p.strokeA;
        if (a <= 0.004f) continue;
        int x0, x1, r0, r1;
        bool ok = pass == 0 ? fillInto(p, t, _covB, x0, x1, r0, r1) : strokeInto(p, t, _covB, x0, x1, r0, r1);
        if (!ok) continue;
        blend(t, resolve(col), a, x0, x1, r0, r1);
      }
    }
  }

  void blend(const Target &t, uint32_t rgb, float a, int x0, int x1, int r0, int r1) {
    int sr = (rgb >> 16) & 255, sg = (rgb >> 8) & 255, sb = rgb & 255;
    int ai = (int)(a * 256);
    for (int r = r0; r < r1; r++) {
      const uint8_t *c = t.row(r);
      uint16_t *o = _band + (r - t.y0) * t.w - t.x0;
      for (int x = x0; x < x1; x++) {
        int k = (c[x] * ai) >> 8;
        if (k == 0) continue;
        uint16_t d = o[x];
        if (k >= 255) {
          o[x] = rgb565(sr, sg, sb);
          continue;
        }
        int dr = (d >> 8) & 0xF8, dg = (d >> 3) & 0xFC, db = (d << 3) & 0xF8;
        o[x] = rgb565(dr + (((sr - dr) * k) >> 8), dg + (((sg - dg) * k) >> 8), db + (((sb - db) * k) >> 8));
      }
    }
  }
};

}  // namespace purple

#pragma GCC pop_options
