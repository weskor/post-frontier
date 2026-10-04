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

from collections.abc import Sequence
import math
import os
import subprocess
from typing import NotRequired, TypedDict

Point = tuple[float, float]
Polygon = list[Point]
Color = tuple[float, float, float]
Fill = tuple[str, float]
SliceMargins = tuple[int, int, int, int]
Rivet = tuple[float, float, float]


class Skin(TypedDict):
    dark: Color
    mid: Color
    light: Color
    fill: Fill
    fill_top: Fill
    fill_bottom: Fill
    edge: str
    groove: str
    rivet: tuple[str, str]
    hazard: tuple[str, str]


class ButtonState(TypedDict):
    skin: Skin
    fill: Fill
    flip: NotRequired[bool]


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI = os.path.join(ROOT, "Art", "UI")
FRAMES = os.path.join(UI, "frames")
GLYPHS = os.path.join(UI, "icons", "commands")

# ---------------------------------------------------------------------------------------------------------------
# Geometry helpers (convex polygons, y down)


def rect_poly(
    w: float,
    h: float,
    tl: float = 0.0,
    tr: float = 0.0,
    br: float = 0.0,
    bl: float = 0.0,
    m: float = 0.0,
) -> Polygon:
    """Rectangle with angled (chamfered) corners, inset by margin m. Returns points clockwise from top-left."""
    x0, y0, x1, y1 = m, m, w - m, h - m
    pts: Polygon = []
    pts += [(x0, y0 + tl), (x0 + tl, y0)] if tl else [(x0, y0)]
    pts += [(x1 - tr, y0), (x1, y0 + tr)] if tr else [(x1, y0)]
    pts += [(x1, y1 - br), (x1 - br, y1)] if br else [(x1, y1)]
    pts += [(x0 + bl, y1), (x0, y1 - bl)] if bl else [(x0, y1)]
    return dedupe(pts)


def dedupe(pts: Sequence[Point]) -> Polygon:
    out: Polygon = []
    for p in pts:
        if not out or (abs(out[-1][0] - p[0]) > 1e-6 or abs(out[-1][1] - p[1]) > 1e-6):
            out.append(p)
    if abs(out[0][0] - out[-1][0]) < 1e-6 and abs(out[0][1] - out[-1][1]) < 1e-6:
        out.pop()
    return out


def rounded_poly(
    w: float,
    h: float,
    r: float,
    cut: float = 0.0,
    m: float = 0.0,
    segs: int = 6,
    corners: tuple[bool, bool, bool, bool] = (True, True, True, True),
) -> Polygon:
    """Rectangle with round corners (Machine skin) approximated by segs segments; corners = TL, TR, BR, BL."""
    x0, y0, x1, y1 = m, m, w - m, h - m
    centers = [
        (x0 + r, y0 + r, 180),
        (x1 - r, y0 + r, 270),
        (x1 - r, y1 - r, 0),
        (x0 + r, y1 - r, 90),
    ]
    pts: Polygon = []
    for (cx, cy, a0), on in zip(centers, corners, strict=False):
        if not on:
            pts.append(
                (cx + (r if a0 in (270, 0) else -r), cy + (r if a0 in (0, 90) else -r))
            )
            continue
        for i in range(segs + 1):
            a = math.radians(a0 + 90.0 * i / segs)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return dedupe(pts)


def signed_area(pts: Sequence[Point]) -> float:
    return (
        sum(
            pts[i][0] * pts[(i + 1) % len(pts)][1]
            - pts[(i + 1) % len(pts)][0] * pts[i][1]
            for i in range(len(pts))
        )
        / 2.0
    )


def inset(pts: Sequence[Point], d: float) -> Polygon:
    """Offset a convex clockwise (y-down) polygon inward by d using mitred offset lines."""
    n = len(pts)
    normals = []
    for i in range(n):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        dx, dy = x1 - x0, y1 - y0
        length = math.hypot(dx, dy) or 1.0
        normals.append(
            (-dy / length, dx / length)
        )  # inward for clockwise in y-down (checked in ring())
    out = []
    for i in range(n):
        nx0, ny0 = normals[i - 1]
        nx1, ny1 = normals[i]
        k = 1.0 + nx0 * nx1 + ny0 * ny1
        px, py = pts[i]
        out.append((px + (nx0 + nx1) * d / k, py + (ny0 + ny1) * d / k))
    return out


