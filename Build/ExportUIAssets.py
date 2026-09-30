#!/usr/bin/env python3
"""Author and export the HUD's vector art: 9-slice frame textures and command-card glyphs (see Art/UI/STYLE.md).

    python3 Build/ExportUIAssets.py          # needs rsvg-convert (librsvg) on PATH; standard library only

Writes, all under Art/UI/:
    frames/<name>.svg + .png      9-slice frames (authored at TWO texels per reference pixel, so they stay sharp up
                                  to the 2x HUD scale; slice margins are listed in Art/UI/frames/slices.txt)
    icons/commands/<name>.svg + .png   bold white command glyphs, 128 px; the HUD tints them at draw time

Frames are layered so the runtime can tint by state: `*_base` carries the bevelled metal and translucent fill,
`*_trim` is a thin WHITE glow line drawn on top with the player / state colour as its tint. Bevel lighting is baked
per edge from the edge normal, so a 9-slice can stretch the edge strips without visible gradients.
"""
import math
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI = os.path.join(ROOT, "Art", "UI")
FRAMES = os.path.join(UI, "frames")
GLYPHS = os.path.join(UI, "icons", "commands")

# ---------------------------------------------------------------------------------------------------------------
# Geometry helpers (convex polygons, y down)


def rect_poly(w, h, tl=0.0, tr=0.0, br=0.0, bl=0.0, m=0.0):
    """Rectangle with angled (chamfered) corners, inset by margin m. Returns points clockwise from top-left."""
    x0, y0, x1, y1 = m, m, w - m, h - m
    pts = []
    pts += [(x0, y0 + tl), (x0 + tl, y0)] if tl else [(x0, y0)]
    pts += [(x1 - tr, y0), (x1, y0 + tr)] if tr else [(x1, y0)]
    pts += [(x1, y1 - br), (x1 - br, y1)] if br else [(x1, y1)]
    pts += [(x0 + bl, y1), (x0, y1 - bl)] if bl else [(x0, y1)]
    return dedupe(pts)


def dedupe(pts):
    out = []
    for p in pts:
        if not out or (abs(out[-1][0] - p[0]) > 1e-6 or abs(out[-1][1] - p[1]) > 1e-6):
            out.append(p)
    if abs(out[0][0] - out[-1][0]) < 1e-6 and abs(out[0][1] - out[-1][1]) < 1e-6:
        out.pop()
    return out


def rounded_poly(w, h, r, cut=0.0, m=0.0, segs=6, corners=(True, True, True, True)):
    """Rectangle with round corners (Machine skin) approximated by segs segments; corners = TL, TR, BR, BL."""
    x0, y0, x1, y1 = m, m, w - m, h - m
    centers = [(x0 + r, y0 + r, 180), (x1 - r, y0 + r, 270), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90)]
    pts = []
    for (cx, cy, a0), on in zip(centers, corners):
        if not on:
            pts.append((cx + (r if a0 in (270, 0) else -r), cy + (r if a0 in (0, 90) else -r)))
            continue
        for i in range(segs + 1):
            a = math.radians(a0 + 90.0 * i / segs)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return dedupe(pts)


def signed_area(pts):
    return sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1]
               for i in range(len(pts))) / 2.0


def inset(pts, d):
    """Offset a convex clockwise (y-down) polygon inward by d using mitred offset lines."""
    n = len(pts)
    normals = []
    for i in range(n):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        dx, dy = x1 - x0, y1 - y0
        length = math.hypot(dx, dy) or 1.0
        normals.append((-dy / length, dx / length))     # inward for clockwise in y-down (checked in ring())
    out = []
    for i in range(n):
        nx0, ny0 = normals[i - 1]
        nx1, ny1 = normals[i]
        k = 1.0 + nx0 * nx1 + ny0 * ny1
        px, py = pts[i]
        out.append((px + (nx0 + nx1) * d / k, py + (ny0 + ny1) * d / k))
    return out


def fmt(pts):
    return " ".join("%.2f,%.2f" % p for p in pts)


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def hexcol(c):
    return "#%02x%02x%02x" % tuple(max(0, min(255, int(round(v)))) for v in c)


