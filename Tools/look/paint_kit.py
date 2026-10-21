"""Paint the anime material kit's textures (m4v): seamless 512 px RGBA PNGs, one per kit surface, the painted surface
in RGB and its ink lines in alpha (M_AnimeKit draws alpha in the ink colour, times InkStrength).

    python Tools/look/paint_kit.py --out build/kit/textures
        [--only GrassShort,WallWood] [--paid DIR] [--preview FILE.png] [--kit-json Content/Look/Kit/kit.json]

- Standard library only (the engine's Python has no PIL): seamless value noise, Voronoi cells, soft brush strokes and
  anti-aliased ink lines, all periodic in the tile so the textures repeat without seams.
- Every surface is painted the way an anime background is: flat painted colour, brush variation, and the dark ink
  detail (plank seams, brick and tile courses, cobbles, bark, leaf edges, rock cracks) as the alpha channel.
- --paid DIR: a surface whose <DIR>/<Surface>.png exists (an AI-generated texture) is
  made from that picture instead (downsampled to 512, its dark detail lifted into the ink channel); DERIVED surfaces
  are another's painted source recoloured by a gradient map (the roof families from the red tiles); <DIR>/sky_anime.png
  (a generated sky) goes into imports.json as T_Kit_Sky. See kit.json.
- Writes T_Kit_<Surface>.png for each surface, kit-textures.json (each surface's source, average linear colour and the
  sha256 of its PNG) and imports.json (an import job list: every PNG to /Game/Look/Kit/Textures as
  T_Kit_<Surface>). --kit-json writes the kit table World/Editor/author_kit.py builds the materials from (every surface,
  the generated props it paints through the kit). --preview writes a contact sheet with the ink drawn in.
Deterministic: the same surface always paints the same pixels.
"""
import argparse
import hashlib
import json
import math
import random
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pngio  # noqa: E402

S = 512
N = S * S