def fmt(pts: Sequence[Point]) -> str:
    return " ".join(f"{x:.2f},{y:.2f}" for x, y in pts)


def lerp(a: Color, b: Color, t: float) -> Color:
    return (
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
    )


def hexcol(c: Color) -> str:
    red, green, blue = (max(0, min(255, round(v))) for v in c)
    return f"#{red:02x}{green:02x}{blue:02x}"


def ramp(t: float, dark: Color, mid: Color, light: Color) -> Color:
    """t in [-1, 1] (edge normal . light direction) -> colour."""
    t = max(-1.0, min(1.0, t))
    return lerp(mid, light, t) if t >= 0 else lerp(mid, dark, -t)


LIGHT = (-0.55, -0.83)  # direction TOWARD the light (upper left)

# ---------------------------------------------------------------------------------------------------------------
# Skins. Each is a dict of tokens (see STYLE.md "Colour tokens").

OFFLINE: Skin = dict(
    dark=(9, 13, 18),
    mid=(52, 66, 82),
    light=(132, 154, 176),
    fill=("#0b1118", 0.88),
    fill_top=("#3a4c60", 0.16),
    fill_bottom=("#000000", 0.30),
    edge="#04070a",
    groove="#000000",
    rivet=("#93a6b9", "#1a222b"),
    hazard=("#ffc21a", "#0a0d10"),
)
MACHINE: Skin = dict(
    dark=(72, 92, 110),
    mid=(178, 196, 210),
    light=(250, 253, 255),
    fill=("#06121a", 0.86),
    fill_top=("#7fe9ff", 0.14),
    fill_bottom=("#000000", 0.30),
    edge="#0a1620",
    groove="#02090e",
    rivet=("#ffffff", "#5a7284"),
    hazard=("#ff3b30", "#ff3b30"),
)


def svg_open(w: float, h: float) -> str:
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{int(w)}" height="{int(h)}" '
        f'viewBox="0 0 {int(w)} {int(h)}">\n'
    )


DEFS_HAZARD = """<defs>
<pattern id="hz" width="10" height="10" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">
<rect width="10" height="10" fill="%s"/><rect width="5" height="10" fill="%s"/></pattern>
<filter id="glow" x="-20%%" y="-20%%" width="140%%" height="140%%"><feGaussianBlur stdDeviation="2.2"/></filter>
<filter id="glow4" x="-20%%" y="-20%%" width="140%%" height="140%%"><feGaussianBlur stdDeviation="4"/></filter>
</defs>
"""


def ring(
    outer: Sequence[Point],
    bw: float,
    skin: Skin,
    flip: bool = False,
) -> tuple[Polygon, str]:
    """Bevel ring between outer polygon and outer inset by bw; each edge quad lit by its outward normal."""
    inner = inset(outer, bw)
    n = len(outer)
    parts = []
    for i in range(n):
        (x0, y0), (x1, y1) = outer[i], outer[(i + 1) % n]
        dx, dy = x1 - x0, y1 - y0
        length = math.hypot(dx, dy) or 1.0
        out_n = (
            dy / length,
            -dx / length,
        )  # outward normal for clockwise y-down polygons
        lit = out_n[0] * LIGHT[0] + out_n[1] * LIGHT[1]
        if flip:
            lit = -lit
        col = hexcol(ramp(lit * 1.25, skin["dark"], skin["mid"], skin["light"]))
        quad = [outer[i], outer[(i + 1) % n], inner[(i + 1) % n], inner[i]]
        parts.append(
            f'<polygon points="{fmt(quad)}" fill="{col}" stroke="{col}" stroke-width="0.6" stroke-linejoin="round"/>'
        )
    return inner, "\n".join(parts)