def ramp(t, dark, mid, light):
    """t in [-1, 1] (edge normal . light direction) -> colour."""
    t = max(-1.0, min(1.0, t))
    return lerp(mid, light, t) if t >= 0 else lerp(mid, dark, -t)


LIGHT = (-0.55, -0.83)     # direction TOWARD the light (upper left)

# ---------------------------------------------------------------------------------------------------------------
# Skins. Each is a dict of tokens (see STYLE.md "Colour tokens").

OFFLINE = dict(
    dark=(9, 13, 18), mid=(52, 66, 82), light=(132, 154, 176),
    fill=("#0b1118", 0.88), fill_top=("#3a4c60", 0.16), fill_bottom=("#000000", 0.30),
    edge="#04070a", groove="#000000", rivet=("#93a6b9", "#1a222b"), hazard=("#ffc21a", "#0a0d10"),
)
MACHINE = dict(
    dark=(72, 92, 110), mid=(178, 196, 210), light=(250, 253, 255),
    fill=("#06121a", 0.86), fill_top=("#7fe9ff", 0.14), fill_bottom=("#000000", 0.30),
    edge="#0a1620", groove="#02090e", rivet=("#ffffff", "#5a7284"), hazard=("#ff3b30", "#ff3b30"),
)


def svg_open(w, h):
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n' % (w, h, w, h))


DEFS_HAZARD = """<defs>
<pattern id="hz" width="10" height="10" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">
<rect width="10" height="10" fill="%s"/><rect width="5" height="10" fill="%s"/></pattern>
<filter id="glow" x="-20%%" y="-20%%" width="140%%" height="140%%"><feGaussianBlur stdDeviation="2.2"/></filter>
<filter id="glow4" x="-20%%" y="-20%%" width="140%%" height="140%%"><feGaussianBlur stdDeviation="4"/></filter>
</defs>
"""


def ring(outer, bw, skin, flip=False):
    """Bevel ring between outer polygon and outer inset by bw; each edge quad lit by its outward normal."""
    inner = inset(outer, bw)
    n = len(outer)
    parts = []
    for i in range(n):
        (x0, y0), (x1, y1) = outer[i], outer[(i + 1) % n]
        dx, dy = x1 - x0, y1 - y0
        length = math.hypot(dx, dy) or 1.0
        out_n = (dy / length, -dx / length)              # outward normal for clockwise y-down polygons
        lit = out_n[0] * LIGHT[0] + out_n[1] * LIGHT[1]
        if flip:
            lit = -lit
        col = hexcol(ramp(lit * 1.25, skin["dark"], skin["mid"], skin["light"]))
        quad = [outer[i], outer[(i + 1) % n], inner[(i + 1) % n], inner[i]]
        parts.append('<polygon points="%s" fill="%s" stroke="%s" stroke-width="0.6" stroke-linejoin="round"/>'
                     % (fmt(quad), col, col))
    return inner, "\n".join(parts)


