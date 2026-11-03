#!/usr/bin/env python3
"""Turn Purple's faces.json into faces_data.h for the Wio Terminal.

Rounded rects / circles / ellipses and quads stay as numbers so they can
morph. SVG paths, polygons and text are flattened to point lists here, so the
Wio only ever draws polygons and polylines.

Usage: make_faces.py <faces.json> <font.ttf> <out faces_data.h>
(needs: pip install fonttools; font: DejaVuSans-Bold.ttf).

faces.json comes with the Purple app (packages/purple_face/assets/faces).
You only need this to change the faces; faces_data.h is already generated.
"""
import json
import math
import re
import sys

from fontTools.pens.basePen import BasePen
from fontTools.ttLib import TTFont

STEP = 5.0  # design units per flattened segment (~2.4 px on the Wio)
CLOSED_EYES = {'shy', 'sleepy', 'laugh', 'crying', 'kiss', 'yawn', 'error', 'dizzy', 'giggle', 'blink'}


def nseg(length):
    return max(1, min(48, int(math.ceil(length / STEP))))


def quad_pts(p0, c, p1):
    n = nseg(math.dist(p0, c) + math.dist(c, p1))
    out = []
    for i in range(1, n + 1):
        t = i / n
        u = 1 - t
        out.append((u * u * p0[0] + 2 * u * t * c[0] + t * t * p1[0], u * u * p0[1] + 2 * u * t * c[1] + t * t * p1[1]))
    return out


def cubic_pts(p0, c1, c2, p1):
    n = nseg(math.dist(p0, c1) + math.dist(c1, c2) + math.dist(c2, p1))
    out = []
    for i in range(1, n + 1):
        t = i / n
        u = 1 - t
        out.append((
            u ** 3 * p0[0] + 3 * u * u * t * c1[0] + 3 * u * t * t * c2[0] + t ** 3 * p1[0],
            u ** 3 * p0[1] + 3 * u * u * t * c1[1] + 3 * u * t * t * c2[1] + t ** 3 * p1[1]))
    return out


def arc_pts(p0, rx, ry, phi, large, sweep, p1):
    if rx == 0 or ry == 0 or p0 == p1:
        return [p1]
    rx, ry = abs(rx), abs(ry)
    cp, sp = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (p0[0] - p1[0]) / 2, (p0[1] - p1[1]) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
    if lam > 1:
        rx *= math.sqrt(lam)
        ry *= math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(0, num / den)) * (-1 if large == sweep else 1)
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (p0[0] + p1[0]) / 2
    cy = sp * cxp + cp * cyp + (p0[1] + p1[1]) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a
    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = nseg(abs(dt) * max(rx, ry))
    out = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        x, y = rx * math.cos(t), ry * math.sin(t)
        out.append((cp * x - sp * y + cx, sp * x + cp * y + cy))
    return out