def frame_svg(
    w: float,
    h: float,
    skin: Skin,
    outer: Sequence[Point],
    bw: float,
    fill: bool = True,
    sheen: float = 18,
    trim: Sequence[Point] | None = None,
    rivets: Sequence[Rivet] = (),
    hazard: tuple[float, float, float, float] | None = None,
    gap_ring: tuple[float, float] | None = None,
    lens: Rivet | None = None,
    flip: bool = False,
    edge_w: float = 1.4,
) -> str:
    """Base layer of a frame. `outer` polygon; bw bevel width; optional second ring (Machine floating gap)."""
    s = [svg_open(w, h), DEFS_HAZARD % (skin["hazard"][0], skin["hazard"][1])]
    s.append(
        f'<polygon points="{fmt(outer)}" fill="{skin["edge"]}"/>'
    )  # dark silhouette edge
    body = inset(outer, edge_w)
    inner, ring_svg = ring(body, bw, skin, flip=flip)
    s.append(ring_svg)
    if gap_ring:  # floating inner ring
        gap_inner = inset(inner, gap_ring[0])
        s.append(f'<polygon points="{fmt(gap_inner)}" fill="{skin["edge"]}"/>')
        inner2, ring2 = ring(inset(gap_inner, 0.8), gap_ring[1], skin, flip=not flip)
        s.append(ring2)
        inner = inner2
    if fill:
        s.append(
            f'<polygon points="{fmt(inner)}" fill="{skin["fill"][0]}" fill-opacity="{skin["fill"][1]:.2f}"/>'
        )
        xs = [p[0] for p in inner]
        ys = [p[1] for p in inner]
        clip = f"clip{abs(hash(fmt(inner))) % 10**6}"
        s.append(f'<clipPath id="{clip}"><polygon points="{fmt(inner)}"/></clipPath>')
        if sheen:
            s.append(
                f'<linearGradient id="{clip}_t" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{skin["fill_top"][0]}" stop-opacity="{skin["fill_top"][1]:.2f}"/>'
                f'<stop offset="1" stop-color="{skin["fill_top"][0]}" stop-opacity="0"/></linearGradient>'
            )
            s.append(
                f'<rect x="{min(xs):.2f}" y="{min(ys):.2f}" width="{max(xs) - min(xs):.2f}" height="{int(sheen)}" fill="url(#{clip}_t)" clip-path="url(#{clip})"/>'
            )
            s.append(
                f'<linearGradient id="{clip}_b" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="{skin["fill_bottom"][0]}" stop-opacity="{skin["fill_bottom"][1]:.2f}"/>'
                f'<stop offset="1" stop-color="{skin["fill_bottom"][0]}" stop-opacity="0"/></linearGradient>'
            )
            s.append(
                f'<rect x="{min(xs):.2f}" y="{max(ys) - sheen:.2f}" width="{max(xs) - min(xs):.2f}" height="{int(sheen)}" fill="url(#{clip}_b)" clip-path="url(#{clip})"/>'
            )
    s.append(
        f'<polygon points="{fmt(inner)}" fill="none" stroke="{skin["groove"]}" stroke-opacity="0.75" stroke-width="1.2"/>'
    )  # inner groove
    for cx, cy, r in rivets:
        s.append(
            f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r:.2f}" fill="{skin["rivet"][1]}"/>'
            f'<circle cx="{cx - r * 0.2:.2f}" cy="{cy - r * 0.2:.2f}" r="{r * 0.7:.2f}" fill="{skin["rivet"][0]}"/>'
        )
    if hazard:  # (x, y, w, h)
        x, y, hw, hh = hazard
        s.append(
            f'<rect x="{x:.2f}" y="{y:.2f}" width="{hw:.2f}" height="{hh:.2f}" fill="url(#hz)" stroke="{skin["edge"]}" stroke-width="1"/>'
        )
    if lens:
        cx, cy, r = lens
        s.append(
            f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r:.2f}" fill="#2a0b0b" stroke="#c8d6e0" stroke-width="1.2"/>'
            f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r * 0.55:.2f}" fill="#ff3b30"/>'
        )
    s.append("</svg>\n")
    return "".join(s)