def frame_svg(w, h, skin, outer, bw, fill=True, sheen=18, trim=None, rivets=(), hazard=None, gap_ring=None, lens=None,
              flip=False, edge_w=1.4):
    """Base layer of a frame. `outer` polygon; bw bevel width; optional second ring (Machine floating gap)."""
    s = [svg_open(w, h), DEFS_HAZARD % (skin["hazard"][0], skin["hazard"][1])]
    s.append('<polygon points="%s" fill="%s"/>' % (fmt(outer), skin["edge"]))              # dark silhouette edge
    body = inset(outer, edge_w)
    inner, ring_svg = ring(body, bw, skin, flip=flip)
    s.append(ring_svg)
    if gap_ring:                                                                            # floating inner ring
        gap_inner = inset(inner, gap_ring[0])
        s.append('<polygon points="%s" fill="%s"/>' % (fmt(gap_inner), skin["edge"]))
        inner2, ring2 = ring(inset(gap_inner, 0.8), gap_ring[1], skin, flip=not flip)
        s.append(ring2)
        inner = inner2
    if fill:
        s.append('<polygon points="%s" fill="%s" fill-opacity="%.2f"/>' % (fmt(inner), skin["fill"][0], skin["fill"][1]))
        xs = [p[0] for p in inner]
        ys = [p[1] for p in inner]
        clip = "clip%d" % (abs(hash(fmt(inner))) % 10**6)
        s.append('<clipPath id="%s"><polygon points="%s"/></clipPath>' % (clip, fmt(inner)))
        if sheen:
            s.append('<linearGradient id="%s_t" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s" stop-opacity="%.2f"/>'
                     '<stop offset="1" stop-color="%s" stop-opacity="0"/></linearGradient>'
                     % (clip, skin["fill_top"][0], skin["fill_top"][1], skin["fill_top"][0]))
            s.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%d" fill="url(#%s_t)" clip-path="url(#%s)"/>'
                     % (min(xs), min(ys), max(xs) - min(xs), sheen, clip, clip))
            s.append('<linearGradient id="%s_b" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="%s" stop-opacity="%.2f"/>'
                     '<stop offset="1" stop-color="%s" stop-opacity="0"/></linearGradient>'
                     % (clip, skin["fill_bottom"][0], skin["fill_bottom"][1], skin["fill_bottom"][0]))
            s.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%d" fill="url(#%s_b)" clip-path="url(#%s)"/>'
                     % (min(xs), max(ys) - sheen, max(xs) - min(xs), sheen, clip, clip))
    s.append('<polygon points="%s" fill="none" stroke="%s" stroke-opacity="0.75" stroke-width="1.2"/>'
             % (fmt(inner), skin["groove"]))                                               # inner groove
    for (cx, cy, r) in rivets:
        s.append('<circle cx="%.2f" cy="%.2f" r="%.2f" fill="%s"/><circle cx="%.2f" cy="%.2f" r="%.2f" fill="%s"/>'
                 % (cx, cy, r, skin["rivet"][1], cx - r * 0.2, cy - r * 0.2, r * 0.7, skin["rivet"][0]))
    if hazard:                                                                              # (x, y, w, h)
        x, y, hw, hh = hazard
        s.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" fill="url(#hz)" stroke="%s" stroke-width="1"/>'
                 % (x, y, hw, hh, skin["edge"]))
    if lens:
        cx, cy, r = lens
        s.append('<circle cx="%.2f" cy="%.2f" r="%.2f" fill="#2a0b0b" stroke="#c8d6e0" stroke-width="1.2"/>'
                 '<circle cx="%.2f" cy="%.2f" r="%.2f" fill="#ff3b30"/>' % (cx, cy, r, cx, cy, r * 0.55))
    s.append("</svg>\n")
    return "".join(s)


def trim_svg(w, h, poly, width=2.0, color="#ffffff", glow=0.55):
    """White glowing line following poly (already positioned). Glow is baked as a blurred copy underneath."""
    s = [svg_open(w, h), DEFS_HAZARD % ("#000", "#000")]
    n = len(poly)
    d = "M" + " L".join("%.2f,%.2f" % p for p in poly) + " Z"
    s.append('<path d="%s" fill="none" stroke="%s" stroke-opacity="%.2f" stroke-width="%.1f" filter="url(#glow4)"/>' % (d, color, glow * 0.8, width * 3.0))
    s.append('<path d="%s" fill="none" stroke="%s" stroke-opacity="%.2f" stroke-width="%.1f" filter="url(#glow)"/>' % (d, color, glow, width * 1.5))
    s.append('<path d="%s" fill="none" stroke="%s" stroke-width="%.1f" stroke-linejoin="miter"/>' % (d, color, width))
    s.append("</svg>\n")
    return "".join(s)


# ---------------------------------------------------------------------------------------------------------------
# Frame catalogue: name -> (svg, (slice top, right, bottom, left) in texels)

SLICES = {}
FILES = {}


def add(name, svg, slice_tuple):
    FILES[name] = svg
    SLICES[name] = slice_tuple


def offline_panel():
    w = h = 128
    outer = rect_poly(w, h, tl=18, tr=6, br=18, bl=6)
    add("panel_base", frame_svg(w, h, OFFLINE, outer, bw=8, sheen=22,
                                rivets=[(14, 24, 2.6), (w - 14, h - 24, 2.6), (w - 12, 12, 2.2), (12, h - 12, 2.2)],
                                hazard=(4, h - 34, 6, 20)), (32, 32, 32, 32))
    trim_poly = rect_poly(w, h, tl=24, tr=10, br=24, bl=10, m=14)
    add("panel_trim", trim_svg(w, h, trim_poly), (32, 32, 32, 32))


