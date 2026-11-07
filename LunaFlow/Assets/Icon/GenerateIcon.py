#!/usr/bin/env python3
"""LunaFlow application icon generator.

Deterministically renders the LunaFlow icon from vector geometry using only
Python and Pillow:

    python GenerateIcon.py [--preview PATH]

Writes next to this script:
    LunaFlow.ico       Windows icon with 16, 20, 24, 32, 40, 48, 64, 96, 128 and
                       256 px images, all 32-bit RGBA (uncompressed 32-bit DIBs
                       with AND masks below 256 px, a PNG entry at 256 px)
    LunaFlow-256.png   256 px render
    LunaFlow-1024.png  1024 px master render

--preview PATH additionally writes a review contact sheet (every size at 1:1 and
magnified with nearest-neighbour, on light and dark backgrounds).

Every size is drawn analytically at a supersampled resolution and box-filtered
down (exact area coverage); nothing is ever upscaled. Each size range has its
own tuned variant (see VARIANTS): 16-20 px a larger crescent with a short, thick
arc; 24-32 px the full motion arc without keys; 40 px the gold head key only;
48 px two keys; 64 px and up the complete artwork with three keys and stars.

Geometry is expressed in tile units: (0, 0) is the top-left and (1, 1) the
bottom-right corner of the rounded tile, y pointing down.
"""

import argparse
import bisect
import io
import math
import os
import struct

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
MASTER_SIZE = 1024

TILE_MARGIN = 0.03     # transparent margin per side; the tile fills ~94 %
TILE_EXPONENT = 5.0    # superellipse exponent, equivalent to a ~22 % corner radius


def rgb(hex_color):
    value = hex_color.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


SKY_TOP = rgb("#23194A")
SKY_BOTTOM = rgb("#0E1222")
HALO = rgb("#7C55E6")
MOON_LIGHT = rgb("#CDB4FF")
MOON_DARK = rgb("#8A5CF0")
TEAL_LIGHT = rgb("#84E3CB")
TEAL_DARK = rgb("#5ED1B5")
GOLD = rgb("#FFD07C")
KEY_TEAL = rgb("#B0EEDD")
KEY_OUTLINE = rgb("#121530")
STAR = rgb("#ECE6FF")
WHITE = (255, 255, 255)

# Motion path: the lower half of a tilted ellipse wrapping the moon like an orbit
# seen in perspective. (cx, cy, rx, ry, rotation deg, start angle, end angle)
ORBIT = ("ellipse", (0.46, 0.465, 0.43, 0.305, -25.0, 190.0, 8.0))

# 16/20 px: a short ring segment concentric with the (larger) tiny moon, so that a
# full pixel of sky always separates the crescent from the teal stroke.
TINY_ARC = ("ellipse", (0.406, 0.406, 0.469, 0.469, 0.0, 102.0, -30.0))

LARGE_MOON = {"c": (0.430, 0.415), "r": 0.290, "cut_dist": 0.50, "cut_r": 0.80}
LARGE_ARC = {"path": ORBIT, "s0": 0.0, "s1": 1.0, "width": ((0.0, 0.004), (1.0, 0.030)),
             "fade": 0.35, "cap": False}