def trim_svg(
    w: float,
    h: float,
    poly: Sequence[Point],
    width: float = 2.0,
    color: str = "#ffffff",
    glow: float = 0.55,
) -> str:
    """White glowing line following poly (already positioned). Glow is baked as a blurred copy underneath."""
    s = [svg_open(w, h), DEFS_HAZARD % ("#000", "#000")]
    d = "M" + " L".join(f"{x:.2f},{y:.2f}" for x, y in poly) + " Z"
    s.append(
        f'<path d="{d}" fill="none" stroke="{color}" stroke-opacity="{glow * 0.8:.2f}" stroke-width="{width * 3.0:.1f}" filter="url(#glow4)"/>'
    )
    s.append(
        f'<path d="{d}" fill="none" stroke="{color}" stroke-opacity="{glow:.2f}" stroke-width="{width * 1.5:.1f}" filter="url(#glow)"/>'
    )
    s.append(
        f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width:.1f}" stroke-linejoin="miter"/>'
    )
    s.append("</svg>\n")
    return "".join(s)


# ---------------------------------------------------------------------------------------------------------------
# Frame catalogue: name -> (svg, (slice top, right, bottom, left) in texels)

SLICES: dict[str, SliceMargins] = {}
FILES: dict[str, str] = {}


def add(name: str, svg: str, slice_tuple: SliceMargins) -> None:
    FILES[name] = svg
    SLICES[name] = slice_tuple


def offline_panel() -> None:
    w = h = 128
    outer = rect_poly(w, h, tl=18, tr=6, br=18, bl=6)
    add(
        "panel_base",
        frame_svg(
            w,
            h,
            OFFLINE,
            outer,
            bw=8,
            sheen=22,
            rivets=[
                (14, 24, 2.6),
                (w - 14, h - 24, 2.6),
                (w - 12, 12, 2.2),
                (12, h - 12, 2.2),
            ],
            hazard=(4, h - 34, 6, 20),
        ),
        (32, 32, 32, 32),
    )
    trim_poly = rect_poly(w, h, tl=24, tr=10, br=24, bl=10, m=14)
    add("panel_trim", trim_svg(w, h, trim_poly), (32, 32, 32, 32))


def offline_button() -> None:
    w = h = 64
    outer = rect_poly(w, h, tl=10, tr=3, br=10, bl=3)
    states: dict[str, ButtonState] = {
        "normal": dict(skin={**OFFLINE}, fill=("#141d27", 0.97)),
        "hover": dict(
            skin={**OFFLINE, "mid": (76, 96, 118), "light": (168, 192, 214)},
            fill=("#1c2a39", 0.98),
        ),
        "pressed": dict(
            skin={
                **OFFLINE,
                "mid": (30, 40, 52),
                "dark": (120, 140, 160),
                "light": (9, 13, 18),
            },
            fill=("#090e14", 0.98),
            flip=True,
        ),
        "disabled": dict(
            skin={
                **OFFLINE,
                "dark": (12, 15, 19),
                "mid": (30, 36, 43),
                "light": (52, 60, 70),
            },
            fill=("#0c1015", 0.90),
        ),
    }
    for name, cfg in states.items():
        skin = cfg["skin"]
        skin["fill"] = cfg["fill"]
        skin["fill_top"] = ("#5b7590", 0.20 if name != "disabled" else 0.04)
        add(
            "btn_" + name,
            frame_svg(
                w,
                h,
                skin,
                outer,
                bw=4,
                sheen=12,
                flip=cfg.get("flip", False),
                edge_w=1.2,
            ),
            (14, 14, 14, 14),
        )
    add(
        "btn_trim",
        trim_svg(w, h, rect_poly(w, h, tl=10, tr=3, br=10, bl=3, m=6), width=1.6),
        (14, 14, 14, 14),
    )