def offline_button():
    w = h = 64
    outer = rect_poly(w, h, tl=10, tr=3, br=10, bl=3)
    states = {
        "normal": dict(skin=dict(OFFLINE), fill=("#141d27", 0.97)),
        "hover": dict(skin=dict(OFFLINE, mid=(76, 96, 118), light=(168, 192, 214)), fill=("#1c2a39", 0.98)),
        "pressed": dict(skin=dict(OFFLINE, mid=(30, 40, 52), dark=(120, 140, 160), light=(9, 13, 18)),
                        fill=("#090e14", 0.98), flip=True),
        "disabled": dict(skin=dict(OFFLINE, dark=(12, 15, 19), mid=(30, 36, 43), light=(52, 60, 70)),
                         fill=("#0c1015", 0.90)),
    }
    for name, cfg in states.items():
        skin = cfg["skin"]
        skin["fill"] = cfg["fill"]
        skin["fill_top"] = ("#5b7590", 0.20 if name != "disabled" else 0.04)
        add("btn_" + name, frame_svg(w, h, skin, outer, bw=4, sheen=12, flip=cfg.get("flip", False), edge_w=1.2),
            (14, 14, 14, 14))
    add("btn_trim", trim_svg(w, h, rect_poly(w, h, tl=10, tr=3, br=10, bl=3, m=6), width=1.6), (14, 14, 14, 14))


def offline_misc():
    # portrait frame: thick bevel, transparent interior
    w = h = 128
    outer = rect_poly(w, h, tl=16, tr=4, br=16, bl=4)
    add("portrait_base", frame_svg(w, h, OFFLINE, outer, bw=9, fill=False, sheen=0,
                                   rivets=[(w - 14, 13, 2.2), (14, h - 13, 2.2)], hazard=(w - 46, h - 8, 34, 6)),
        (28, 28, 28, 28))
    add("portrait_trim", trim_svg(w, h, rect_poly(w, h, tl=14, tr=3, br=14, bl=3, m=13), width=1.8), (28, 28, 28, 28))
    # minimap: heavier frame, transparent interior, cut TL/BR
    outer = rect_poly(w, h, tl=24, tr=8, br=24, bl=8)
    add("minimap_base", frame_svg(w, h, OFFLINE, outer, bw=10, fill=False, sheen=0,
                                  rivets=[(14, 30, 2.6), (w - 14, h - 30, 2.6), (w - 14, 14, 2.4), (14, h - 14, 2.4)],
                                  hazard=(6, h - 46, 8, 26)), (36, 36, 36, 36))
    add("minimap_trim", trim_svg(w, h, rect_poly(w, h, tl=26, tr=9, br=26, bl=9, m=15), width=1.8), (36, 36, 36, 36))
    # top strip: no top border, angled lower corners
    w2, h2 = 128, 64
    outer = rect_poly(w2, h2, tl=0, tr=0, br=26, bl=26)
    svg = frame_svg(w2, h2, OFFLINE, outer, bw=6, sheen=14, rivets=[(w2 - 26, h2 - 20, 2.2)])
    add("strip_base", svg, (8, 40, 28, 40))
    add("strip_trim", trim_svg(w2, h2, [(13, -6), (w2 - 13, -6), (w2 - 13, h2 - 35), (w2 - 35, h2 - 13),
                                       (35, h2 - 13), (13, h2 - 35)], width=1.6), (8, 40, 28, 40))
    # bars
    wb, hb = 64, 24
    tpoly = rect_poly(wb, hb, tl=5, tr=5, br=5, bl=5)
    skin = dict(OFFLINE, dark=(4, 6, 9), mid=(16, 20, 26), light=(58, 70, 84), fill=("#06090d", 1.0))
    add("bar_track", frame_svg(wb, hb, skin, tpoly, bw=3, sheen=6, flip=True, edge_w=1.0), (8, 8, 8, 8))
    # banner (victory / defeat / large notices)
    wn, hn = 256, 160
    outer = rect_poly(wn, hn, tl=34, tr=34, br=34, bl=34)
    add("banner_base", frame_svg(wn, hn, OFFLINE, outer, bw=8, sheen=36,
                                 rivets=[(20, 20, 2.8), (wn - 20, 20, 2.8), (20, hn - 20, 2.8), (wn - 20, hn - 20, 2.8)],
                                 hazard=(wn / 2 - 40, hn - 10, 80, 6)), (48, 48, 48, 48))
    add("banner_trim", trim_svg(wn, hn, rect_poly(wn, hn, tl=34, tr=34, br=34, bl=34, m=16), width=2.0), (48, 48, 48, 48))
    # roster / command-card slot: square, same family as buttons but chunkier bevel
    ws = 96
    outer = rect_poly(ws, ws, tl=14, tr=4, br=14, bl=4)
    add("slot_base", frame_svg(ws, ws, dict(OFFLINE, fill=("#0d141c", 0.95)), outer, bw=5, sheen=16), (20, 20, 20, 20))
    add("slot_trim", trim_svg(ws, ws, rect_poly(ws, ws, tl=12, tr=3, br=12, bl=3, m=8), width=1.8), (20, 20, 20, 20))