def parse_path(d):
    """SVG path -> list of (points, closed) contours."""
    toks = re.findall(r'[A-Za-z]|[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?', d)
    i = 0
    contours = []
    cur = None
    pos = (0.0, 0.0)
    start = (0.0, 0.0)
    cmd = None
    last_ctrl = None
    prev_cmd = None

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    while i < len(toks):
        if re.match(r'[A-Za-z]', toks[i]):
            cmd = toks[i]
            i += 1
        rel = cmd.islower()
        c = cmd.upper()
        ox, oy = pos if rel else (0.0, 0.0)
        if c == 'Z':
            if cur:
                contours.append((cur, True))
            cur = None
            pos = start
            prev_cmd = 'Z'
            continue
        if c == 'M':
            if cur and len(cur) > 1:
                contours.append((cur, False))
            pos = (ox + num(), oy + num())
            start = pos
            cur = [pos]
            cmd = 'l' if rel else 'L'
            prev_cmd = 'M'
            continue
        if cur is None:
            cur = [pos]
        if c == 'L':
            pos = (ox + num(), oy + num())
            cur.append(pos)
        elif c == 'H':
            pos = ((pos[0] if rel else 0) + num(), pos[1])
            cur.append(pos)
        elif c == 'V':
            pos = (pos[0], (pos[1] if rel else 0) + num())
            cur.append(pos)
        elif c == 'Q':
            ctrl = (ox + num(), oy + num())
            end = (ox + num(), oy + num())
            cur += quad_pts(pos, ctrl, end)
            last_ctrl, pos = ctrl, end
        elif c == 'T':
            ctrl = (2 * pos[0] - last_ctrl[0], 2 * pos[1] - last_ctrl[1]) if prev_cmd in 'QT' and last_ctrl else pos
            end = (ox + num(), oy + num())
            cur += quad_pts(pos, ctrl, end)
            last_ctrl, pos = ctrl, end
        elif c == 'C':
            c1 = (ox + num(), oy + num())
            c2 = (ox + num(), oy + num())
            end = (ox + num(), oy + num())
            cur += cubic_pts(pos, c1, c2, end)
            last_ctrl, pos = c2, end
        elif c == 'S':
            c1 = (2 * pos[0] - last_ctrl[0], 2 * pos[1] - last_ctrl[1]) if prev_cmd in 'CS' and last_ctrl else pos
            c2 = (ox + num(), oy + num())
            end = (ox + num(), oy + num())
            cur += cubic_pts(pos, c1, c2, end)
            last_ctrl, pos = c2, end
        elif c == 'A':
            rx, ry, phi, la, sw = num(), num(), num(), num(), num()
            end = (ox + num(), oy + num())
            cur += arc_pts(pos, rx, ry, phi, int(la), int(sw), end)
            pos = end
        else:
            raise ValueError('path command ' + cmd)
        prev_cmd = c
    if cur and len(cur) > 1:
        contours.append((cur, False))
    return contours


class FlatPen(BasePen):
    def __init__(self, glyphset):
        super().__init__(glyphset)
        self.contours = []
        self.cur = None

    def _moveTo(self, p):
        self.cur = [p]

    def _lineTo(self, p):
        self.cur.append(p)

    def _qCurveToOne(self, c, p):
        self.cur += quad_pts(self.cur[-1], c, p)

    def _curveToOne(self, c1, c2, p):
        self.cur += cubic_pts(self.cur[-1], c1, c2, p)

    def _closePath(self):
        self.contours.append((self.cur, True))
        self.cur = None

    _endPath = _closePath


def text_contours(font, s):
    gs = font.getGlyphSet()
    cmap = font.getBestCmap()
    upm = font['head'].unitsPerEm
    hmtx = font['hmtx']
    size = s['size']
    k = size / upm
    ls = s.get('letterSpacing', 0) or 0
    glyphs = [cmap.get(ord(ch), cmap.get(ord('?'))) for ch in s['text']]
    width = sum(hmtx[g][0] * k + ls for g in glyphs)
    anchor = s.get('anchor', 'start')
    if anchor == 'middle':
        x = s['x'] - (width - ls) / 2
    elif anchor == 'end':
        x = s['x'] - width
    else:
        x = s['x']
    out = []
    for g in glyphs:
        pen = FlatPen(gs)
        gs[g].draw(pen)
        for pts, closed in pen.contours:
            out.append(([(x + px * k, s['y'] - py * k) for px, py in pts], closed))
        x += hmtx[g][0] * k + ls
    return out


def simplify(pts, eps=0.9):
    """Ramer-Douglas-Peucker: drop points that change the outline by < eps units."""
    if len(pts) < 4:
        return pts
    if math.dist(pts[0], pts[-1]) < 1e-6:
        # A loop that ends where it starts: split at the farthest point, so
        # neither half measures against a zero-length chord.
        k = max(range(len(pts)), key=lambda i: math.dist(pts[0], pts[i]))
        return simplify(pts[:k + 1], eps)[:-1] + simplify(pts[k:], eps)
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        (ax, ay), (bx, by) = pts[a], pts[b]
        L = math.hypot(bx - ax, by - ay) or 1e-9
        best, bi = -1, -1
        for i in range(a + 1, b):
            x, y = pts[i]
            d = abs((bx - ax) * (ay - y) - (ax - x) * (by - ay)) / L
            if d > best:
                best, bi = d, i
        if best > eps:
            keep[bi] = True
            stack += [(a, bi), (bi, b)]
    return [p for p, k in zip(pts, keep) if k]