VARIANTS = {
    # 16 and 20 px: bold crescent and a short, thick teal arc; no glow or details.
    "tiny": {
        "moon": {"c": (0.406, 0.406), "r": 0.344, "cut_dist": 0.55, "cut_r": 0.76},
        "moon_grad": ((0.25, 0.10), (0.60, 0.78)),
        "snap": True,
        "halo": None,
        "arc": {"path": TINY_ARC, "s0": 0.0, "s1": 1.0, "width": ((0.0, 0.035), (1.0, 0.070)),
                "fade": 0.0},
        "keys": None,
        "stars": (),
        "highlight": 0.0,
    },
    # 24 and 32 px: crescent plus a thick motion arc without keys.
    "small": {
        "moon": {"c": (0.410, 0.400), "r": 0.295, "cut_dist": 0.52, "cut_r": 0.78},
        "moon_grad": ((0.28, 0.12), (0.62, 0.74)),
        "snap": True,
        "halo": {"alpha": 0.22, "scale": 1.0, "blur": 0.08},
        "arc": {"path": ORBIT, "s0": 0.15, "s1": 1.0, "width": ((0.0, 0.022), (1.0, 0.055)),
                "fade": 0.0},
        "keys": None,
        "stars": (),
        "highlight": 0.10,
    },
    # 40 px: full tapered arc ending in the selected (gold) key only.
    "medium": {
        "moon": {"c": (0.430, 0.415), "r": 0.295, "cut_dist": 0.50, "cut_r": 0.80},
        "moon_grad": ((0.28, 0.14), (0.62, 0.72)),
        "halo": {"alpha": 0.28, "scale": 1.0, "blur": 0.09},
        "arc": {"path": ORBIT, "s0": 0.0, "s1": 1.0, "width": ((0.0, 0.008), (1.0, 0.040)),
                "fade": 0.30, "cap": False},
        "keys": {"size": 0.046, "outline": 0.012, "items": ((1.0, "gold"),)},
        "stars": (),
        "highlight": 0.14,
    },
    # 48 px: full design with two slightly larger keys, no stars.
    "compact": {
        "moon": LARGE_MOON,
        "moon_grad": ((0.28, 0.14), (0.62, 0.72)),
        "halo": {"alpha": 0.30, "scale": 1.0, "blur": 0.09},
        "arc": LARGE_ARC,
        "keys": {"size": 0.042, "outline": 0.010, "items": ((0.50, "teal"), (1.0, "gold"))},
        "stars": (),
        "highlight": 0.16,
    },
    # 64 px and up: full design with three keyframe diamonds and faint stars.
    "large": {
        "moon": LARGE_MOON,
        "moon_grad": ((0.28, 0.14), (0.62, 0.72)),
        "halo": {"alpha": 0.30, "scale": 1.0, "blur": 0.09},
        "arc": LARGE_ARC,
        "keys": {"size": 0.038, "outline": 0.009,
                 "items": ((0.42, "teal"), (0.70, "teal"), (1.0, "gold"))},
        "stars": ((0.70, 0.12, 0.040), (0.84, 0.10, 0.016), (0.58, 0.21, 0.014)),
        "highlight": 0.16,
    },
}


def variant_for(size):
    if size <= 20:
        return "tiny"
    if size <= 32:
        return "small"
    if size <= 40:
        return "medium"
    if size <= 48:
        return "compact"
    return "large"


def supersample_for(size):
    if size <= 64:
        return 16
    if size <= 256:
        return 8
    return 4


# --------------------------------------------------------------------------- geometry

def lerp(a, b, t):
    return a + (b - a) * t


def lerp_color(c0, c1, t):
    return tuple(int(round(lerp(a, b, t))) for a, b in zip(c0, c1))