def offline_misc() -> None:
    # portrait frame: thick bevel, transparent interior
    w = h = 128
    outer = rect_poly(w, h, tl=16, tr=4, br=16, bl=4)
    add(
        "portrait_base",
        frame_svg(
            w,
            h,
            OFFLINE,
            outer,
            bw=9,
            fill=False,
            sheen=0,
            rivets=[(w - 14, 13, 2.2), (14, h - 13, 2.2)],
            hazard=(w - 46, h - 8, 34, 6),
        ),
        (28, 28, 28, 28),
    )
    add(
        "portrait_trim",
        trim_svg(w, h, rect_poly(w, h, tl=14, tr=3, br=14, bl=3, m=13), width=1.8),
        (28, 28, 28, 28),
    )
    # minimap: heavier frame, transparent interior, cut TL/BR
    outer = rect_poly(w, h, tl=24, tr=8, br=24, bl=8)
    add(
        "minimap_base",
        frame_svg(
            w,
            h,
            OFFLINE,
            outer,
            bw=10,
            fill=False,
            sheen=0,
            rivets=[
                (14, 30, 2.6),
                (w - 14, h - 30, 2.6),
                (w - 14, 14, 2.4),
                (14, h - 14, 2.4),
            ],
            hazard=(6, h - 46, 8, 26),
        ),
        (36, 36, 36, 36),
    )
    add(
        "minimap_trim",
        trim_svg(w, h, rect_poly(w, h, tl=26, tr=9, br=26, bl=9, m=15), width=1.8),
        (36, 36, 36, 36),
    )
    # top strip: no top border, angled lower corners
    w2, h2 = 128, 64
    outer = rect_poly(w2, h2, tl=0, tr=0, br=26, bl=26)
    svg = frame_svg(
        w2, h2, OFFLINE, outer, bw=6, sheen=14, rivets=[(w2 - 26, h2 - 20, 2.2)]
    )
    add("strip_base", svg, (8, 40, 28, 40))
    add(
        "strip_trim",
        trim_svg(
            w2,
            h2,
            [
                (13, -6),
                (w2 - 13, -6),
                (w2 - 13, h2 - 35),
                (w2 - 35, h2 - 13),
                (35, h2 - 13),
                (13, h2 - 35),
            ],
            width=1.6,
        ),
        (8, 40, 28, 40),
    )
    # bars
    wb, hb = 64, 24
    tpoly = rect_poly(wb, hb, tl=5, tr=5, br=5, bl=5)
    skin: Skin = {
        **OFFLINE,
        "dark": (4, 6, 9),
        "mid": (16, 20, 26),
        "light": (58, 70, 84),
        "fill": ("#06090d", 1.0),
    }
    add(
        "bar_track",
        frame_svg(wb, hb, skin, tpoly, bw=3, sheen=6, flip=True, edge_w=1.0),
        (8, 8, 8, 8),
    )
    # banner (victory / defeat / large notices)
    wn, hn = 256, 160
    outer = rect_poly(wn, hn, tl=34, tr=34, br=34, bl=34)
    add(
        "banner_base",
        frame_svg(
            wn,
            hn,
            OFFLINE,
            outer,
            bw=8,
            sheen=36,
            rivets=[
                (20, 20, 2.8),
                (wn - 20, 20, 2.8),
                (20, hn - 20, 2.8),
                (wn - 20, hn - 20, 2.8),
            ],
            hazard=(wn / 2 - 40, hn - 10, 80, 6),
        ),
        (48, 48, 48, 48),
    )
    add(
        "banner_trim",
        trim_svg(
            wn, hn, rect_poly(wn, hn, tl=34, tr=34, br=34, bl=34, m=16), width=2.0
        ),
        (48, 48, 48, 48),
    )
    # roster / command-card slot: square, same family as buttons but chunkier bevel
    ws = 96
    outer = rect_poly(ws, ws, tl=14, tr=4, br=14, bl=4)
    add(
        "slot_base",
        frame_svg(
            ws, ws, {**OFFLINE, "fill": ("#0d141c", 0.95)}, outer, bw=5, sheen=16
        ),
        (20, 20, 20, 20),
    )
    add(
        "slot_trim",
        trim_svg(ws, ws, rect_poly(ws, ws, tl=12, tr=3, br=12, bl=3, m=8), width=1.8),
        (20, 20, 20, 20),
    )