def machine_panel():
    w = h = 128
    outer = rounded_poly(w, h, 26, m=0, segs=8)
    add("machine_panel_base", frame_svg(w, h, MACHINE, outer, bw=5, sheen=26, gap_ring=(2.0, 3.0),
                                        lens=(22, 22, 5.5)), (36, 36, 36, 36))
    add("machine_panel_trim", trim_svg(w, h, rounded_poly(w, h, 16, m=17, segs=8), width=1.8), (36, 36, 36, 36))
    wb = hb = 64
    outer = rounded_poly(wb, hb, 12, segs=6)
    for name, skin in (("normal", dict(MACHINE, fill=("#0a1a24", 0.96))),
                       ("hover", dict(MACHINE, fill=("#10303e", 0.97))),
                       ("disabled", dict(MACHINE, dark=(30, 36, 42), mid=(70, 80, 90), light=(110, 122, 134),
                                         fill=("#070d12", 0.9)))):
        add("machine_btn_" + name, frame_svg(wb, hb, skin, outer, bw=4, sheen=12, edge_w=1.2), (16, 16, 16, 16))
    add("machine_btn_trim", trim_svg(wb, hb, rounded_poly(wb, hb, 8, m=6, segs=6), width=1.6), (16, 16, 16, 16))


def hazard_tile():
    add("hazard_tile", svg_open(32, 32) + '<rect width="32" height="32" fill="#ffc21a"/>'
        '<g fill="#0a0d10"><polygon points="0,8 8,0 16,0 0,16"/><polygon points="0,24 24,0 32,0 32,8 8,32 0,32"/>'
        '<polygon points="16,32 32,16 32,24 24,32"/></g></svg>\n', (0, 0, 0, 0))


# ---------------------------------------------------------------------------------------------------------------
# Command-card glyphs: 64x64 grid, white, bold; the HUD tints them.

W = "#ffffff"


def g(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 64 64">'
            '<g fill="%s" stroke="none" stroke-linejoin="miter">%s</g></svg>\n' % (W, body))


def sword(rot):
    return ('<g transform="rotate(%d 32 32)"><polygon points="32,3 37,10 37,42 27,42 27,10"/>'
            '<rect x="17" y="42" width="30" height="6"/><rect x="29" y="48" width="6" height="10"/>'
            '<circle cx="32" cy="60" r="3"/></g>' % rot)


