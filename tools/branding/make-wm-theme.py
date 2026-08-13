#!/usr/bin/env python3
"""
make-wm-theme - generate OpenOSX's window decoration theme for xfwm4.

Every pixel here is drawn in Python rather than shipped as binary artwork. That
keeps the theme reproducible, reviewable in a diff, and free of any question
about where the images came from - which matters when the whole point is to
look like a commercial desktop without borrowing its assets.

xfwm4's theme format is a directory of PNGs plus a `themerc`. The pieces it
needs are fixed by name:

  title-{1..5}-{active,inactive}.png   titlebar fill, left to right segments
  top-{left,right}-{active,inactive}.png     rounded upper corners
  {left,right,bottom,bottom-left,bottom-right}-{active,inactive}.png   borders
  {close,hide,maximize,shade,stick,menu}-{active,inactive,prelight,pressed}.png

The buttons are the recognisable part: three circles at the *left* of the bar,
red/amber/green, flat until the pointer is over the group and then carrying a
glyph. The glyph-on-hover behaviour is what reads as "not Linux" more than the
colours do, and xfwm4 gives it to us free - `prelight` is drawn for all buttons
when any one of them is hovered.

  make-wm-theme.py -o themes/OpenOSX-Aqua
"""
import argparse
import math
import os
import struct
import zlib

# ---------------------------------------------------------------- palette --
# Sampled to feel right against the Dawn wallpaper rather than copied from any
# particular release: a warm off-white bar, a hairline separator, near-black
# text, and the three signal colours.
TITLE_ACTIVE_TOP    = (0xF6, 0xF5, 0xF4)
TITLE_ACTIVE_BOT    = (0xE4, 0xE2, 0xE0)
TITLE_INACTIVE_TOP  = (0xF2, 0xF2, 0xF1)
TITLE_INACTIVE_BOT  = (0xEC, 0xEB, 0xEA)
HAIRLINE            = (0xC8, 0xC5, 0xC2)
BORDER              = (0xB9, 0xB6, 0xB3)

RED     = (0xFF, 0x5F, 0x57)
AMBER   = (0xFE, 0xBC, 0x2E)
GREEN   = (0x28, 0xC8, 0x40)
DIM     = (0xD6, 0xD3, 0xD0)      # buttons on an unfocused window
GLYPH   = (0x4D, 0x2A, 0x08)      # dark warm ink for the hover glyphs

TITLE_H = 28                      # titlebar height
# The cell is deliberately narrower than the bar is tall. Square cells put ~30px
# between dot centres, which reads as a Linux titlebar however good the colours
# are; the look being copied sits them about 20px apart.
BTN_W   = 20                      # button cell width
BTN_H   = TITLE_H                 # ... full bar height, so it centres itself
DOT_R   = 6.0                     # traffic light radius
BORDER_W = 1
CORNER_R = 10.0                   # top corner rounding


def png(path, pix, w, h):
    """Write RGBA rows. pix is a list of rows of (r,g,b,a) tuples."""
    raw = bytearray()
    for row in pix:
        raw.append(0)                                  # filter: None
        for r, g, b, a in row:
            raw += bytes((r, g, b, a))

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    out = b'\x89PNG\r\n\x1a\n'
    out += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    out += chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    out += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(out)


def blank(w, h, rgba=(0, 0, 0, 0)):
    return [[rgba for _ in range(w)] for _ in range(h)]


def vgrad(w, h, top, bot):
    rows = []
    for y in range(h):
        t = y / float(max(1, h - 1))
        c = tuple(int(round(top[i] + (bot[i] - top[i]) * t)) for i in range(3))
        rows.append([(c[0], c[1], c[2], 255) for _ in range(w)])
    return rows


def blend(dst, src, a):
    """src over dst with alpha a in 0..1."""
    return tuple(int(round(dst[i] * (1 - a) + src[i] * a)) for i in range(3))


def put(img, x, y, rgb, a):
    """Composite a colour onto an RGBA pixel, keeping coverage."""
    if y < 0 or y >= len(img) or x < 0 or x >= len(img[0]):
        return
    dr, dg, db, da = img[y][x]
    na = da / 255.0 + a * (1 - da / 255.0)
    if na <= 0:
        return
    # Straight-alpha compositing; da==0 means take the source colour outright.
    if da == 0:
        nr, ng, nb = rgb
    else:
        nr, ng, nb = blend((dr, dg, db), rgb, a / na)
    img[y][x] = (nr, ng, nb, int(round(min(1.0, na) * 255)))


def disc(img, cx, cy, r, rgb, ss=4):
    """Anti-aliased filled circle, by supersampling coverage per pixel."""
    x0, x1 = int(math.floor(cx - r - 1)), int(math.ceil(cx + r + 1))
    y0, y1 = int(math.floor(cy - r - 1)), int(math.ceil(cy + r + 1))
    step = 1.0 / ss
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            hits = 0
            for sy in range(ss):
                for sx in range(ss):
                    px = x + (sx + 0.5) * step
                    py = y + (sy + 0.5) * step
                    if (px - cx) ** 2 + (py - cy) ** 2 <= r * r:
                        hits += 1
            if hits:
                put(img, x, y, rgb, hits / float(ss * ss))