def machine_panel() -> None:
    w = h = 128
    outer = rounded_poly(w, h, 26, m=0, segs=8)
    add(
        "machine_panel_base",
        frame_svg(
            w,
            h,
            MACHINE,
            outer,
            bw=5,
            sheen=26,
            gap_ring=(2.0, 3.0),
            lens=(22, 22, 5.5),
        ),
        (36, 36, 36, 36),
    )
    add(
        "machine_panel_trim",
        trim_svg(w, h, rounded_poly(w, h, 16, m=17, segs=8), width=1.8),
        (36, 36, 36, 36),
    )
    wb = hb = 64
    outer = rounded_poly(wb, hb, 12, segs=6)
    states: dict[str, Skin] = {
        "normal": {**MACHINE, "fill": ("#0a1a24", 0.96)},
        "hover": {**MACHINE, "fill": ("#10303e", 0.97)},
        "disabled": {
            **MACHINE,
            "dark": (30, 36, 42),
            "mid": (70, 80, 90),
            "light": (110, 122, 134),
            "fill": ("#070d12", 0.9),
        },
    }
    for name, skin in states.items():
        add(
            "machine_btn_" + name,
            frame_svg(wb, hb, skin, outer, bw=4, sheen=12, edge_w=1.2),
            (16, 16, 16, 16),
        )
    add(
        "machine_btn_trim",
        trim_svg(wb, hb, rounded_poly(wb, hb, 8, m=6, segs=6), width=1.6),
        (16, 16, 16, 16),
    )


def hazard_tile() -> None:
    add(
        "hazard_tile",
        svg_open(32, 32) + '<rect width="32" height="32" fill="#ffc21a"/>'
        '<g fill="#0a0d10"><polygon points="0,8 8,0 16,0 0,16"/><polygon points="0,24 24,0 32,0 32,8 8,32 0,32"/>'
        '<polygon points="16,32 32,16 32,24 24,32"/></g></svg>\n',
        (0, 0, 0, 0),
    )


# ---------------------------------------------------------------------------------------------------------------
# Command-card glyphs: 64x64 grid, white, bold; the HUD tints them.

W = "#ffffff"


def g(body: str) -> str:
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 64 64">'
        f'<g fill="{W}" stroke="none" stroke-linejoin="miter">{body}</g></svg>\n'
    )


def sword(rot: float) -> str:
    return (
        f'<g transform="rotate({int(rot)} 32 32)"><polygon points="32,3 37,10 37,42 27,42 27,10"/>'
        '<rect x="17" y="42" width="30" height="6"/><rect x="29" y="48" width="6" height="10"/>'
        '<circle cx="32" cy="60" r="3"/></g>'
    )