# ------------------------------------------------------------------------------------------------ colour helpers
def hexc(code):
    code = code.lstrip('#')
    return tuple(int(code[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def lin(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def scale(c, k):
    return tuple(min(1.0, max(0.0, x * k)) for x in c)


def clamp01(v):
    return 0.0 if v < 0.0 else 1.0 if v > 1.0 else v


def smooth(e0, e1, v):
    t = clamp01((v - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)


# ------------------------------------------------------------------------------------------------ canvas
class Canvas:
    def __init__(self, base):
        self.r = [base[0]] * N
        self.g = [base[1]] * N
        self.b = [base[2]] * N
        self.ink = [0.0] * N

    def blend(self, i, c, a):
        if a <= 0.0:
            return
        if a > 1.0:
            a = 1.0
        self.r[i] += (c[0] - self.r[i]) * a
        self.g[i] += (c[1] - self.g[i]) * a
        self.b[i] += (c[2] - self.b[i]) * a

    def mul(self, i, k):
        self.r[i] *= k
        self.g[i] *= k
        self.b[i] *= k

    def add_ink(self, i, a):
        if a > self.ink[i]:
            self.ink[i] = a if a < 1.0 else 1.0

    def modulate(self, field, amount, centre=0.5):
        """Multiply value by 1 + amount * (field - centre) (brush mottling)."""
        r, g, b = self.r, self.g, self.b
        for i in range(N):
            k = 1.0 + amount * (field[i] - centre)
            r[i] *= k
            g[i] *= k
            b[i] *= k

    def tint_by(self, field, colour, amount):
        """Blend toward colour by amount * field (0..1)."""
        r, g, b = self.r, self.g, self.b
        cr, cg, cb = colour
        for i in range(N):
            t = amount * field[i]
            if t > 0:
                r[i] += (cr - r[i]) * t
                g[i] += (cg - g[i]) * t
                b[i] += (cb - b[i]) * t

    def rgba(self):
        out = bytearray(N * 4)
        for i in range(N):
            o = i * 4
            out[o] = int(clamp01(self.r[i]) * 255 + .5)
            out[o + 1] = int(clamp01(self.g[i]) * 255 + .5)
            out[o + 2] = int(clamp01(self.b[i]) * 255 + .5)
            out[o + 3] = int(clamp01(self.ink[i]) * 255 + .5)
        return out

    def average_linear(self):
        return [sum(lin(clamp01(v)) for v in ch) / N for ch in (self.r, self.g, self.b)]


# ------------------------------------------------------------------------------------------------ fields
def value_noise(period, rng):
    lat = [[rng.random() for _ in range(period)] for _ in range(period)]
    cell = S / period
    axis = []
    for x in range(S):
        f = x / cell
        i0 = int(f) % period
        t = f - int(f)
        axis.append((i0, (i0 + 1) % period, t * t * (3 - 2 * t)))
    out = [0.0] * N
    for y in range(S):
        j0, j1, ty = axis[y]
        r0, r1 = lat[j0], lat[j1]
        base = y * S
        for x in range(S):
            i0, i1, tx = axis[x]
            a = r0[i0] + (r0[i1] - r0[i0]) * tx
            b = r1[i0] + (r1[i1] - r1[i0]) * tx
            out[base + x] = a + (b - a) * ty
    return out


def fbm(rng, base_period=4, octaves=4, gain=0.5):
    total = [0.0] * N
    weight, norm, period = 1.0, 0.0, base_period
    for _ in range(octaves):
        layer = value_noise(period, rng)
        for i in range(N):
            total[i] += layer[i] * weight
        norm += weight
        weight *= gain
        period *= 2
    lo, hi = min(total), max(total)
    span = max(hi - lo, 1e-6)
    return [(v - lo) / span for v in total]


def stretched_noise(rng, px, py, octaves=3):
    """Value noise with different periods along x and y (grain, strata); periodic."""
    total = [0.0] * N
    weight, norm = 1.0, 0.0
    for _ in range(octaves):
        lat = [[rng.random() for _ in range(px)] for _ in range(py)]
        cx, cy = S / px, S / py
        ax = []
        for x in range(S):
            f = x / cx
            i0 = int(f) % px
            t = f - int(f)
            ax.append((i0, (i0 + 1) % px, t * t * (3 - 2 * t)))
        ay = []
        for y in range(S):
            f = y / cy
            j0 = int(f) % py
            t = f - int(f)
            ay.append((j0, (j0 + 1) % py, t * t * (3 - 2 * t)))
        for y in range(S):
            j0, j1, ty = ay[y]
            r0, r1 = lat[j0], lat[j1]
            base = y * S
            for x in range(S):
                i0, i1, tx = ax[x]
                a = r0[i0] + (r0[i1] - r0[i0]) * tx
                b = r1[i0] + (r1[i1] - r1[i0]) * tx
                total[base + x] += (a + (b - a) * ty) * weight
        norm += weight
        weight *= 0.5
        px *= 2
        py *= 2
    lo, hi = min(total), max(total)
    span = max(hi - lo, 1e-6)
    return [(v - lo) / span for v in total]


def voronoi(cells, rng, jitter=0.85, cells_y=None):
    """Periodic Voronoi: per pixel the cell id, the distance to its point (px) and to the nearest cell edge (px)."""
    cy_n = cells_y or cells
    cw, ch = S / cells, S / cy_n
    pts = [[((i + 0.5 + (rng.random() - 0.5) * jitter) * cw, (j + 0.5 + (rng.random() - 0.5) * jitter) * ch)
            for i in range(cells)] for j in range(cy_n)]
    cand = {}
    for j in range(cy_n):
        for i in range(cells):
            lst = []
            for dj in (-2, -1, 0, 1, 2):
                jj, wy = j + dj, 0.0
                if jj < 0:
                    jj += cy_n
                    wy = -S
                elif jj >= cy_n:
                    jj -= cy_n
                    wy = S
                for di in (-2, -1, 0, 1, 2):
                    ii, wx = i + di, 0.0
                    if ii < 0:
                        ii += cells
                        wx = -S
                    elif ii >= cells:
                        ii -= cells
                        wx = S
                    px, py = pts[jj][ii]
                    lst.append((px + wx, py + wy, jj * cells + ii))
            cand[(i, j)] = lst
    ident = [0] * N
    centre = [0.0] * N
    edge = [0.0] * N
    sqrt = math.sqrt
    for y in range(S):
        cj = min(int(y / ch), cy_n - 1)
        for x in range(S):
            ci = min(int(x / cw), cells - 1)
            d1 = d2 = 1e18
            p1 = p2 = None
            for px, py, k in cand[(ci, cj)]:
                dx, dy = px - x, py - y
                d = dx * dx + dy * dy
                if d < d1:
                    d2, p2 = d1, p1
                    d1, p1 = d, (px, py, k)
                elif d < d2:
                    d2, p2 = d, (px, py, k)
            i = y * S + x
            ident[i] = p1[2]
            centre[i] = sqrt(d1)
            sep = sqrt((p1[0] - p2[0]) ** 2 + (p1[1] - p2[1]) ** 2)
            edge[i] = (d2 - d1) / (2.0 * max(sep, 1e-6))
    return ident, centre, edge


def per_cell(rng, count):
    return [rng.random() for _ in range(count)]


# ------------------------------------------------------------------------------------------------ marks
def wrap_points(x, y, reach):
    """The canvas positions a mark centred at (x, y) must also be drawn at so it wraps across the edges."""
    xs = [x] + ([x + S] if x - reach < 0 else []) + ([x - S] if x + reach >= S else [])
    ys = [y] + ([y + S] if y - reach < 0 else []) + ([y - S] if y + reach >= S else [])
    return [(a, b) for a in xs for b in ys]


def stroke(cv, x, y, angle, length, width, colour, opacity, taper=0.0, ink=0.0, point=False):
    """A soft brush stroke (a capsule) centred at x, y. point: a blade that narrows to its tip at +length/2."""
    ca, sa = math.cos(angle), math.sin(angle)
    half = length / 2.0
    reach = half + width + 2
    for ox, oy in wrap_points(x, y, reach):
        x0, x1 = int(ox - reach), int(ox + reach) + 1
        y0, y1 = int(oy - reach), int(oy + reach) + 1
        for py in range(max(0, y0), min(S, y1)):
            dy = py + 0.5 - oy
            row = py * S
            for px in range(max(0, x0), min(S, x1)):
                dx = px + 0.5 - ox
                u = dx * ca + dy * sa
                v = -dx * sa + dy * ca
                if u < -half - width or u > half + width:
                    continue
                if point:
                    t = clamp01((u + half) / (2 * half))
                    w = width * (1.0 - t)
                    if u > half:
                        continue
                    du = max(-half - u, 0.0)
                else:
                    w = width * (1.0 - taper * (u / half) ** 2) if half > 0 else width
                    du = max(abs(u) - half, 0.0)
                d = math.sqrt(du * du + v * v)
                a = clamp01(w * 0.5 + 0.5 - d)
                if a <= 0:
                    continue
                i = row + px
                cv.blend(i, colour, a * opacity)
                if ink:
                    cv.add_ink(i, a * ink)


def disc(cv, x, y, radius, colour, opacity=1.0, rim_ink=0.0, rim=1.2, shade=None):
    """A soft disc; shade=(light, dark): lit from the top left. rim_ink draws an ink ring round it."""
    reach = radius + rim + 2
    for ox, oy in wrap_points(x, y, reach):
        for py in range(max(0, int(oy - reach)), min(S, int(oy + reach) + 1)):
            dy = py + 0.5 - oy
            for px in range(max(0, int(ox - reach)), min(S, int(ox + reach) + 1)):
                dx = px + 0.5 - ox
                d = math.sqrt(dx * dx + dy * dy)
                i = py * S + px
                a = clamp01(radius + 0.5 - d)
                if a > 0:
                    c = colour
                    if shade:
                        t = clamp01(0.5 + (dx + dy) / (2.8 * radius))
                        c = mix(shade[0], shade[1], t)
                    cv.blend(i, c, a * opacity)
                if rim_ink:
                    e = abs(d - radius)
                    cv.add_ink(i, clamp01(rim * 0.5 + 0.5 - e) * rim_ink)


INK_SCALE = 1.8   # m4v round 2: thin lines vanished in the mips at the game camera's distance


def line_ink(cv, x0, y0, x1, y1, width, strength=1.0):
    """An anti-aliased ink segment (wraps), INK_SCALE times wider than asked."""
    width *= INK_SCALE
    mx, my = (x0 + x1) / 2, (y0 + y1) / 2
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    if length < 1e-6:
        return
    ux, uy = dx / length, dy / length
    reach = length / 2 + width + 2
    for ox, oy in wrap_points(mx, my, reach):
        for py in range(max(0, int(oy - reach)), min(S, int(oy + reach) + 1)):
            ry = py + 0.5 - oy
            for px in range(max(0, int(ox - reach)), min(S, int(ox + reach) + 1)):
                rx = px + 0.5 - ox
                u = rx * ux + ry * uy
                v = -rx * uy + ry * ux
                du = max(abs(u) - length / 2, 0.0)
                d = math.sqrt(du * du + v * v)
                a = clamp01(width * 0.5 + 0.5 - d)
                if a > 0:
                    cv.add_ink(py * S + px, a * strength)


def polyline_ink(cv, pts, width, strength=1.0):
    for (a, b), (c, d) in zip(pts, pts[1:]):
        line_ink(cv, a, b, c, d, width, strength)


def crack(cv, rng, x, y, angle, length, width=1.2, strength=0.9, branches=1):
    pts = [(x, y)]
    for _ in range(int(length / 6) + 1):
        angle += rng.uniform(-0.6, 0.6)
        x += math.cos(angle) * 6
        y += math.sin(angle) * 6
        pts.append((x, y))
    polyline_ink(cv, pts, width, strength)
    if branches and len(pts) > 4:
        bx, by = pts[len(pts) // 2]
        crack(cv, rng, bx, by, angle + rng.choice((-1, 1)) * rng.uniform(0.6, 1.2), length * 0.45, width * 0.8,
              strength * 0.8, branches - 1)


def brush_layer(cv, rng, count, length, width, colours, opacity, angle=None, jitter=0.4):
    for _ in range(count):
        a = angle + rng.uniform(-jitter, jitter) if angle is not None else rng.uniform(0, math.pi)
        stroke(cv, rng.uniform(0, S), rng.uniform(0, S), a, length * rng.uniform(0.6, 1.4), width * rng.uniform(0.7, 1.3),
               rng.choice(colours), opacity * rng.uniform(0.5, 1.0), taper=0.6)


# ------------------------------------------------------------------------------------------------ surfaces
# Orientation: M_AnimeKit projects in world space with V growing downward in the picture as the world goes south (on
# the ground) or down (on walls), so every picture is painted the right way up as the game camera sees it.
def planks(cv, rng, rows, base, light, dark, seam_ink=1.0, grain=True, joints=(1, 3), vertical=False, shadow=0.0):
    """Boards in rows (horizontal; vertical=True turns them): per-board tone, grain streaks, ink seams and butt joints."""
    size = S / rows
    tone = [rng.uniform(-1, 1) for _ in range(rows)]
    noise = stretched_noise(rng, 2, 32, 3) if not vertical else stretched_noise(rng, 32, 2, 3)
    for y in range(S):
        for x in range(S):
            a, b = (x, y) if not vertical else (y, x)
            k = int(b / size) % rows
            f = (b % size) / size
            t = 0.5 + 0.18 * tone[k] + 0.35 * (noise[y * S + x] - 0.5)
            c = mix(dark, light, clamp01(t))
            if shadow:
                c = scale(c, 1.0 - shadow * smooth(0.0, 0.22, 0.22 - f))   # under the lip of the board above
            i = y * S + x
            cv.r[i], cv.g[i], cv.b[i] = c
    if grain:
        for _ in range(rows * 14):
            k = rng.randrange(rows)
            b = (k + rng.uniform(0.15, 0.85)) * size
            a = rng.uniform(0, S)
            col = rng.choice((dark, light))
            if not vertical:
                stroke(cv, a, b, 0.0, rng.uniform(30, 140), rng.uniform(1.0, 2.2), col, 0.35, taper=0.8)
            else:
                stroke(cv, b, a, math.pi / 2, rng.uniform(30, 140), rng.uniform(1.0, 2.2), col, 0.35, taper=0.8)
    for k in range(rows):
        b = k * size
        if not vertical:
            line_ink(cv, -2, b, S + 2, b, 2.0, seam_ink)
        else:
            line_ink(cv, b, -2, b, S + 2, 2.0, seam_ink)
        if joints:
            for _ in range(rng.randint(*joints)):
                a = rng.uniform(0, S)
                if not vertical:
                    line_ink(cv, a, b + 1, a, b + size - 1, 1.8, seam_ink * 0.9)
                else:
                    line_ink(cv, b + 1, a, b + size - 1, a, 1.8, seam_ink * 0.9)


def paint_wall_wood(rng):
    """White clapboard siding: 25 cm boards with ink seams and a shadow under each board's lip (Pallet's houses)."""
    cv = Canvas(hexc('f4efe2'))
    planks(cv, rng, 8, None, hexc('fbf8ef'), hexc('e2dccd'), seam_ink=0.95, joints=(0, 1), shadow=0.12)
    cv.modulate(fbm(rng, 4, 3), 0.06)
    for _ in range(40):   # nail heads
        k = rng.randrange(8)
        disc(cv, rng.uniform(0, S), (k + 0.82) * S / 8, 1.3, hexc('9a9282'), 0.7)
    return cv


def paint_wall_plaster(rng):
    cv = Canvas(hexc('f5eedb'))
    cv.modulate(fbm(rng, 3, 4), 0.10)
    brush_layer(cv, rng, 160, 60, 9, (hexc('fbf6e8'), hexc('e9e0c8')), 0.25)
    for _ in range(3):
        crack(cv, rng, rng.uniform(0, S), rng.uniform(0, S), rng.uniform(0, 6.28), rng.uniform(25, 60), 1.0, 0.45)
    return cv


def paint_wall_stone(rng):
    """Cut stone blocks in rows of mixed height, ink joints, a lit top edge and a shaded foot on each block."""
    cv = Canvas(hexc('c9c1b0'))
    noise = fbm(rng, 6, 3)
    rows, y = [], 0.0
    while y < S - 1:
        h = S / 6 * rng.uniform(0.85, 1.15)
        if S - y - h < S / 12:
            h = S - y
        rows.append((y, y + h))
        y += h
    for (y0, y1) in rows:
        x = rng.uniform(0, S)
        start = x
        while x < start + S - 1:
            w = rng.uniform(0.9, 1.6) * S / 6
            if start + S - (x + w) < S / 10:
                w = start + S - x
            tone = rng.uniform(0.9, 1.08)
            col = scale(mix(hexc('c9c1b0'), hexc(rng.choice(('d6cfbf', 'bcb4a3', 'cfc4ad'))), 0.6), tone)
            for py in range(int(y0), min(S, int(y1) + 1)):
                fy = (py - y0) / max(y1 - y0, 1)
                for pxx in range(int(x), int(x + w) + 1):
                    px = pxx % S
                    i = py * S + px
                    lit = 1.0 + 0.10 * smooth(0.25, 0.0, fy) - 0.12 * smooth(0.7, 1.0, fy)
                    c = scale(col, lit * (0.92 + 0.16 * noise[i]))
                    cv.r[i], cv.g[i], cv.b[i] = c
            line_ink(cv, x, y0, x, y1, 2.2, 0.95)
            x += w
        line_ink(cv, -2, y0, S + 2, y0, 2.2, 0.95)
    for _ in range(14):
        crack(cv, rng, rng.uniform(0, S), rng.uniform(0, S), rng.uniform(0, 6.28), rng.uniform(10, 26), 1.0, 0.6, 0)
    return cv


def paint_wall_brick(rng):
    """Sandstone bricks in running bond (Pewter): warm per-brick tones, ink mortar courses, lit tops."""
    cv = Canvas(hexc('e3c08a'))
    noise = fbm(rng, 8, 3)
    rows, cols = 8, 4
    rh, cw = S / rows, S / cols
    tones = {}
    for r in range(rows):
        for c in range(cols):
            tones[(r, c)] = mix(hexc('e5c48f'), hexc(rng.choice(('d9a86a', 'ecd2a2', 'e0b67e', 'd4a064', 'efd8a8'))),
                                rng.uniform(0.3, 0.9))
    for y in range(S):
        r = int(y / rh)
        fy = (y - r * rh) / rh
        off = (cw / 2) if r % 2 else 0.0
        for x in range(S):
            xx = (x + off) % S
            c = int(xx / cw)
            col = tones[(r, c)]
            i = y * S + x
            lit = 1.0 + 0.08 * smooth(0.3, 0.0, fy) - 0.10 * smooth(0.65, 1.0, fy)
            k = lit * (0.9 + 0.2 * noise[i])
            cv.r[i], cv.g[i], cv.b[i] = scale(col, k)
    for r in range(rows):
        y = r * rh
        line_ink(cv, -2, y, S + 2, y, 2.4, 0.9)
        off = (cw / 2) if r % 2 else 0.0
        for c in range(cols):
            x = (c * cw - off) % S
            line_ink(cv, x, y + 1, x, y + rh - 1, 2.4, 0.9)
    for _ in range(26):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        stroke(cv, x, y, rng.uniform(0, 3.14), rng.uniform(4, 10), rng.uniform(1, 2), hexc('b98a52'), 0.5, taper=0.5,
               ink=0.35)
    return cv


def paint_floor_wood(rng):
    """Honey floor planks running east-west, staggered joints, grain and ink seams (Oak's lab, houses)."""
    cv = Canvas(hexc('e6bd83'))
    planks(cv, rng, 10, None, hexc('f0cd95'), hexc('d3a066'), seam_ink=0.8, joints=(1, 2))
    cv.modulate(fbm(rng, 4, 3), 0.06)
    return cv


def paint_floor_tile(rng):
    """Big square tiles, light and a little darker by turns (a room's tint makes them pink and cream, blue and white),
    with grout lines and a painted gloss streak."""
    cv = Canvas(hexc('ffffff'))
    tiles = 4
    t = S / tiles
    noise = fbm(rng, 4, 3)
    for y in range(S):
        for x in range(S):
            a, b = int(x / t), int(y / t)
            base = 1.0 if (a + b) % 2 == 0 else 0.86
            fx, fy = (x % t) / t, (y % t) / t
            gloss = 0.035 * smooth(0.06, 0.0, abs((fx + fy) - 0.7))
            k = base * (0.975 + 0.05 * noise[y * S + x]) + gloss
            i = y * S + x
            cv.r[i] = cv.g[i] = cv.b[i] = min(1.0, k)
    for k in range(tiles):
        line_ink(cv, k * t, -2, k * t, S + 2, 2.0, 0.55)
        line_ink(cv, -2, k * t, S + 2, k * t, 2.0, 0.55)
    return cv


def flagstones(rng, cells, base, variants, ink=0.95, ink_w=2.2, grit=None, chips=0):
    ident, centre, edge = voronoi(cells, rng, 0.9)
    tone = [mix(base, hexc(rng.choice(variants)), rng.uniform(0.2, 0.9)) for _ in range(cells * cells)]
    noise = fbm(rng, 8, 3)
    cv = Canvas(base)
    for i in range(N):
        e = edge[i]
        col = tone[ident[i]]
        k = (0.9 + 0.2 * noise[i]) * (1.0 - 0.10 * smooth(5.0, 0.0, e))
        cv.r[i], cv.g[i], cv.b[i] = scale(col, k)
        cv.add_ink(i, clamp01(ink_w * 0.5 + 0.5 - e) * ink)
        if grit and e < 4.0 and noise[(i * 7919) % N] > 0.72:
            cv.blend(i, grit, 0.6)
    for _ in range(chips):
        crack(cv, rng, rng.uniform(0, S), rng.uniform(0, S), rng.uniform(0, 6.28), rng.uniform(10, 30), 1.0, 0.6, 0)
    return cv


def paint_floor_stone(rng):
    """Brock's arena: dry sandstone slabs, cracked, warm ochre (also the gym floors)."""
    return flagstones(rng, 9, hexc('d9b47e'), ('cf9a5c', 'e2c493', 'd8ab70', 'e8cd9d'), 0.95, 2.0, chips=30)


def paint_cave_floor(rng):
    """Mt. Moon's floor: blue-grey flagstones with gravel in the joints."""
    return flagstones(rng, 7, hexc('8f9cad'), ('7d8a9c', 'a2aebd', '8794a6', '97a3b5'), 0.95, 2.4,
                      grit=hexc('5a6372'), chips=16)


def paint_path_paving(rng):
    """Cobbles: rounded stones lit from the top left, ink between them."""
    ident, centre, edge = voronoi(12, rng, 0.8)
    tone = [mix(hexc('d8cbb0'), hexc(rng.choice(('c9b996', 'e4d8bf', 'cdbf9f'))), rng.uniform(0.2, 0.9))
            for _ in range(144)]
    cv = Canvas(hexc('d8cbb0'))
    for i in range(N):
        e = edge[i]
        dome = smooth(0.0, 10.0, e)
        col = scale(tone[ident[i]], 0.82 + 0.22 * dome)
        cv.r[i], cv.g[i], cv.b[i] = col
        cv.add_ink(i, clamp01(1.4 - e) * 0.9)
    return cv


def paint_path_dirt(rng):
    """Sandy dirt: soft painted mottling, darker patches, a few pebbles outlined in ink."""
    cv = Canvas(hexc('e8c98c'))
    big = fbm(rng, 3, 4)
    cv.modulate(big, 0.16)
    cv.tint_by([smooth(0.62, 0.8, v) for v in fbm(rng, 5, 3)], hexc('d2a868'), 0.45)
    brush_layer(cv, rng, 260, 26, 5, (hexc('f1d7a2'), hexc('d9b273')), 0.28)
    for _ in range(46):
        r = rng.uniform(2.0, 4.5)
        disc(cv, rng.uniform(0, S), rng.uniform(0, S), r, None, 1.0, rim_ink=0.75, rim=1.2,
             shade=(hexc('efe2c6'), hexc('b99c6c')))
    for _ in range(90):
        stroke(cv, rng.uniform(0, S), rng.uniform(0, S), rng.uniform(0, 3.14), rng.uniform(3, 7), 1.0, hexc('a07c4a'),
               0.6, ink=0.25)
    return cv


def paint_sand(rng):
    cv = Canvas(hexc('f2dfa6'))
    cv.modulate(fbm(rng, 4, 3), 0.10)
    warp = fbm(rng, 3, 2)
    for y in range(S):
        for x in range(S):
            i = y * S + x
            v = math.sin((y / S) * 2 * math.pi * 7 + warp[i] * 6.0)
            k = 1.0 + 0.05 * v
            cv.mul(i, k)
            if v > 0.97:
                cv.add_ink(i, 0.25)
    for _ in range(300):
        stroke(cv, rng.uniform(0, S), rng.uniform(0, S), 0, 1.5, 1.4, hexc('c9ad74'), 0.7)
    return cv


def paint_grass_short(rng):
    """Short meadow grass: two greens of painted blade tufts over a mottled base, a few inked blades."""
    cv = Canvas(hexc('78b84a'))
    cv.modulate(fbm(rng, 4, 4), 0.20)
    cv.tint_by([smooth(0.55, 0.8, v) for v in fbm(rng, 6, 3)], hexc('5f9e3c'), 0.5)
    for layer, (count, cols, op) in enumerate(((900, (hexc('5c9a38'), hexc('4e8a32')), 0.75),
                                                (900, (hexc('92cc58'), hexc('a3d763')), 0.7),
                                                (500, (hexc('6aa840'),), 0.8))):
        for _ in range(count):
            x, y = rng.uniform(0, S), rng.uniform(0, S)
            blades = rng.randint(2, 4)
            for b in range(blades):
                a = -math.pi / 2 + rng.uniform(-0.6, 0.6)
                ln = rng.uniform(7, 15)
                stroke(cv, x + math.cos(a) * ln / 2, y + math.sin(a) * ln / 2, a, ln, rng.uniform(2.0, 3.2),
                       rng.choice(cols), op, point=True)
    for _ in range(170):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        a = -math.pi / 2 + rng.uniform(-0.5, 0.5)
        ln = rng.uniform(6, 12)
        stroke(cv, x + math.cos(a) * ln / 2, y + math.sin(a) * ln / 2, a, ln, 1.6, hexc('3e7a2c'), 0.8, point=True,
               ink=0.7)
    return cv


def paint_grass_tall(rng):
    """Tall grass (the swaying cones): upright blade strokes, deep greens, lighter tips."""
    cv = Canvas(hexc('3f9a45'))
    cv.modulate(stretched_noise(rng, 24, 3, 3), 0.25)
    for _ in range(1400):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        a = -math.pi / 2 + rng.uniform(-0.25, 0.25)
        ln = rng.uniform(30, 70)
        stroke(cv, x, y, a, ln, rng.uniform(3, 6), rng.choice((hexc('2f7d3c'), hexc('4fae4f'), hexc('62bd55'))), 0.6,
               point=True)
    for _ in range(140):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        a = -math.pi / 2 + rng.uniform(-0.2, 0.2)
        stroke(cv, x, y, a, rng.uniform(25, 50), 1.6, hexc('245f2c'), 0.8, point=True, ink=0.55)
    return cv


def paint_flowers(rng):
    """Petal paint: soft round dabs with a shaded side and an ink speck at the heart (tinted per bed colour)."""
    cv = Canvas(hexc('f6f4ee'))
    for _ in range(220):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        disc(cv, x, y, rng.uniform(8, 16), None, 0.9, shade=(hexc('ffffff'), hexc('d9d4c8')))
    for _ in range(120):
        disc(cv, rng.uniform(0, S), rng.uniform(0, S), 2.0, hexc('e9c84a'), 0.9, rim_ink=0.5)
    return cv


def paint_water(rng):
    """Painted water: blue depths, a light caustic net and white glints, thin ink on the brightest ripples."""
    cv = Canvas(hexc('4aa8e8'))
    deep = fbm(rng, 3, 3)
    cv.tint_by(deep, hexc('2f7fc4'), 0.6)
    ident, centre, edge = voronoi(7, rng, 0.9)
    for i in range(N):
        e = edge[i]
        a = smooth(4.5, 0.5, e)
        if a > 0:
            cv.blend(i, hexc('bfe8ff'), a * 0.85)
        if e < 1.0 and deep[i] > 0.55:
            cv.add_ink(i, 0.35)
    for _ in range(40):
        stroke(cv, rng.uniform(0, S), rng.uniform(0, S), rng.uniform(-0.3, 0.3), rng.uniform(10, 24), 2.4,
               hexc('ffffff'), 0.9, taper=0.9)
    return cv


def rock(rng, base, variants, light, dark, cells=5, crack_ink=0.95):
    """Faceted rock: each Voronoi facet lit by its own tilt, ink cracks along most facet edges, grain."""
    ident, centre, edge = voronoi(cells, rng, 0.95)
    count = cells * cells
    tilt = [rng.uniform(-1, 1) for _ in range(count)]
    tone = [mix(base, hexc(rng.choice(variants)), rng.uniform(0.2, 0.8)) for _ in range(count)]
    draw = [rng.random() < 0.8 for _ in range(count)]
    noise = fbm(rng, 8, 3)
    cv = Canvas(base)
    for i in range(N):
        k = ident[i]
        t = clamp01(0.5 + 0.35 * tilt[k] + 0.25 * (noise[i] - 0.5))
        c = mix(dark, light, t)
        c = mix(c, tone[k], 0.5)
        e = edge[i]
        c = scale(c, 1.0 - 0.15 * smooth(6.0, 0.0, e))
        cv.r[i], cv.g[i], cv.b[i] = c
        if draw[k] or e < 0.6:
            cv.add_ink(i, clamp01(1.6 - e) * crack_ink)
    for _ in range(22):
        crack(cv, rng, rng.uniform(0, S), rng.uniform(0, S), rng.uniform(0, 6.28), rng.uniform(16, 40), 1.1, 0.75, 1)
    return cv


def paint_rock_grey(rng):
    return rock(rng, hexc('a7a39b'), ('b5b1a8', '98948c', 'aca59a'), hexc('c6c2b9'), hexc('7d7a73'))


def paint_rock_cave(rng):
    """Mt. Moon's rock: blue-grey facets, deep cracks."""
    return rock(rng, hexc('7f8ea4'), ('8a99ae', '6f7d92', '93a0b3'), hexc('a3b1c4'), hexc('56627a'), 4)


def paint_tree_canopy(rng):
    """Leaf clumps: overlapping scalloped clusters lit from the top left, an ink arc under each clump."""
    cv = Canvas(hexc('3f8a3c'))
    light, mid, dark = hexc('6fb85a'), hexc('4f9c45'), hexc('2f6e33')
    for _ in range(520):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        r = rng.uniform(10, 22)
        disc(cv, x, y, r, None, 1.0, shade=(light, dark))
        # the shaded underside and its ink arc
        for k in range(7):
            a = math.pi * (0.15 + 0.7 * k / 6)
            ax, ay = x + math.cos(a) * r * 0.92, y + math.sin(a) * r * 0.92
            bx, by = x + math.cos(a + 0.35) * r * 0.92, y + math.sin(a + 0.35) * r * 0.92
            line_ink(cv, ax, ay, bx, by, 1.6, 0.55)
    for _ in range(260):
        stroke(cv, rng.uniform(0, S), rng.uniform(0, S), rng.uniform(0, 3.14), rng.uniform(4, 8), 3.0, mid, 0.5,
               taper=0.8)
    return cv


def paint_tree_trunk(rng):
    """Bark: upright brush strokes in browns, wavy ink lines and a knot or two."""
    cv = Canvas(hexc('8a5a3a'))
    cv.modulate(stretched_noise(rng, 16, 2, 3), 0.3)
    for _ in range(500):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        stroke(cv, x, y, math.pi / 2 + rng.uniform(-0.08, 0.08), rng.uniform(30, 90), rng.uniform(2, 5),
               rng.choice((hexc('a06c45'), hexc('6e4428'), hexc('94603c'))), 0.5, taper=0.8)
    for _ in range(26):
        x = rng.uniform(0, S)
        y = rng.uniform(0, S)
        pts = []
        for k in range(14):
            pts.append((x + math.sin(k * 0.7 + rng.uniform(-0.3, 0.3)) * 2.5, y + k * 8))
        polyline_ink(cv, pts, 1.5, 0.85)
    for _ in range(4):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        disc(cv, x, y, 7, hexc('6e4428'), 1.0, rim_ink=0.85, rim=1.6)
    return cv


def paint_fence(rng):
    """Painted wooden posts: cream wood with upright brush strokes, wavy grain lines and knots in ink, a worn darker
    band every so often (round 3: the reviewer read plain white posts as untextured)."""
    cv = Canvas(hexc('efe6d0'))
    cv.modulate(stretched_noise(rng, 20, 2, 3), 0.16)
    for _ in range(340):
        stroke(cv, rng.uniform(0, S), rng.uniform(0, S), math.pi / 2, rng.uniform(40, 140), rng.uniform(3, 6),
               rng.choice((hexc('fbf6ea'), hexc('d9cdb0'), hexc('e6dbc2'))), 0.5, taper=0.8)
    for _ in range(40):
        x, y = rng.uniform(0, S), rng.uniform(0, S)
        pts = [(x + math.sin(k * 0.5 + x) * 4, y + k * 12) for k in range(rng.randint(6, 14))]
        polyline_ink(cv, pts, 4.2, 1.0)  # round 2: bolder still   # round 13: grain lines bold enough to read on a post at the game camera's distance
    for _ in range(6):
        disc(cv, rng.uniform(0, S), rng.uniform(0, S), rng.uniform(4, 7), hexc('b39c74'), 1.0, rim_ink=0.8, rim=1.4)
    return cv


def paint_ledge(rng):
    """A ledge's face: earth strata in browns with ink bands and pebbles."""
    cv = Canvas(hexc('a8743e'))
    strata = stretched_noise(rng, 2, 12, 3)
    for i in range(N):
        cv.mul(i, 0.82 + 0.36 * strata[i])
    for k in range(5):
        y = (k + 0.5) * S / 5 + rng.uniform(-8, 8)
        pts = [(x, y + math.sin(x / 40.0 + k) * 4) for x in range(-10, S + 20, 12)]
        polyline_ink(cv, pts, 1.8, 0.8)
    for _ in range(60):
        disc(cv, rng.uniform(0, S), rng.uniform(0, S), rng.uniform(2.5, 5), None, 1.0, rim_ink=0.7,
             shade=(hexc('c89a64'), hexc('7a5230')))
    return cv


def paint_sign(rng):
    """A signboard's planks: honey wood, grain and ink seams."""
    cv = Canvas(hexc('d8b070'))
    planks(cv, rng, 6, None, hexc('e6c287'), hexc('c49558'), seam_ink=0.9, joints=(0, 1))
    return cv


def paint_counter(rng):
    """A counter's or shelf's front: upright wood panels with ink seams (tinted per piece)."""
    cv = Canvas(hexc('d29a5c'))
    planks(cv, rng, 6, None, hexc('e2ae70'), hexc('bf8649'), seam_ink=0.8, joints=None, vertical=True)
    cv.modulate(fbm(rng, 4, 3), 0.05)
    return cv


def paint_carpet(rng):
    """Woven carpet: a fine soft weave and a darker border band (tinted per carpet)."""
    cv = Canvas(hexc('e9e9e9'))
    for y in range(S):
        for x in range(S):
            i = y * S + x
            w = 0.03 * math.sin((x + y) * 0.9) + 0.03 * math.sin((x - y) * 0.9)
            cv.mul(i, 1.0 + w)
    cv.modulate(fbm(rng, 6, 3), 0.10)
    return cv


def roof(rng, light, base, dark):
    """Barrel roof tiles in courses: each tile rounded (lit left, shaded right), a scalloped lip at its foot with an
    ink outline and a shadow on the course below. 8 courses and 8 tiles across a 2 m tile of texture."""
    cv = Canvas(base)
    rows, cols = 8, 8
    rh, cw = S / rows, S / cols
    noise = fbm(rng, 8, 3)
    tone = [[rng.uniform(-1, 1) for _ in range(cols)] for _ in range(rows)]
    for y in range(S):
        r = int(y / rh)
        fy = (y - r * rh) / rh
        off = (cw / 2) if r % 2 else 0.0
        for x in range(S):
            xx = (x + off) % S
            c = int(xx / cw)
            fx = (xx - c * cw) / cw
            # The lip: the foot of each tile bulges down in an arc; below the arc is the next course in shadow.
            lip = 0.80 + 0.14 * math.sin(fx * math.pi)
            i = y * S + x
            if fy > lip:
                col = scale(dark, 0.85)
            else:
                barrel = math.sin(fx * math.pi)
                t = clamp01(0.25 + 0.55 * barrel - 0.25 * fx + 0.1 * tone[r][c] + 0.2 * (noise[i] - 0.5))
                col = mix(dark, light, t)
                col = scale(col, 1.0 - 0.18 * smooth(0.35, 0.0, fy))   # in the shade of the lip above
            cv.r[i], cv.g[i], cv.b[i] = col
    for r in range(rows):
        off = (cw / 2) if r % 2 else 0.0
        for c in range(cols + 1):
            x0 = c * cw - off
            pts = []
            for k in range(13):
                fx = k / 12
                pts.append((x0 + fx * cw, r * rh + (0.80 + 0.14 * math.sin(fx * math.pi)) * rh))
            polyline_ink(cv, pts, 2.0, 0.95)
            line_ink(cv, x0, r * rh + 0.1 * rh, x0, r * rh + 0.8 * rh, 1.6, 0.6)
    for _ in range(140):
        stroke(cv, rng.uniform(0, S), rng.uniform(0, S), math.pi / 2, rng.uniform(6, 16), 2.0, light, 0.35, taper=0.8)
    return cv


def paint_roof_red(rng):
    return roof(rng, hexc('ef7b5c'), hexc('d2553f'), hexc('9a3426'))


def paint_roof_blue(rng):
    return roof(rng, hexc('7397cd'), hexc('4c6fa8'), hexc('2c4676'))


def paint_roof_grey(rng):
    return roof(rng, hexc('b4b7bc'), hexc('8c8f94'), hexc('5c5f66'))


def paint_roof_brown(rng):
    return roof(rng, hexc('b08866'), hexc('8a6448'), hexc('5a3e2c'))


def paint_paint(rng):
    """A plain painted surface: soft brush variation and nothing else (props, books, plates; tinted per piece)."""
    cv = Canvas(hexc('f0f0f0'))
    cv.modulate(fbm(rng, 3, 4), 0.08)
    brush_layer(cv, rng, 200, 50, 8, (hexc('fafafa'), hexc('e2e2e2')), 0.3)
    return cv


def paint_glass(rng):
    """Window glass: pale blue, darker toward the foot, and the anime double shine streak."""
    cv = Canvas(hexc('bfe3f5'))
    for y in range(S):
        for x in range(S):
            i = y * S + x
            cv.mul(i, 1.04 - 0.14 * (y / S))
            d = ((x + y) % 256) - 120
            if -16 < d < 2 or 10 < d < 16:
                cv.blend(i, hexc('ffffff'), 0.75)
    return cv


def paint_metal(rng):
    """Painted machine panels: pale grey, brushed, ink seams every half metre, rivets."""
    cv = Canvas(hexc('c8ccd4'))
    cv.modulate(stretched_noise(rng, 2, 40, 2), 0.06)
    for k in range(3):
        y = k * S / 3
        line_ink(cv, -2, y, S + 2, y, 2.0, 0.8)
        for j in range(8):
            disc(cv, (j + 0.5) * S / 8, y + 9, 2.2, hexc('9aa0aa'), 1.0, rim_ink=0.6)
    for k in range(2):
        x = k * S / 2
        line_ink(cv, x, -2, x, S + 2, 2.0, 0.8)
    return cv


def paint_ink(rng):
    """Ink: the outline colour itself (rims, the dark beyond doorways)."""
    cv = Canvas(hexc('1f1f2e'))
    cv.modulate(fbm(rng, 4, 3), 0.15)
    for i in range(N):
        cv.ink[i] = 1.0
    return cv


# name: (painter, tiling cm, ink strength, cel steps, blend sharpness, note)
SURFACES = {
    'WallWood': (paint_wall_wood, 200, 1.0, 0, 6, 'white clapboard siding'),
    'WallPlaster': (paint_wall_plaster, 300, 0.8, 0, 6, 'cream plaster'),
    'WallStone': (paint_wall_stone, 250, 1.0, 0, 6, 'cut stone blocks'),
    'WallBrick': (paint_wall_brick, 220, 1.0, 0, 6, 'sandstone brick, running bond'),
    'FloorWood': (paint_floor_wood, 250, 0.9, 0, 6, 'honey floor planks'),
    'FloorTile': (paint_floor_tile, 200, 0.8, 0, 6, 'big square tiles, two tones'),
    'FloorStone': (paint_floor_stone, 450, 1.0, 0, 6, 'cracked sandstone slabs'),
    'RoofRed': (paint_roof_red, 200, 1.0, 0, 12, 'red barrel tiles'),
    'RoofBlue': (paint_roof_blue, 200, 1.0, 0, 12, 'slate-blue barrel tiles'),
    'RoofGrey': (paint_roof_grey, 200, 1.0, 0, 12, 'grey barrel tiles'),
    'RoofBrown': (paint_roof_brown, 200, 1.0, 0, 12, 'brown barrel tiles'),
    'PathDirt': (paint_path_dirt, 700, 0.9, 0, 6, 'sandy dirt with pebbles'),
    'PathPaving': (paint_path_paving, 200, 1.0, 0, 6, 'cobbles'),
    'GrassShort': (paint_grass_short, 600, 0.8, 0, 6, 'short meadow grass, blade tufts'),
    'GrassTall': (paint_grass_tall, 45, 0.6, 0, 6, 'tall grass blades'),
    'Flowers': (paint_flowers, 40, 0.6, 0, 6, 'petal dabs'),
    'RockGrey': (paint_rock_grey, 350, 1.0, 0, 6, 'grey faceted rock, ink cracks'),
    'RockCave': (paint_rock_cave, 700, 1.0, 0, 6, 'blue-grey cave rock, ink cracks'),
    'CaveFloor': (paint_cave_floor, 480, 1.0, 0, 6, 'blue-grey flagstones, gravel'),
    'Water': (paint_water, 550, 0.8, 0, 6, 'painted water, caustic net'),
    'Sand': (paint_sand, 300, 0.6, 0, 6, 'sand ripples'),
    'TreeCanopy': (paint_tree_canopy, 200, 0.9, 0, 4, 'leaf clumps'),
    'TreeTrunk': (paint_tree_trunk, 70, 1.0, 0, 6, 'bark'),
    'Fence': (paint_fence, 90, 0.9, 0, 6, 'white painted posts'),
    'Ledge': (paint_ledge, 100, 1.0, 0, 6, 'earth strata'),
    'Sign': (paint_sign, 100, 1.0, 0, 6, 'signboard planks'),
    'Counter': (paint_counter, 150, 0.9, 0, 6, 'wood panels'),
    'Carpet': (paint_carpet, 100, 0.0, 0, 6, 'woven carpet'),
    'Paint': (paint_paint, 200, 0.0, 0, 6, 'plain painted colour'),
    'Glass': (paint_glass, 60, 0.0, 0, 6, 'window glass with shine'),
    'Metal': (paint_metal, 150, 0.9, 0, 6, 'painted machine panels'),
    'Ink': (paint_ink, 100, 1.0, 0, 6, 'outline ink'),
}
# Round 4: the rim ink per surface (M_AnimeKit RimInk) and the foliage master's scalloped crowns.
# Round 5: Detail (the paint's contrast kept) calms the ground, which read as noise at the game camera's distance.
LOOK_EXTRA = {'Fence': {'rim_ink': 0.0, 'detail': 0.8, 'shade_amount': 1.0, 'ink': 1.5}, 'Counter': {'rim_ink': 0.0, 'detail': 0.6, 'ink': 1.4},
              'FloorWood': {'detail': 0.6, 'ink': 1.6}, 'Glass': {'detail': 1.0}, 'Metal': {'rim_ink': 0.0, 'detail': 0.5}, 'Sign': {'detail': 0.6}, 'Flowers': {'rim_ink': 0.9}, 'TreeCanopy': {'rim_ink': 0.0, 'detail': 1.0},
              'TreeTrunk': {'rim_ink': 0.5, 'shade_amount': 0.8, 'ink': 1.6}, 'RockGrey': {'rim_ink': 0.4}, 'RockCave': {'rim_ink': 0.4}, 'GrassTall': {'rim_ink': 0.0, 'detail': 0.9},
              'GrassShort': {'detail': 0.45}, 'Water': {'detail': 0.45, 'ink': 0.05}, 'PathDirt': {'detail': 0.65},
              'Sand': {'detail': 0.6}, 'FloorStone': {'detail': 0.7, 'ink': 1.2}, 'CaveFloor': {'detail': 0.8}, 'RoofRed': {'detail': 0.75, 'ink': 1.5},
              'RoofBlue': {'detail': 0.75, 'ink': 1.5}, 'RoofGrey': {'detail': 0.75, 'ink': 1.5}, 'RoofBrown': {'detail': 0.75, 'ink': 1.5}, 'WallWood': {'detail': 1.0, 'ink': 1.4}}

# The generated props (Content/Props/<id>) drawn through the kit (MI_KitProp_<id>: their own pictures, the kit's flat tones).
PROPS = ['dome_fossil', 'fossil_skeleton', 'gym_boulder', 'healing_machine', 'helix_fossil', 'item_ball', 'league_statue', 'mart_shelf',
         'moon_stone', 'pc_terminal', 'potted_plant', 'signpost', 'stalagmite_cluster', 'stalagmite_tall']
# Surfaces made from another surface's painted source, recoloured by a gradient map on its luminance (dark, mid, light):
# the roof families from the painted red tiles.
DERIVED = {
    'RoofBlue': ('RoofRed', ('243c66', '4a6ca4', '7290c2')),
    'RoofGrey': ('RoofRed', ('4f525a', '8c8f94', 'c2c5ca')),
    'RoofBrown': ('RoofRed', ('4e3424', '8a6448', 'c49a74')),
    # Round 3: wood furniture and signs from the painted planks, cut stone from the painted bricks, ledge faces from the
    # painted rock.
    'Counter': ('FloorWood', ('6e4424', 'c98e52', 'efca94')),
    'Sign': ('FloorWood', ('7a5228', 'd8b070', 'f4dca8')),
    'WallStone': ('WallBrick', ('6a665e', 'bcb5a6', 'e6e1d6')),
    'Ledge': ('RockGrey', ('4a3422', 'a8743e', 'd8a86e')),
}
REQUIRED = ['WallWood', 'WallPlaster', 'WallStone', 'WallBrick', 'FloorWood', 'FloorTile', 'FloorStone', 'RoofRed',
            'RoofBlue', 'RoofGrey', 'RoofBrown', 'PathDirt', 'PathPaving', 'GrassShort', 'GrassTall', 'Flowers',
            'RockGrey', 'RockCave', 'CaveFloor', 'Water', 'Sand', 'TreeCanopy', 'TreeTrunk', 'Fence', 'Ledge', 'Sign',
            'Counter', 'Carpet']


# ------------------------------------------------------------------------------------------------ paid sources
# Round 9: the painted sources' ink lines are 1-2 px wide and vanished in the mips at the game camera's distance; these
# surfaces thicken them (a max filter of this radius, px at 512).
INK_DILATE = {'WallWood': 1, 'FloorWood': 1, 'TreeTrunk': 1}


def dilate(field, radius):
    out = field[:]
    for _ in range(radius):
        src = out[:]
        for y in range(S):
            up, dn = ((y - 1) % S) * S, ((y + 1) % S) * S
            row = y * S
            for x in range(S):
                l, r = (x - 1) % S, (x + 1) % S
                out[row + x] = max(src[row + x], src[row + l], src[row + r], src[up + x], src[dn + x])
    return out


def from_paid(path, name=''):
    """A Scenario texture, downsampled to 512 (2x2 boxes in linear light), its dark detail lifted into the ink."""
    img = pngio.read(path)
    if img.w != img.h or img.w % S:
        raise ValueError(f'{path}: expected a square multiple of {S} px, got {img.w}x{img.h}')
    f = img.w // S
    ch = img.channels
    data = img.data
    table = [lin(v / 255.0) for v in range(256)]
    cv = Canvas((0, 0, 0))
    for y in range(S):
        for x in range(S):
            acc = [0.0, 0.0, 0.0]
            for dy in range(f):
                row = ((y * f + dy) * img.w + x * f) * ch
                for dx in range(f):
                    o = row + dx * ch
                    acc[0] += table[data[o]]
                    acc[1] += table[data[o + 1]]
                    acc[2] += table[data[o + 2]]
            i = y * S + x
            k = 1.0 / (f * f)
            cv.r[i], cv.g[i], cv.b[i] = (((a * k) ** (1 / 2.4)) * 1.055 - 0.055 if a * k > 0.0031308 else a * k * 12.92
                                         for a in acc)
    # Ink: pixels much darker than their neighbourhood (a 9 px box), the lines the painter drew.
    lum = [0.3 * cv.r[i] + 0.59 * cv.g[i] + 0.11 * cv.b[i] for i in range(N)]
    blur = box_blur(lum, 4)
    for i in range(N):
        cv.ink[i] = smooth(0.06, 0.22, blur[i] - lum[i]) * 0.95
    if INK_DILATE.get(name):
        cv.ink = dilate(cv.ink, INK_DILATE[name])
    return cv


def gradient_map(cv, dark, mid, light):
    """Recolour by luminance: the 5th..95th percentile of the picture's luminance runs dark -> mid -> light."""
    lum = [0.3 * cv.r[i] + 0.59 * cv.g[i] + 0.11 * cv.b[i] for i in range(N)]
    ranked = sorted(lum)
    lo, hi = ranked[N // 20], ranked[N * 19 // 20]
    span = max(hi - lo, 1e-4)
    d, m, l = hexc(dark), hexc(mid), hexc(light)
    for i in range(N):
        t = clamp01((lum[i] - lo) / span)
        c = mix(d, m, t * 2) if t < 0.5 else mix(m, l, (t - 0.5) * 2)
        cv.r[i], cv.g[i], cv.b[i] = c
    return cv


def box_blur(field, radius):
    out = field[:]
    for horizontal in (True, False):
        src = out[:]
        for a in range(S):
            window = 0.0
            for k in range(-radius, radius + 1):
                window += src[(a * S + (k % S)) if horizontal else ((k % S) * S + a)]
            for b in range(S):
                i = a * S + b if horizontal else b * S + a
                out[i] = window / (2 * radius + 1)
                add = (b + radius + 1) % S
                sub = (b - radius) % S
                window += src[a * S + add if horizontal else add * S + a] - src[a * S + sub if horizontal else sub * S + a]
    return out


# ------------------------------------------------------------------------------------------------ main
def preview(cvs, path):
    """A contact sheet: each texture at 256 px with its ink drawn in, 8 to a row."""
    names = list(cvs)
    cols = 8
    rows = (len(names) + cols - 1) // cols
    t = 256
    w, h = cols * t, rows * t
    out = bytearray(w * h * 3)
    ink = hexc('1f1f2e')
    for n, name in enumerate(names):
        cv = cvs[name]
        ox, oy = (n % cols) * t, (n // cols) * t
        for y in range(t):
            for x in range(t):
                i = (y * 2) * S + x * 2
                a = cv.ink[i]
                c = (cv.r[i] + (ink[0] - cv.r[i]) * a, cv.g[i] + (ink[1] - cv.g[i]) * a, cv.b[i] + (ink[2] - cv.b[i]) * a)
                o = ((oy + y) * w + ox + x) * 3
                out[o:o + 3] = bytes(int(clamp01(v) * 255) for v in c)
    pngio.write(path, w, h, out, channels=3)
    return names


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', required=True)
    ap.add_argument('--only', default='')
    ap.add_argument('--paid', default='')
    ap.add_argument('--preview', default='')
    ap.add_argument('--kit-json', default='')
    args = ap.parse_args(argv)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    wanted = [s for s in args.only.split(',') if s] or list(SURFACES)
    manifest_path = out / 'kit-textures.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8')) if manifest_path.is_file() else {}
    done = {}
    for name in wanted:
        painter, tiling, ink, cel, sharp, note = SURFACES[name]
        paid = Path(args.paid) / f'{name}.png' if args.paid else None
        derived = DERIVED.get(name)
        from_png = Path(args.paid) / f'{derived[0]}.png' if derived and args.paid else None
        if paid and paid.is_file():
            cv = from_paid(paid, name)
            source = {'kind': 'paid', 'file': str(paid), 'sha256': hashlib.sha256(paid.read_bytes()).hexdigest()}
        elif from_png and from_png.is_file():
            cv = gradient_map(from_paid(from_png, derived[0]), *derived[1])
            source = {'kind': 'derived', 'from': derived[0], 'file': str(from_png), 'gradient': list(derived[1]),
                      'sha256': hashlib.sha256(from_png.read_bytes()).hexdigest()}
        else:
            cv = painter(random.Random(f'pyr-m4v-{name}'))
            source = {'kind': 'procedural', 'painter': painter.__name__}
        png = out / f'T_Kit_{name}.png'
        pngio.write(png, S, S, cv.rgba())
        manifest[name] = {'texture': f'/Game/Look/Kit/Textures/T_Kit_{name}', 'png': png.name, 'size': S,
                          'tiling_cm': tiling, 'ink_strength': ink, 'cel_steps': cel, 'blend_sharpness': sharp,
                          'note': note, 'source': source, 'average_linear': [round(v, 5) for v in cv.average_linear()],
                          'sha256': hashlib.sha256(png.read_bytes()).hexdigest()}
        done[name] = cv
        print(f'{name}: {source["kind"]} -> {png}', flush=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    imports = {'imports': [{'kind': 'texture', 'file': str((out / spec['png']).resolve()), 'dest': '/Game/Look/Kit/Textures',
                            'name': f'Kit_{name}'} for name, spec in manifest.items()]}
    # The painted sky panorama (a generated sky), imported as a sky texture (no mips, clamped V) for M_Sky's SkyTex.
    sky = Path(args.paid) / 'sky_anime.png' if args.paid else None
    if sky and sky.is_file():
        imports['imports'].append({'kind': 'texture', 'file': str(sky.resolve()), 'dest': '/Game/Look/Kit/Textures', 'name': 'Kit_Sky',
                                   'sky': True})
    (out / 'imports.json').write_text(json.dumps(imports, indent=2) + '\n', encoding='utf-8')
    if args.kit_json:
        surfaces = {}
        for name in SURFACES:
            if name not in manifest:
                raise SystemExit(f'{name} is not painted yet (run without --only first)')
            spec = {k: manifest[name][k] for k in ('texture', 'tiling_cm', 'ink_strength', 'cel_steps', 'blend_sharpness', 'average_linear',
                                                   'note', 'source', 'sha256')}
            if name == 'GrassTall':
                spec['sway'] = True
            extra = dict(LOOK_EXTRA.get(name, {}))
            if 'ink' in extra:
                spec['ink_strength'] = extra.pop('ink')
            spec.update(extra)
            spec['required'] = name in REQUIRED
            surfaces[name] = spec
        kit = {'about': 'm4v anime material kit: Tools/look/paint_kit.py paints T_Kit_<Surface> (paint in RGB, ink in alpha), '
                        'the editor import brings them to /Game/Look/Kit/Textures, World/Editor/author_kit.py builds M_AnimeKit, '
                        'MI_Kit_<Surface> and MI_KitProp_<prop> from this table. See README.md beside it.',
               'surfaces': surfaces, 'props': PROPS, 'prop_cel_steps': 4}
        Path(args.kit_json).write_text(json.dumps(kit, indent=2) + '\n', encoding='utf-8')
        print(f'kit table: {args.kit_json} ({len(surfaces)} surfaces, {len(PROPS)} props)')
    if args.preview:
        print('preview order:', ', '.join(preview(done, args.preview)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