def stroke(img, pts, rgb, width=1.4, ss=4):
    """Anti-aliased polyline, used for the hover glyphs."""
    for i in range(len(pts) - 1):
        ax, ay = pts[i]
        bx, by = pts[i + 1]
        n = max(2, int(math.hypot(bx - ax, by - ay) * ss * 2))
        for k in range(n + 1):
            t = k / float(n)
            disc(img, ax + (bx - ax) * t, ay + (by - ay) * t, width / 2.0, rgb, ss=2)


def rounded_corner(w, h, r, fill_top, fill_bot, which):
    """One upper corner: gradient fill inside a quarter-round, else transparent.

    `which` is 'left' or 'right'. The hairline that runs along the bottom of the
    titlebar is drawn by the caller for the straight segments; here it has to
    follow the curve, so it is baked in.
    """
    img = blank(w, h)
    cx = (r - 0.5) if which == 'left' else (w - r + 0.5 - 1)
    cy = r - 0.5
    for y in range(h):
        t = y / float(max(1, TITLE_H - 1))
        col = tuple(int(round(fill_top[i] + (fill_bot[i] - fill_top[i]) * min(1.0, t)))
                    for i in range(3))
        for x in range(w):
            inside_x = (x >= cx) if which == 'left' else (x <= cx)
            if y >= cy or inside_x:
                put(img, x, y, col, 1.0)                # straight part
            else:
                d = math.hypot(x - cx, y - cy)
                cov = max(0.0, min(1.0, r - d + 0.5))   # 1px feathered edge
                if cov > 0:
                    put(img, x, y, col, cov)
                # the outer edge line, following the curve
                edge = max(0.0, 1.0 - abs(d - r) )
                if edge > 0:
                    put(img, x, y, BORDER, edge * 0.55)
    return img


def tri(img, pts, rgb, ss=4):
    """Anti-aliased filled triangle - the maximise glyph, which is too small at
    this size to read as a pair of arrows."""
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    step = 1.0 / ss

    def side(px, py, a, b):
        return (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0])

    for y in range(int(math.floor(min(ys))) - 1, int(math.ceil(max(ys))) + 2):
        for x in range(int(math.floor(min(xs))) - 1, int(math.ceil(max(xs))) + 2):
            hits = 0
            for sy in range(ss):
                for sx in range(ss):
                    px, py = x + (sx + 0.5) * step, y + (sy + 0.5) * step
                    d1 = side(px, py, pts[0], pts[1])
                    d2 = side(px, py, pts[1], pts[2])
                    d3 = side(px, py, pts[2], pts[0])
                    if (d1 >= 0 and d2 >= 0 and d3 >= 0) or \
                       (d1 <= 0 and d2 <= 0 and d3 <= 0):
                        hits += 1
            if hits:
                put(img, x, y, rgb, hits / float(ss * ss))


def button(state, kind):
    """One button cell, BTN_W x BTN_H.

    state: 'active' | 'inactive' | 'prelight' | 'pressed'
    kind:  'close' | 'hide' | 'maximize'
    """
    img = blank(BTN_W, BTN_H)
    colours = {'close': RED, 'hide': AMBER, 'maximize': GREEN}
    cx, cy = BTN_W / 2.0, BTN_H / 2.0

    base = DIM if state == 'inactive' else colours[kind]
    if state == 'pressed':
        base = tuple(int(c * 0.82) for c in base)

    disc(img, cx, cy, DOT_R, base)
    # A hairline ring keeps the dot from dissolving into a light titlebar. It
    # has to stay faint: at full strength the dots read as cartoon stickers.
    ring = tuple(int(c * 0.88) for c in base)
    for a in range(0, 360, 3):
        rad = math.radians(a)
        put(img, int(round(cx + math.cos(rad) * DOT_R)),
                 int(round(cy + math.sin(rad) * DOT_R)), ring, 0.30)

    if state in ('prelight', 'pressed'):
        # ~7px of usable room inside a 12px dot, so the glyphs stay minimal.
        g, wdt = 2.1, 1.0
        if kind == 'close':
            stroke(img, [(cx - g, cy - g), (cx + g, cy + g)], GLYPH, width=wdt)
            stroke(img, [(cx + g, cy - g), (cx - g, cy + g)], GLYPH, width=wdt)
        elif kind == 'hide':
            stroke(img, [(cx - g - 0.6, cy), (cx + g + 0.6, cy)], GLYPH, width=wdt)
        else:
            # Two opposed corner wedges: legible at 7px where arrows are not.
            # They must not meet - with legs long enough to touch across the
            # centre the pair reads as one diagonal slash rather than as two
            # arrows pointing apart.
            t, leg = 2.9, 3.1
            tri(img, [(cx - t, cy - t), (cx - t + leg, cy - t),
                      (cx - t, cy - t + leg)], GLYPH)
            tri(img, [(cx + t, cy + t), (cx + t - leg, cy + t),
                      (cx + t, cy + t - leg)], GLYPH)
    return img