GLYPHS_SVG: dict[str, str] = {
    "move": g('<polygon points="4,24 32,24 32,11 60,32 32,53 32,40 4,40"/>'),
    "attack": g(sword(45) + sword(-45)),
    "hold": g(
        '<path fill-rule="evenodd" d="M21,4 H43 L60,21 V43 L43,60 H21 L4,43 V21 Z '
        'M22,20 H29 V44 H22 Z M35,20 H42 V44 H35 Z"/>'
    ),
    "fall_back": g(
        f'<path d="M44,58 V26 a13,13 0 0 0 -26,0 V40" fill="none" stroke="{W}" stroke-width="9"/>'
        '<polygon points="6,38 30,38 18,56"/>'
    ),
    "secure": g(
        f'<circle cx="32" cy="32" r="17" fill="none" stroke="{W}" stroke-width="7"/>'
        '<rect x="29" y="2" width="6" height="16"/><rect x="29" y="46" width="6" height="16"/>'
        '<rect x="2" y="29" width="16" height="6"/><rect x="46" y="29" width="16" height="6"/>'
        '<circle cx="32" cy="32" r="5"/>'
    ),
    "defend": g(
        '<path fill-rule="evenodd" d="M32,3 L58,12 V32 C58,47 47,57 32,62 C17,57 6,47 6,32 V12 Z '
        'M32,17 L46,22 V33 C46,41 40,47 32,50 C24,47 18,41 18,33 V22 Z"/><rect x="29" y="24" width="6" height="18"/>'
    ),
    "build": g(
        '<g transform="rotate(-40 32 32)"><rect x="8" y="7" width="48" height="17" rx="2"/>'
        '<rect x="44" y="7" width="12" height="17" rx="2"/><rect x="27" y="24" width="10" height="36" rx="3"/></g>'
    ),
    "cancel": g(
        '<g transform="rotate(45 32 32)"><rect x="26" y="4" width="12" height="56"/>'
        '<rect x="4" y="26" width="56" height="12"/></g>'
    ),
    "reinforce": g(
        '<rect x="26" y="4" width="12" height="30"/><rect x="17" y="13" width="30" height="12"/>'
        f'<polyline points="10,44 32,32 54,44" fill="none" stroke="{W}" stroke-width="8"/>'
        f'<polyline points="10,58 32,46 54,58" fill="none" stroke="{W}" stroke-width="8"/>'
    ),
    "power": g('<polygon points="38,2 12,36 28,36 22,62 52,24 34,24"/>'),
    "lock": g(
        '<path fill-rule="evenodd" d="M12,28 H52 V60 H12 Z M32,38 a5,5 0 1 0 0.01,0 Z M29.5,42 h5 v10 h-5 Z"/>'
        f'<path d="M21,28 V20 a11,11 0 0 1 22,0 V28" fill="none" stroke="{W}" stroke-width="7"/>'
    ),
    "pause": g(
        '<rect x="14" y="10" width="13" height="44"/><rect x="37" y="10" width="13" height="44"/>'
    ),
    "start": g('<polygon points="14,6 56,32 14,58"/>'),
    "role_frontline": g(
        '<path fill-rule="evenodd" d="M8,8 H56 V34 C56,46 46,56 32,60 C18,56 8,46 8,34 Z '
        'M20,20 H44 V26 H20 Z M29,26 H35 V46 H29 Z"/>'
    ),
    "role_ranged": g(
        '<rect x="4" y="27" width="46" height="6"/><rect x="42" y="23" width="18" height="14" rx="2"/>'
        '<polygon points="4,27 4,44 14,44 20,33 20,27"/><rect x="24" y="16" width="14" height="8" rx="2"/>'
        '<rect x="26" y="33" width="4" height="10"/>'
    ),
    "role_siege": g(
        '<g transform="rotate(-38 32 32)"><rect x="6" y="20" width="44" height="14" rx="3"/>'
        '<rect x="46" y="16" width="12" height="22" rx="2"/></g>'
        '<rect x="8" y="50" width="48" height="8"/><circle cx="20" cy="52" r="7"/><circle cx="44" cy="52" r="7"/>'
    ),
    "role_assault": g(
        '<path fill-rule="evenodd" d="M6,18 H28 V36 C28,47 23,54 17,58 C11,54 6,47 6,36 Z '
        'M12,24 H22 V36 C22,43 20,47 17,50 C14,47 12,43 12,36 Z"/>'
        '<polygon points="44,4 56,24 47,24 47,48 41,48 41,24 32,24"/>'
        '<rect x="34" y="46" width="20" height="6"/><rect x="41" y="52" width="6" height="8"/>'
    ),
    "role_support": g(
        '<rect x="27" y="27" width="10" height="10"/>'
        f'<path d="M22,18 H18 V46 H22 M42,18 H46 V46 H42 '
        f'M12,8 H6 V56 H12 M52,8 H58 V56 H52" fill="none" stroke="{W}" stroke-width="6"/>'
    ),
}

# ---------------------------------------------------------------------------------------------------------------


def write(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def png(svg_path: str, size: tuple[int, int] | None = None) -> None:
    out = svg_path[:-4] + ".png"
    cmd = ["rsvg-convert", svg_path, "-o", out]
    if size:
        cmd += ["-w", str(size[0]), "-h", str(size[1])]
    subprocess.check_call(cmd)


def main() -> None:
    offline_panel()
    offline_button()
    offline_misc()
    machine_panel()
    hazard_tile()
    lines = [
        "# 9-slice margins in texels: top right bottom left. Textures are 2 texels per reference pixel."
    ]
    for name in sorted(FILES):
        path = os.path.join(FRAMES, name + ".svg")
        write(path, FILES[name])
        png(path)
        lines.append(f"{name:<22} {' '.join(str(v) for v in SLICES[name])}")
    write(os.path.join(FRAMES, "slices.txt"), "\n".join(lines) + "\n")
    for name, svg in sorted(GLYPHS_SVG.items()):
        path = os.path.join(GLYPHS, name + ".svg")
        write(path, svg)
        png(path)
    print(f"UI_ASSETS_EXPORTED frames={len(FILES)} glyphs={len(GLYPHS_SVG)}")


if __name__ == "__main__":
    main()