def fl(v):
    """A C float literal."""
    t = '%g' % v
    if '.' not in t and 'e' not in t and 'inf' not in t:
        t += '.0'
    return t + 'f'


def rgb(hexs):
    v = int(hexs[1:], 16)
    return (v >> 16) & 255, (v >> 8) & 255, v & 255


def col(v):
    """0 = none, 1 = accent, else 0x1000000 | rgb888 packed as hex string."""
    if v is None:
        return '0'
    if v == 'accent':
        return '1'
    r, g, b = rgb(v)
    return '0x%08X' % (0x1000000 | (r << 16) | (g << 8) | b)


ROLE = {'eye': 0, 'brow': 1, 'mouth': 2, 'extra': 3}
SIDE = {'L': 0, 'R': 1}


def main():
    src, fontfile, out = sys.argv[1:4]
    doc = json.load(open(src))
    font = TTFont(fontfile)
    faces = doc['faces']

    points = []      # flat int16 list, units of 1/4 design unit
    contours = []    # (start point index, count, closed)
    polys = {}       # key -> (contour start, contour count)
    shapes = []
    face_rows = []

    def poly(conts):
        key = tuple((tuple((round(x * 4), round(y * 4)) for x, y in simplify(p)), c) for p, c in conts)
        if key in polys:
            return polys[key]
        cstart = len(contours)
        for pts, closed in key:
            # drop a repeated closing point
            pts = list(pts)
            if closed and len(pts) > 2 and pts[0] == pts[-1]:
                pts.pop()
            contours.append((len(points) // 2, len(pts), 1 if closed else 0))
            for x, y in pts:
                points.extend((x, y))
        polys[key] = (cstart, len(key))
        return polys[key]

    def shape_row(s, group, cut=False):
        k = s['kind']
        sw = s.get('strokeWidth', 0) or 0
        aff = s.get('transform') or [1, 0, 0, 1, 0, 0]
        n = lambda key, d=0: float(s.get(key, d) or 0)
        poly_ref = (0, 0)
        if k == 'circle':
            kind, geom = 0, [n('cx') - n('r'), n('cy') - n('r'), 2 * n('r'), 2 * n('r'), n('r'), n('r')]
        elif k == 'ellipse':
            kind, geom = 0, [n('cx') - n('rx'), n('cy') - n('ry'), 2 * n('rx'), 2 * n('ry'), n('rx'), n('ry')]
        elif k == 'rect':
            kind, geom = 0, [n('x'), n('y'), n('w'), n('h'), n('r'), n('r')]
        elif k == 'quad':
            kind, geom = 1, [n('x0'), n('y0'), n('cx'), n('cy'), n('x1'), n('y1')]
        elif k == 'polygon':
            p = s['points']
            kind, geom = 2, [0] * 6
            poly_ref = poly([([(p[i], p[i + 1]) for i in range(0, len(p), 2)], True)])
        elif k == 'path':
            kind, geom = 2, [0] * 6
            poly_ref = poly(parse_path(s['d']))
        elif k == 'text':
            kind, geom = 2, [0] * 6
            poly_ref = poly(text_contours(font, s))
        else:
            raise ValueError(k)
        fill = None if cut else s.get('fill')
        if k == 'text' and fill is None:
            fill = 'accent'
        if cut:
            fill = '#000000'
        flags = 0
        if s.get('masked'):
            flags |= 1
        if s.get('closed'):
            flags |= 2
        if s.get('cap') == 'round' or s.get('join') == 'round':
            flags |= 4
        if cut:
            flags |= 8
        role = 0 if cut else ROLE[s['role']]
        side = SIDE.get(s.get('side'), 2)
        fo = round(255 * float(s.get('fillOpacity', 1)))
        so = round(255 * float(s.get('strokeOpacity', 1)))
        params = geom + [sw] + list(map(float, aff))
        shapes.append('{%d,%d,%d,%d,%d,%s,%s,%d,%d,%d,%d,{%s}}' % (
            kind, role, side, group, flags, col(fill), col(s.get('stroke')), fo, so,
            poly_ref[0], poly_ref[1], ','.join(fl(round(v, 3)) for v in params)))
        return params, kind, role, side, poly_ref

    def bounds_of(params, kind, poly_ref):
        if kind == 0:
            pts = [(params[0], params[1]), (params[0] + params[2], params[1] + params[3])]
        elif kind == 1:
            pts = [(params[0], params[1]), (params[2], params[3]), (params[4], params[5])]
        else:
            pts = []
            cs, cn = poly_ref
            for ci in range(cs, cs + cn):
                st, cnt, _ = contours[ci]
                pts += [(points[2 * j] / 4, points[2 * j + 1] / 4) for j in range(st, st + cnt)]
        a, b, c, d, e, f = params[7:13]
        return [(a * x + c * y + e, b * x + d * y + f) for x, y in pts]

    for f in faces:
        first = len(shapes)
        eye_pts = {0: [], 1: []}
        can_blink = False
        for g in f['groups']:
            group = 0 if g.get('glow') else (1 if g.get('blur') is not None else 2)
            for s in g['shapes']:
                params, kind, role, side, pref = shape_row(s, group)
                if role == 0 and side in eye_pts and s['kind'] != 'text':
                    eye_pts[side] += bounds_of(params, kind, pref)
                if role == 0 and side != 2 and s.get('fill') == 'accent':
                    can_blink = True
        nshapes = len(shapes) - first
        cfirst = len(shapes)
        for c in f['cuts']:
            shape_row(c, 2, cut=True)
        ncuts = len(shapes) - cfirst
        centres = []
        for side, dflt in ((0, 203), (1, 437)):
            p = eye_pts[side]
            if p:
                xs = [q[0] for q in p]
                ys = [q[1] for q in p]
                centres += [(min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2]
            else:
                centres += [dflt, 319]
        r, g_, b = rgb(f['accent'])
        glow = f.get('glow') or {}
        face_rows.append('{"%s","%s",0x%06X,%s,%s,%d,%d,%d,%d,%d,{%s}}' % (
            f['id'], f['category'], (r << 16) | (g_ << 8) | b, fl(float(f['tilt'])),
            fl(float(glow.get('opacity', 0.75))), first, nshapes, cfirst, ncuts,
            1 if can_blink and f['id'] not in CLOSED_EYES else 0,
            ','.join(fl(round(v, 2)) for v in centres)))

    with open(out, 'w') as o:
        o.write('// Generated by tools/make_faces.py from Purple faces.json. Do not edit.\n#pragma once\n')
        o.write('#include "FaceTypes.h"\n\n')
        o.write('static const int16_t FACE_POINTS[] = {%s};\n' % ','.join(map(str, points)))
        o.write('static const Contour FACE_CONTOURS[] = {%s};\n' % ','.join('{%d,%d,%d}' % c for c in contours))
        o.write('static const ShapeDef FACE_SHAPES[] = {\n%s};\n' % ',\n'.join(shapes))
        o.write('static const FaceDef FACES[] = {\n%s};\n' % ',\n'.join(face_rows))
        o.write('static const int FACE_COUNT = %d;\n' % len(face_rows))
    maxpts = 0
    for cs, cn in polys.values():
        maxpts = max(maxpts, sum(contours[i][1] for i in range(cs, cs + cn)))
    print('faces', len(face_rows), 'shapes', len(shapes), 'points', len(points) // 2,
          'contours', len(contours), 'max points/shape', maxpts)


if __name__ == '__main__':
    main()