def smoothstep(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def bezier_point(ctrl, t):
    (x0, y0), (x1, y1), (x2, y2), (x3, y3) = ctrl
    mt = 1.0 - t
    a, b, c, d = mt * mt * mt, 3.0 * mt * mt * t, 3.0 * mt * t * t, t * t * t
    return (a * x0 + b * x1 + c * x2 + d * x3, a * y0 + b * y1 + c * y2 + d * y3)


def path_point(spec, t):
    kind, p = spec
    if kind == "bezier":
        return bezier_point(p, t)
    cx, cy, rx, ry, rotation, a0, a1 = p
    theta, phi = math.radians(lerp(a0, a1, t)), math.radians(rotation)
    x, y = rx * math.cos(theta), ry * math.sin(theta)
    return (cx + x * math.cos(phi) - y * math.sin(phi), cy + x * math.sin(phi) + y * math.cos(phi))


def profile(points, u):
    """Piecewise-linear profile through (u, value) breakpoints."""
    if u <= points[0][0]:
        return points[0][1]
    for (ua, va), (ub, vb) in zip(points, points[1:]):
        if u <= ub:
            return lerp(va, vb, (u - ua) / (ub - ua))
    return points[-1][1]


class Path:
    """Parametric curve sampled densely and parametrised by arc-length fraction."""

    def __init__(self, spec, samples=4000):
        self.pts = [path_point(spec, i / (samples - 1)) for i in range(samples)]
        acc = [0.0]
        for a, b in zip(self.pts, self.pts[1:]):
            acc.append(acc[-1] + math.dist(a, b))
        self.length = acc[-1]
        self.frac = [v / self.length for v in acc]

    def at(self, s):
        s = min(max(s, 0.0), 1.0)
        i = min(max(bisect.bisect_left(self.frac, s), 1), len(self.frac) - 1)
        a, b = self.frac[i - 1], self.frac[i]
        f = (s - a) / (b - a) if b > a else 0.0
        p, q = self.pts[i - 1], self.pts[i]
        return (lerp(p[0], q[0], f), lerp(p[1], q[1], f))

    def tangent(self, s, eps=1e-3):
        a, b = self.at(s - eps), self.at(s + eps)
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1.0
        return (dx / length, dy / length)


def moon_circles(moon):
    """Return ((cx, cy, r) outer, (cx, cy, r) cut) for a crescent opening up-right."""
    cx, cy = moon["c"]
    r = moon["r"]
    d = moon["cut_dist"] * r / math.sqrt(2.0)
    return (cx, cy, r), (cx + d, cy - d, moon["cut_r"] * r)


def circle_polygon(cx, cy, r):
    n = max(64, int(2.0 * math.pi * r / 1.5))
    return [(cx + r * math.cos(2.0 * math.pi * i / n), cy + r * math.sin(2.0 * math.pi * i / n))
            for i in range(n)]


def superellipse_polygon(cx, cy, a, exponent, count=2880):
    e = 2.0 / exponent
    pts = []
    for i in range(count):
        t = 2.0 * math.pi * i / count
        c, s = math.cos(t), math.sin(t)
        pts.append((cx + a * math.copysign(abs(c) ** e, c), cy + a * math.copysign(abs(s) ** e, s)))
    return pts


def diamond_polygon(cx, cy, half_diagonal):
    h = half_diagonal
    return [(cx, cy - h), (cx + h, cy), (cx, cy + h), (cx - h, cy)]


# --------------------------------------------------------------------------- raster

class Raster:
    """Supersampled canvas; maps tile units to supersampled pixel coordinates."""

    def __init__(self, size, ss):
        self.size = size
        self.ss = ss
        self.R = size * ss
        margin = round(size * TILE_MARGIN)
        self.tile_px = size - 2 * margin
        self.origin = margin * ss
        self.extent = self.tile_px * ss

    def p(self, u, v):
        return (self.origin + u * self.extent, self.origin + v * self.extent)

    def len(self, d):
        return d * self.extent

    def mask(self):
        return Image.new("L", (self.R, self.R), 0)


def linear_gradient(R, p0, p1, stops):
    """RGB image of a linear gradient from p0 to p1 (pixel coordinates).

    stops: sequence of (t, color) with t ascending in [0, 1]; colours are
    clamped outside the range. Evaluated per pixel through a 1-D lookup strip
    mapped with an affine transform, so it is exact at any resolution.
    """
    n, tmin, tmax = 8192, -4.0, 5.0
    data = bytearray()
    for i in range(n):
        t = tmin + (i + 0.5) * (tmax - tmin) / n
        if t <= stops[0][0]:
            color = stops[0][1]
        elif t >= stops[-1][0]:
            color = stops[-1][1]
        else:
            for (ta, ca), (tb, cb) in zip(stops, stops[1:]):
                if ta <= t <= tb:
                    color = lerp_color(ca, cb, (t - ta) / (tb - ta))
                    break
        data += bytes(color)
    strip = Image.frombytes("RGB", (n, 1), bytes(data)).resize((n, 3), Image.Resampling.NEAREST)
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    l2 = dx * dx + dy * dy
    k = n / (tmax - tmin)
    coeffs = (dx / l2 * k, dy / l2 * k, (-(p0[0] * dx + p0[1] * dy) / l2 - tmin) * k, 0.0, 0.0, 1.5)
    return strip.transform((R, R), Image.Transform.AFFINE, coeffs, resample=Image.Resampling.BILINEAR)


def scale_mask(mask, alpha):
    return mask.point([int(round(v * alpha)) for v in range(256)])


def paint(img, fill, mask):
    """Alpha-blend a solid colour or an RGB image into img through mask."""
    if isinstance(fill, tuple):
        img.paste(fill, (0, 0, img.width, img.height), mask)
    else:
        img.paste(fill, (0, 0), mask)


# --------------------------------------------------------------------------- drawing

def draw_tile_mask(g):
    m = g.mask()
    half = g.extent / 2.0
    cx, cy = g.p(0.5, 0.5)
    ImageDraw.Draw(m).polygon(superellipse_polygon(cx, cy, half, TILE_EXPONENT), fill=255)
    return m


def draw_highlight(g, img, tile, strength):
    """Very subtle 1 px inner highlight along the top edge of the tile."""
    width = max(1, round(g.size / 256)) * g.ss
    shifted = Image.new("L", tile.size, 0)
    shifted.paste(tile.crop((0, 0, g.R, g.R - width)), (0, width))
    rim = ImageChops.subtract(tile, shifted)
    fade = linear_gradient(g.R, g.p(0.5, 0.0), g.p(0.5, 0.45),
                           [(0.0, (255,) * 3), (1.0, (0,) * 3)]).getchannel("R")
    paint(img, WHITE, scale_mask(ImageChops.multiply(rim, fade), strength))


def draw_halo(g, img, moon, halo):
    (cx, cy, r), _ = moon_circles(moon)
    m = g.mask()
    x, y = g.p(cx, cy)
    ImageDraw.Draw(m).polygon(circle_polygon(x, y, g.len(r * halo["scale"])), fill=255)
    m = m.filter(ImageFilter.GaussianBlur(g.len(halo["blur"])))
    paint(img, HALO, scale_mask(m, halo["alpha"]))


def draw_stars(g, img, stars):
    for (u, v, size) in stars:
        m = g.mask()
        x, y = g.p(u, v)
        r = g.len(size)
        ImageDraw.Draw(m).polygon(superellipse_polygon(x, y, r, 0.55, count=720), fill=255)
        paint(img, STAR, scale_mask(m, 0.85 if size > 0.03 else 0.6))


def draw_moon(g, img, moon, grad):
    (cx, cy, r), (kx, ky, kr) = moon_circles(moon)
    m = g.mask()
    d = ImageDraw.Draw(m)
    x, y = g.p(cx, cy)
    d.polygon(circle_polygon(x, y, g.len(r)), fill=255)
    x, y = g.p(kx, ky)
    d.polygon(circle_polygon(x, y, g.len(kr)), fill=0)
    fill = linear_gradient(g.R, g.p(*grad[0]), g.p(*grad[1]), [(0.0, MOON_LIGHT), (1.0, MOON_DARK)])
    paint(img, fill, m)


def draw_arc(g, img, arc):
    path = Path(arc["path"])
    s0, s1 = arc["s0"], arc["s1"]
    steps = max(200, int(g.len(path.length * (s1 - s0)) / 2.0))
    left, right, alphas = [], [], []
    for i in range(steps + 1):
        u = i / steps
        s = lerp(s0, s1, u)
        cx, cy = g.p(*path.at(s))
        tx, ty = path.tangent(s)
        w = g.len(profile(arc["width"], u))
        left.append((cx - ty * w, cy + tx * w))
        right.append((cx + ty * w, cy - tx * w))
        alphas.append(smoothstep(u / arc["fade"]) if arc["fade"] > 0 else 1.0)
    m = g.mask()
    d = ImageDraw.Draw(m)
    for i in range(steps):
        a = int(round(255 * 0.5 * (alphas[i] + alphas[i + 1])))
        d.polygon([left[i], left[i + 1], right[i + 1], right[i]], fill=a)
    if arc.get("cap", True):
        hx, hy = g.p(*path.at(s1))
        d.polygon(circle_polygon(hx, hy, g.len(arc["width"][-1][1])), fill=255)
    fill = linear_gradient(g.R, g.p(*path.at(s0)), g.p(*path.at(s1)),
                           [(0.0, TEAL_DARK), (1.0, TEAL_LIGHT)])
    paint(img, fill, m)
    return path


def draw_keys(g, img, path, keys):
    for s, kind in keys["items"]:
        x, y = g.p(*path.at(s))
        size = keys["size"] * (1.25 if kind == "gold" else 1.0)
        outer = g.len(size + keys["outline"] * math.sqrt(2.0))
        m = g.mask()
        ImageDraw.Draw(m).polygon(diamond_polygon(x, y, outer), fill=255)
        paint(img, KEY_OUTLINE, m)
        m = g.mask()
        ImageDraw.Draw(m).polygon(diamond_polygon(x, y, g.len(size)), fill=255)
        paint(img, GOLD if kind == "gold" else KEY_TEAL, m)


def snap_moon(g, moon):
    """Shift the crescent so its outer left and bottom edges land on pixel boundaries."""
    cx, cy = moon["c"]
    r = moon["r"]
    margin = g.origin / g.ss
    left = margin + (cx - r) * g.tile_px
    bottom = margin + (cy + r) * g.tile_px
    return dict(moon, c=(cx + (round(left) - left) / g.tile_px,
                         cy + (round(bottom) - bottom) / g.tile_px))


def render_icon(size, variant=None, ss=None, spec=None):
    """Render the icon at size x size pixels and return an RGBA image."""
    spec = spec or VARIANTS[variant or variant_for(size)]
    g = Raster(size, ss or supersample_for(size))
    moon = snap_moon(g, spec["moon"]) if spec.get("snap") else spec["moon"]
    tile = draw_tile_mask(g)
    img = linear_gradient(g.R, g.p(0.5, 0.0), g.p(0.5, 1.0), [(0.0, SKY_TOP), (1.0, SKY_BOTTOM)])
    if spec.get("halo"):
        draw_halo(g, img, moon, spec["halo"])
    if spec.get("stars"):
        draw_stars(g, img, spec["stars"])
    if spec.get("arc"):
        path = draw_arc(g, img, spec["arc"])
        if spec.get("keys"):
            draw_keys(g, img, path, spec["keys"])
    draw_moon(g, img, moon, spec["moon_grad"])
    if spec.get("highlight"):
        draw_highlight(g, img, tile, spec["highlight"])
    out = img.convert("RGBA")
    out.putalpha(tile)
    return out.reduce(g.ss)


# --------------------------------------------------------------------------- ICO output

def dib_entry(im):
    """32-bit BGRA DIB with an AND mask, as stored inside .ico files."""
    w, h = im.size
    mask_stride = ((w + 31) // 32) * 4
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0,
                         w * h * 4 + mask_stride * h, 0, 0, 0, 0)
    bgra = im.tobytes("raw", "BGRA")
    rows = [bgra[y * w * 4:(y + 1) * w * 4] for y in range(h)]
    alpha = im.getchannel("A").tobytes()
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray(mask_stride)
        for x in range(w):
            if alpha[y * w + x] < 128:
                row[x >> 3] |= 0x80 >> (x & 7)
        mask += row
    return header + b"".join(reversed(rows)) + bytes(mask)


def png_bytes(im):
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def write_ico(path, images):
    entries = []
    for im in sorted(images, key=lambda i: i.width):
        data = png_bytes(im) if im.width >= 256 else dib_entry(im)
        entries.append((im.width, im.height, data))
    out = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for w, h, data in entries:
        out += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, _, data in entries:
        out += data
    with open(path, "wb") as f:
        f.write(out)


def verify_ico(path):
    """Re-read the icon: ICO magic, 32 bpp directory entries, RGBA frames with clear corners."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x00\x00\x01\x00":
        raise SystemExit(f"{path}: not an ICO file (magic {data[:4].hex()})")
    count = struct.unpack_from("<H", data, 4)[0]
    for i in range(count):
        planes, bpp = struct.unpack_from("<HH", data, 6 + 16 * i + 4)
        if (planes, bpp) != (1, 32):
            raise SystemExit(f"{path}: entry {i} is not 32 bpp (planes {planes}, bpp {bpp})")
    with Image.open(path) as ico:
        sizes = sorted(ico.info["sizes"])
        for size in sizes:
            frame = ico.ico.getimage(size)
            if frame.mode != "RGBA" or frame.size != size:
                raise SystemExit(f"{path}: {size} frame is {frame.mode} {frame.size}")
            w, h = size
            if any(frame.getpixel(p)[3] != 0 for p in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1))):
                raise SystemExit(f"{path}: {size} frame lacks transparent corners")
    return sizes


# --------------------------------------------------------------------------- preview

def build_preview(images, path):
    """Contact sheet: all sizes at 1:1 plus nearest-neighbour magnified views."""
    font = ImageFont.load_default(size=13)
    big = ImageFont.load_default(size=16)
    pad, gap = 24, 22
    zoomed = [(im, max(2, round(128 / im.width))) for im in images if im.width < 128]
    row1_w = sum(im.width for im in images) + gap * (len(images) - 1)
    row2_w = sum(im.width * k for im, k in zoomed) + gap * (len(zoomed) - 1)
    width = max(row1_w, row2_w) + 2 * pad
    row1_h = max(im.height for im in images)
    row2_h = max(im.height * k for im, k in zoomed)
    panel_h = pad + 26 + row1_h + 22 + 16 + row2_h + 22 + pad
    sheet = Image.new("RGB", (width, panel_h * 2), (0, 0, 0))
    for index, (bg, fg, name) in enumerate((((243, 243, 243), (40, 40, 40), "light #F3F3F3"),
                                            ((32, 32, 32), (220, 220, 220), "dark #202020"))):
        panel = Image.new("RGBA", (width, panel_h), bg + (255,))
        d = ImageDraw.Draw(panel)
        d.text((pad, pad - 6), f"LunaFlow icon on {name}: 1:1 (top), nearest-neighbour zoom (bottom)",
               font=big, fill=fg)
        x, base = pad, pad + 26 + row1_h
        for im in images:
            panel.alpha_composite(im, (x, base - im.height))
            d.text((x, base + 4), str(im.width), font=font, fill=fg)
            x += im.width + gap
        x, base = pad, pad + 26 + row1_h + 22 + 16 + row2_h
        for im, k in zoomed:
            panel.alpha_composite(im.resize((im.width * k, im.height * k), Image.Resampling.NEAREST),
                                  (x, base - im.height * k))
            d.text((x, base + 4), f"{im.width} px x{k}", font=font, fill=fg)
            x += im.width * k + gap
        sheet.paste(panel.convert("RGB"), (0, index * panel_h))
    sheet.save(path, "PNG", optimize=True)


# --------------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--preview", help="also write a review contact sheet PNG to this path")
    parser.add_argument("--out", default=HERE, help="output folder (default: next to this script)")
    args = parser.parse_args()

    images = [render_icon(size) for size in ICO_SIZES]
    master = render_icon(MASTER_SIZE)

    os.makedirs(args.out, exist_ok=True)
    ico_path = os.path.join(args.out, "LunaFlow.ico")
    write_ico(ico_path, images)
    images[-1].save(os.path.join(args.out, "LunaFlow-256.png"), "PNG", optimize=True)
    master.save(os.path.join(args.out, "LunaFlow-1024.png"), "PNG", optimize=True)

    sizes = verify_ico(ico_path)
    print("LunaFlow.ico sizes:", ", ".join(f"{w}x{h}" for w, h in sizes))
    if args.preview:
        build_preview(images, args.preview)
        print("preview:", args.preview)


if __name__ == "__main__":
    main()
