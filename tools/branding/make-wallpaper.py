#!/usr/bin/env python3
"""
make-wallpaper - render the OpenOSX Dawn wallpaper.

The Dawn mark (docs/branding/OpenOSX-Dawn.svg) is a rising sun over three
layered waves, in a squircle. A desktop background wants the same scene without
the squircle and composed for 16:9, so this redraws it rather than scaling the
logo: the waves become continuous functions across the full width instead of
the mark's fixed Bezier, which lets the same design work at any aspect ratio.

Pure Python, no Pillow, no cairosvg, no Inkscape - it writes the PNG itself.
The palette is lifted verbatim from the SVG so the wallpaper and the logo stay
the same artwork.

  make-wallpaper.py [-o out.png] [--width 1920] [--height 1080]
"""
import argparse
import math
import struct
import zlib

# Straight from OpenOSX-Dawn.svg.
SKY_TOP = (0x1B, 0x16, 0x37)
SKY_BOTTOM = (0x07, 0x06, 0x0F)
GLOW = (0xFF, 0xB3, 0x5C)
SUN_TOP = (0xFF, 0xE9, 0xA8)
SUN_BOTTOM = (0xFF, 0xA9, 0x4D)
HIGHLIGHT = (0xFF, 0xF6, 0xE2)

BANDS = [
    # (top colour, bottom colour, opacity, base height, amplitude, phase)
    ((0xA9, 0x6C, 0xFF), (0x5F, 0x35, 0xDF), 0.88, 0.520, 0.045, 0.00),
    ((0xFF, 0x7E, 0x6B), (0xE8, 0x44, 0x6E), 0.94, 0.655, 0.050, 0.55),
    ((0xFF, 0xD3, 0x6B), (0xFF, 0x9A, 0x3C), 1.00, 0.780, 0.042, 1.15),
]

SUN_CX, SUN_CY, SUN_R = 0.500, 0.455, 0.150     # fractions of height (cy, r)


def lerp(a, b, t):
    return a + (b - a) * t


def mix(c0, c1, t):
    return (lerp(c0[0], c1[0], t), lerp(c0[1], c1[1], t), lerp(c0[2], c1[2], t))


def over(dst, src, alpha):
    """Source-over composite of one pixel."""
    return (dst[0] + (src[0] - dst[0]) * alpha,
            dst[1] + (src[1] - dst[1]) * alpha,
            dst[2] + (src[2] - dst[2]) * alpha)


def wave_y(x_frac, base, amp):
    """Band top as a function of horizontal position.

    Two harmonics rather than one: a single sine reads as a wallpaper made by
    a sine, while the second (smaller, faster, offset) gives the uneven crest
    the hand-drawn mark has.
    """
    t = x_frac * math.tau
    return base + amp * (math.sin(t * 1.05) * 0.78 + math.sin(t * 2.15 + 1.9) * 0.22)


def stars(width, height, horizon, count=340):
    """Deterministic star field above the horizon.

    A fixed LCG rather than `random`, so the wallpaper is byte-identical on
    every machine and a rebuild does not show up as a spurious diff.
    """
    out = []
    seed = 0x0D15EA5E
    for _ in range(count):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        x = (seed / 0x7FFFFFFF) * width
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        yf = (seed / 0x7FFFFFFF) ** 1.7          # crowd them toward the top
        y = yf * horizon * 0.95
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        bright = 0.25 + (seed / 0x7FFFFFFF) * 0.75
        # Fade out near the horizon so they do not collide with the glow.
        bright *= max(0.0, 1.0 - (y / (horizon * 0.95)) ** 2 * 0.75)
        out.append((x, y, bright))
    return out


def render(width, height):
    cx, cy, r = SUN_CX * width, SUN_CY * height, SUN_R * height
    glow_r = r * 3.4

    # Per-column band tops, in pixels. Computed once instead of per pixel.
    tops = []
    for _c, _c2, _o, base, amp, phase in BANDS:
        tops.append([wave_y((x / width) + phase, base, amp) * height
                     for x in range(width)])

    rows = []
    for y in range(height):
        sky = mix(SKY_TOP, SKY_BOTTOM, y / (height - 1))
        row = bytearray()
        for x in range(width):
            px = sky

            # Warm glow behind the sun.
            d = math.hypot(x - cx, y - cy)
            if d < glow_r:
                g = (1.0 - d / glow_r) ** 2 * 0.42
                px = over(px, GLOW, g)

            # Sun disc, analytically antialiased at the rim.
            cov = 0.5 - (d - r)
            if cov > 0.0:
                if cov > 1.0:
                    cov = 1.0
                top = cy - r
                px = over(px, mix(SUN_TOP, SUN_BOTTOM,
                                  min(1.0, max(0.0, (y - top) / (2 * r)))), cov)

            # Wave bands, back to front. Coverage from the vertical distance to
            # the band top gives clean edges on near-horizontal curves.
            for i, (c0, c1, opacity, _b, _a, _p) in enumerate(BANDS):
                ty = tops[i][x]
                c = 0.5 + (y - ty)
                if c <= 0.0:
                    continue
                if c > 1.0:
                    c = 1.0
                shade = min(1.0, max(0.0, (y - ty) / (height - ty + 1e-6)))
                px = over(px, mix(c0, c1, shade), c * opacity)

            row.append(int(px[0] + 0.5))
            row.append(int(px[1] + 0.5))
            row.append(int(px[2] + 0.5))
        rows.append(row)

    # Highlight along the crest of the front band, and the stars behind it all.
    front = tops[2]
    half = 2.2                           # highlight half-width, pixels
    for x in range(width):
        ty = front[x]
        for y in range(int(ty - half) - 1, int(ty + half) + 2):
            if not (0 <= y < height):
                continue
            # Distance from the pixel centre to the true (fractional) crest.
            # Using the integer offset instead quantises the falloff and makes
            # the highlight stair-step along the curve.
            a = 1.0 - abs((y + 0.5) - ty) / half
            if a <= 0.0:
                continue
            a *= 0.55
            o = x * 3
            row = rows[y]
            row[o] = int(lerp(row[o], HIGHLIGHT[0], a))
            row[o + 1] = int(lerp(row[o + 1], HIGHLIGHT[1], a))
            row[o + 2] = int(lerp(row[o + 2], HIGHLIGHT[2], a))

    horizon = min(tops[0])
    for sx, sy, bright in stars(width, height, horizon):
        ix, iy = int(sx), int(sy)
        if not (0 <= ix < width and 0 <= iy < height):
            continue
        o = ix * 3
        row = rows[iy]
        for k in range(3):
            row[o + k] = min(255, int(row[o + k] + (255 - row[o + k]) * bright * 0.9))
    return rows


def write_png(path, rows, width, height):
    raw = bytearray()
    for row in rows:
        raw.append(0)                    # filter type 0 (None)
        raw.extend(row)

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-o', '--out', default='OpenOSX-Dawn-Wallpaper.png')
    ap.add_argument('--width', type=int, default=1920)
    ap.add_argument('--height', type=int, default=1080)
    args = ap.parse_args()

    rows = render(args.width, args.height)
    write_png(args.out, rows, args.width, args.height)
    print('wrote %s (%dx%d)' % (args.out, args.width, args.height))


if __name__ == '__main__':
    main()
