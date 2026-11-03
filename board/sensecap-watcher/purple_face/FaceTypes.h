#pragma once
#include <stdint.h>

// Shape kinds: rounded rect (circles and ellipses too), quadratic curve,
// or a pre-flattened polygon/polyline list (SVG paths, polygons, text).
enum : uint8_t { K_RRECT = 0, K_QUAD = 1, K_POLY = 2 };
enum : uint8_t { R_EYE = 0, R_BROW = 1, R_MOUTH = 2, R_EXTRA = 3 };
enum : uint8_t { S_L = 0, S_R = 1, S_C = 2 };
enum : uint8_t { G_GLOW = 0, G_BLUR = 1, G_PLAIN = 2 };
enum : uint8_t { F_MASKED = 1, F_CLOSED = 2, F_ROUND = 4, F_CUT = 8 };

// Colours: 0 = none, 1 = the face accent, else 0x1RRGGBB.
static const uint32_t C_NONE = 0, C_ACCENT = 1;

struct Contour {
  uint16_t start;   // first point (index into FACE_POINTS pairs)
  uint16_t count;
  uint8_t closed;
};

// params: geometry[6], stroke width, affine a b c d e f.
static const int NPARAM = 13;

struct ShapeDef {
  uint8_t kind, role, side, group, flags;
  uint32_t fill, stroke;
  uint8_t fillOpacity, strokeOpacity;
  uint16_t contourStart, contourCount;
  float params[NPARAM];
};

struct FaceDef {
  const char *id;
  const char *category;
  uint32_t accent;  // 0xRRGGBB
  float tilt;
  float glowOpacity;
  uint16_t shapeStart, shapeCount, cutStart, cutCount;
  uint8_t canBlink;
  float eyeCentre[4];  // Lx Ly Rx Ry
};