GLYPHS_SVG = {
    "move": g('<polygon points="4,24 32,24 32,11 60,32 32,53 32,40 4,40"/>'),
    "attack": g(sword(45) + sword(-45)),
    "hold": g('<path fill-rule="evenodd" d="M21,4 H43 L60,21 V43 L43,60 H21 L4,43 V21 Z '
              'M22,20 H29 V44 H22 Z M35,20 H42 V44 H35 Z"/>'),
    "fall_back": g('<path d="M44,58 V26 a13,13 0 0 0 -26,0 V40" fill="none" stroke="%s" stroke-width="9"/>'
                   '<polygon points="6,38 30,38 18,56"/>' % W),
    "secure": g('<circle cx="32" cy="32" r="17" fill="none" stroke="%s" stroke-width="7"/>'
                '<rect x="29" y="2" width="6" height="16"/><rect x="29" y="46" width="6" height="16"/>'
                '<rect x="2" y="29" width="16" height="6"/><rect x="46" y="29" width="16" height="6"/>'
                '<circle cx="32" cy="32" r="5"/>' % W),
    "defend": g('<path fill-rule="evenodd" d="M32,3 L58,12 V32 C58,47 47,57 32,62 C17,57 6,47 6,32 V12 Z '
                'M32,17 L46,22 V33 C46,41 40,47 32,50 C24,47 18,41 18,33 V22 Z"/><rect x="29" y="24" width="6" height="18"/>'),
    "build": g('<g transform="rotate(-40 32 32)"><rect x="8" y="7" width="48" height="17" rx="2"/>'
               '<rect x="44" y="7" width="12" height="17" rx="2"/><rect x="27" y="24" width="10" height="36" rx="3"/></g>'),
    "cancel": g('<g transform="rotate(45 32 32)"><rect x="26" y="4" width="12" height="56"/>'
                '<rect x="4" y="26" width="56" height="12"/></g>'),
    "reinforce": g('<rect x="26" y="4" width="12" height="30"/><rect x="17" y="13" width="30" height="12"/>'
                   '<polyline points="10,44 32,32 54,44" fill="none" stroke="%s" stroke-width="8"/>'
                   '<polyline points="10,58 32,46 54,58" fill="none" stroke="%s" stroke-width="8"/>' % (W, W)),
    "power": g('<polygon points="38,2 12,36 28,36 22,62 52,24 34,24"/>'),
    "lock": g('<path fill-rule="evenodd" d="M12,28 H52 V60 H12 Z M32,38 a5,5 0 1 0 0.01,0 Z M29.5,42 h5 v10 h-5 Z"/>'
              '<path d="M21,28 V20 a11,11 0 0 1 22,0 V28" fill="none" stroke="%s" stroke-width="7"/>' % W),
    "pause": g('<rect x="14" y="10" width="13" height="44"/><rect x="37" y="10" width="13" height="44"/>'),
    "start": g('<polygon points="14,6 56,32 14,58"/>'),
    "role_frontline": g('<path fill-rule="evenodd" d="M8,8 H56 V34 C56,46 46,56 32,60 C18,56 8,46 8,34 Z '
                        'M20,20 H44 V26 H20 Z M29,26 H35 V46 H29 Z"/>'),
    "role_ranged": g('<rect x="4" y="27" width="46" height="6"/><rect x="42" y="23" width="18" height="14" rx="2"/>'
                     '<polygon points="4,27 4,44 14,44 20,33 20,27"/><rect x="24" y="16" width="14" height="8" rx="2"/>'
                     '<rect x="26" y="33" width="4" height="10"/>'),
    "role_siege": g('<g transform="rotate(-38 32 32)"><rect x="6" y="20" width="44" height="14" rx="3"/>'
                    '<rect x="46" y="16" width="12" height="22" rx="2"/></g>'
                    '<rect x="8" y="50" width="48" height="8"/><circle cx="20" cy="52" r="7"/><circle cx="44" cy="52" r="7"/>'),
}

# ---------------------------------------------------------------------------------------------------------------


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def png(svg_path, size=None):
    out = svg_path[:-4] + ".png"
    cmd = ["rsvg-convert", svg_path, "-o", out]
    if size:
        cmd += ["-w", str(size[0]), "-h", str(size[1])]
    subprocess.check_call(cmd)


def main():
    offline_panel()
    offline_button()
    offline_misc()
    machine_panel()
    hazard_tile()
    lines = ["# 9-slice margins in texels: top right bottom left. Textures are 2 texels per reference pixel."]
    for name in sorted(FILES):
        path = os.path.join(FRAMES, name + ".svg")
        write(path, FILES[name])
        png(path)
        lines.append("%-22s %s" % (name, " ".join(str(v) for v in SLICES[name])))
    write(os.path.join(FRAMES, "slices.txt"), "\n".join(lines) + "\n")
    for name, svg in sorted(GLYPHS_SVG.items()):
        path = os.path.join(GLYPHS, name + ".svg")
        write(path, svg)
        png(path)
    print("UI_ASSETS_EXPORTED frames=%d glyphs=%d" % (len(FILES), len(GLYPHS_SVG)))


if __name__ == "__main__":
    sys.exit(main())