def opaque(w, h, rgb):
    return [[(rgb[0], rgb[1], rgb[2], 255) for _ in range(w)] for _ in range(h)]


def titlebar_segment(w, active):
    top = TITLE_ACTIVE_TOP if active else TITLE_INACTIVE_TOP
    bot = TITLE_ACTIVE_BOT if active else TITLE_INACTIVE_BOT
    img = [[(c[0], c[1], c[2], 255) for c in row]
           for row in [[tuple(px[:3]) for px in r] for r in vgrad(w, TITLE_H, top, bot)]]
    for x in range(w):                       # hairline under the bar
        img[TITLE_H - 1][x] = (HAIRLINE[0], HAIRLINE[1], HAIRLINE[2], 255)
    return img


THEMERC = """\
# OpenOSX Aqua - xfwm4 theme
#
# Buttons live on the left, close|minimise|maximise, with the title centred in
# what is left.
#
# The letter codes are NOT the mnemonic ones. Read out of xfwm4-settings' own
# UI, where each draggable button widget is id="button-layout-<letter>" next to
# its label:
#
#   C close    H minimise   M maximise
#   O MENU     T STICK      S shade      | the title, which cannot be removed
#
# So "O" is the window menu, not "open/close", and "T" is stick, not title. An
# earlier version of this file said OHM| and would have shipped a titlebar whose
# red dot opened the window menu and which had no close button at all - an
# unrecognised or misread letter is skipped silently rather than erroring.
button_layout=CHM|
button_offset=8
button_spacing=2
title_horizontal_offset=0
title_vertical_offset_active=1
title_vertical_offset_inactive=1
full_width_title=true
title_alignment=center
title_shadow_active=false
title_shadow_inactive=false
active_text_color=#1d1d1f
inactive_text_color=#8a8a8e
shadow_delta_height=0
shadow_delta_width=0
shadow_delta_x=0
shadow_delta_y=0
shadow_opacity=40
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-o', '--out', default='OpenOSX-Aqua')
    args = ap.parse_args()

    d = args.out
    os.makedirs(d, exist_ok=True)

    for active in (True, False):
        suf = 'active' if active else 'inactive'
        top = TITLE_ACTIVE_TOP if active else TITLE_INACTIVE_TOP
        bot = TITLE_ACTIVE_BOT if active else TITLE_INACTIVE_BOT

        # title-1..5: xfwm4 tiles 2 and 4, and centres the text over 3.
        for n, w in ((1, 6), (2, 6), (3, 6), (4, 6), (5, 6)):
            png(os.path.join(d, 'title-%d-%s.png' % (n, suf)),
                titlebar_segment(w, active), w, TITLE_H)

        for which in ('left', 'right'):
            img = rounded_corner(int(CORNER_R) + 2, TITLE_H, CORNER_R, top, bot, which)
            png(os.path.join(d, 'top-%s-%s.png' % (which, suf)),
                img, int(CORNER_R) + 2, TITLE_H)

        # Thin flat borders. A 1px frame is most of what makes a window read as
        # modern rather than as a 2005 desktop.
        png(os.path.join(d, 'left-%s.png' % suf), opaque(BORDER_W, 6, BORDER), BORDER_W, 6)
        png(os.path.join(d, 'right-%s.png' % suf), opaque(BORDER_W, 6, BORDER), BORDER_W, 6)
        png(os.path.join(d, 'bottom-%s.png' % suf), opaque(6, BORDER_W, BORDER), 6, BORDER_W)
        png(os.path.join(d, 'bottom-left-%s.png' % suf), opaque(8, 8, BORDER), 8, 8)
        png(os.path.join(d, 'bottom-right-%s.png' % suf), opaque(8, 8, BORDER), 8, 8)

    for kind in ('close', 'hide', 'maximize'):
        for state in ('active', 'inactive', 'prelight', 'pressed'):
            png(os.path.join(d, '%s-%s.png' % (kind, state)),
                button(state, kind), BTN_W, BTN_H)
        # xfwm4 also looks for the toggled maximise variants.
        if kind == 'maximize':
            for state in ('active', 'inactive', 'prelight', 'pressed'):
                png(os.path.join(d, 'maximize-toggled-%s.png' % state),
                    button(state, 'maximize'), BTN_W, BTN_H)

    # Buttons we deliberately do not show still need files, or xfwm4 logs for
    # each one on every window; an empty cell is the honest answer.
    for kind in ('shade', 'stick', 'menu', 'shade-toggled', 'stick-toggled'):
        for state in ('active', 'inactive', 'prelight', 'pressed'):
            png(os.path.join(d, '%s-%s.png' % (kind, state)),
                blank(BTN_W, BTN_H), BTN_W, BTN_H)

    with open(os.path.join(d, 'themerc'), 'w', newline='\n') as f:
        f.write(THEMERC)

    n = len([f for f in os.listdir(d) if f.endswith('.png')])
    print('wrote %s (%d PNGs + themerc)' % (d, n))


if __name__ == '__main__':
    main()
