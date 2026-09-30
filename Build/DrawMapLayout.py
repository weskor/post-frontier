#!/usr/bin/env python3
"""Validate a map data file and draw its top-down layout plan.

    python3 Build/DrawMapLayout.py [Build/Maps/AvailabilityZone.json] [--out Art/Maps/AvailabilityZone-layout.png]
                                   [--svg PATH] [--report] [--quiet]

The single source of truth is Build/Maps/<Map>.json (arena, HQs, sectors, elevation regions, ramps, blockers,
chokes, decoration zones, proposals); the Unreal generator is meant to read the same file. This script:

  1. rasterises the regions onto a 100 cm grid (X = minimap-up, Y = minimap-right, like the game's minimap),
  2. runs the design checks the docs rely on (level overlap and cliff bands, ramp slopes and widths, sectors on
     one level, ramps and chokes outside every territory disc, choke widths, rot180 symmetry, reachability),
  3. measures real walking distances at the unit speed in the file (16-neighbour Dijkstra with a 100 cm
     clearance ring, then line-of-sight smoothing; within about 1 % of a nav path),
  4. counts building placements with the 2D rules of ACommandGameState::ValidateBuildingPlacement,
  5. writes an SVG and rasterises it to PNG with rsvg-convert (or ImageMagick). No Python packages are needed.

Exit status is 1 if any check fails. `--report` prints the tables the design docs quote.
"""
import argparse
import heapq
import json
import math
import os
import shutil
import subprocess
import sys

CELL = 100.0
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SQ2 = math.sqrt(2.0)
MOVES = ([(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0), (1, 1, SQ2), (1, -1, SQ2), (-1, 1, SQ2), (-1, -1, SQ2)]
         + [(a, b, math.sqrt(5.0)) for a in (-2, 2) for b in (-1, 1)] + [(b, a, math.sqrt(5.0)) for a in (-2, 2) for b in (-1, 1)])


# ----------------------------------------------------------------------------- geometry
def bbox(poly):
    xs = [p[0] for p in poly]
    ys = [p[1] for p in poly]
    return min(xs), max(xs), min(ys), max(ys)


def inside(x, y, poly):
    """Even-odd ray cast. Points exactly on an edge may go either way; the design keeps clear of that."""
    hit = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            hit = not hit
        j = i
    return hit


def seg_dist(px, py, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    t = 0.0 if dx == dy == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def poly_dist(px, py, poly):
    """Distance from a point to the polygon (0 inside)."""
    if inside(px, py, poly):
        return 0.0
    return min(seg_dist(px, py, poly[i], poly[(i + 1) % len(poly)]) for i in range(len(poly)))


def centroid(poly):
    x0, x1, y0, y1 = bbox(poly)
    return (x0 + x1) / 2.0, (y0 + y1) / 2.0


def rot180(p):
    return [-p[0], -p[1]]


# ----------------------------------------------------------------------------- grid
class Grid:
    """i indexes X (south to north), j indexes Y (west to east); cell centres sit on multiples of CELL plus half."""

    def __init__(self, data, breach_open=False):
        hx, hy = data["arena"]["half_extent"]
        self.hx, self.hy = hx, hy
        self.nx, self.ny = int(2 * hx // CELL), int(2 * hy // CELL)
        self.levels = [lv["id"] for lv in data["elevation"]]
        nx, ny = self.nx, self.ny
        self.cover = [[0] * ny for _ in range(nx)]       # bit per level id
        self.ramp = [[-1] * ny for _ in range(nx)]       # ramp index
        self.block = [[False] * ny for _ in range(nx)]
        for region in data["regions"]:
            bit = 1 << self.levels.index(region["level"])
            for i, j in self.cells_in(region["poly"]):
                self.cover[i][j] |= bit
        for index, ramp in enumerate(data["ramps"]):
            for i, j in self.cells_in(ramp["poly"]):
                self.ramp[i][j] = index
        for blocker in data["blockers"]:
            for i, j in self.cells_in(blocker["poly"]):
                self.block[i][j] = True
        if breach_open:
            for i, j in self.cells_in(data["proposals"]["breach"]["poly"]):
                self.block[i][j] = False
                self.cover[i][j] |= 1 << self.levels.index("L0")
        self.walk = [[(self.cover[i][j] != 0 or self.ramp[i][j] >= 0) and not self.block[i][j] for j in range(ny)]
                     for i in range(nx)]
        self.nav = [[self.walk[i][j] and all(self.is_walk(i + a, j + b) for a in (-1, 0, 1) for b in (-1, 0, 1))
                     for j in range(ny)] for i in range(nx)]

    def centre(self, i, j):
        return -self.hx + (i + 0.5) * CELL, -self.hy + (j + 0.5) * CELL

    def cell(self, x, y):
        return int((x + self.hx) // CELL), int((y + self.hy) // CELL)

    def in_range(self, i, j):
        return 0 <= i < self.nx and 0 <= j < self.ny

    def is_walk(self, i, j):
        return self.in_range(i, j) and self.walk[i][j]

    def cells_in(self, poly):
        x0, x1, y0, y1 = bbox(poly)
        i0, j0 = self.cell(x0, y0)
        i1, j1 = self.cell(x1, y1)
        for i in range(max(0, i0), min(self.nx - 1, i1) + 1):
            for j in range(max(0, j0), min(self.ny - 1, j1) + 1):
                x, y = self.centre(i, j)
                # Sample a hair outward from the arena centre so rot180 partners get mirrored samples and polygon
                # edges that pass through cell centres resolve symmetrically.
                if inside(x + math.copysign(0.011, x), y + math.copysign(0.017, y), poly):
                    yield i, j

    def level_of(self, i, j):
        bits = self.cover[i][j]
        for k, name in enumerate(self.levels):
            if bits & (1 << k):
                return name
        return None

    def nearest_nav(self, x, y, limit=12):
        i0, j0 = self.cell(x, y)
        best = None
        for r in range(limit):
            for i in range(i0 - r, i0 + r + 1):
                for j in range(j0 - r, j0 + r + 1):
                    if self.in_range(i, j) and self.nav[i][j]:
                        d = math.hypot(*(a - b for a, b in zip(self.centre(i, j), (x, y))))
                        if best is None or d < best[0]:
                            best = (d, i, j)
            if best:
                return best[1], best[2]
        raise RuntimeError("No navigable cell near (%.0f, %.0f)" % (x, y))

    def edge_distance(self):
        """Approximate distance (cm) from each walkable cell to the nearest non-walkable cell (two-pass chamfer)."""
        big = 1e9
        d = [[0.0 if not self.walk[i][j] else big for j in range(self.ny)] for i in range(self.nx)]
        fwd = [(-1, -1, 1.4142), (-1, 0, 1.0), (-1, 1, 1.4142), (0, -1, 1.0)]
        bwd = [(1, 1, 1.4142), (1, 0, 1.0), (1, -1, 1.4142), (0, 1, 1.0)]
        for i in range(self.nx):
            for j in range(self.ny):
                for a, b, w in fwd:
                    if self.in_range(i + a, j + b):
                        d[i][j] = min(d[i][j], d[i + a][j + b] + w)
                    else:
                        d[i][j] = min(d[i][j], 1.0 + 0 * w)
        for i in range(self.nx - 1, -1, -1):
            for j in range(self.ny - 1, -1, -1):
                for a, b, w in bwd:
                    if self.in_range(i + a, j + b):
                        d[i][j] = min(d[i][j], d[i + a][j + b] + w)
                    else:
                        d[i][j] = min(d[i][j], 1.0)
        return [[v * CELL for v in row] for row in d]

    # ------------------------------------------------------------------------- paths
    def dijkstra(self, source, closed=None):
        closed = closed or set()
        inf = float("inf")
        dist = [[inf] * self.ny for _ in range(self.nx)]
        parent = {}
        si, sj = source
        dist[si][sj] = 0.0
        heap = [(0.0, si, sj)]
        nav = self.nav
        while heap:
            d, i, j = heapq.heappop(heap)
            if d > dist[i][j]:
                continue
            for a, b, w in MOVES:
                ni, nj = i + a, j + b
                if 0 <= ni < self.nx and 0 <= nj < self.ny and nav[ni][nj] and (ni, nj) not in closed:
                    # Knight moves must not clip a blocked corner.
                    if abs(a) == 2 and not (nav[i + a // 2][j] and nav[i + a // 2][nj]):
                        continue
                    if abs(b) == 2 and not (nav[i][j + b // 2] and nav[ni][j + b // 2]):
                        continue
                    if abs(a) == 1 and abs(b) == 1 and not (nav[ni][j] and nav[i][nj]):
                        continue
                    nd = d + w * CELL
                    if nd < dist[ni][nj]:
                        dist[ni][nj] = nd
                        parent[(ni, nj)] = (i, j)
                        heapq.heappush(heap, (nd, ni, nj))
        return dist, parent

    def line_clear(self, a, b, closed):
        (ax, ay), (bx, by) = a, b
        n = max(1, int(math.hypot(bx - ax, by - ay) // (CELL / 2)))
        for k in range(n + 1):
            x, y = ax + (bx - ax) * k / n, ay + (by - ay) * k / n
            i, j = self.cell(x, y)
            if not (self.in_range(i, j) and self.nav[i][j]) or (i, j) in closed:
                return False
        return True

    def path(self, parent, source, target, closed=None):
        """Cell chain from source to target, string-pulled into world points."""
        closed = closed or set()
        chain = [target]
        while chain[-1] != source:
            chain.append(parent[chain[-1]])
        chain.reverse()
        pts = [self.centre(*c) for c in chain]
        out = [pts[0]]
        anchor = 0
        while anchor < len(pts) - 1:
            far = anchor + 1
            for k in range(len(pts) - 1, anchor, -1):
                if self.line_clear(pts[anchor], pts[k], closed):
                    far = k
                    break
            out.append(pts[far])
            anchor = far
        return out


def path_length(pts):
    return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(pts, pts[1:]))


# ----------------------------------------------------------------------------- analysis
class Analysis:
    def __init__(self, data):
        self.data = data
        self.errors = []
        self.notes = []
        self.const = data["constants"]
        self.speed = float(self.const["unit_speed_cm_s"])
        self.grid = Grid(data)
        self.hq = {h["id"]: h for h in data["headquarters"]}
        self.ramps = {r["id"]: r for r in data["ramps"]}
        self.regions = {r["id"]: r for r in data["regions"]}
        self.z = {lv["id"]: lv for lv in data["elevation"]}

    def err(self, message):
        self.errors.append(message)

    # ---- structure
    def check_levels(self):
        g = self.grid
        overlap = 0
        thin = 0
        for i in range(g.nx):
            for j in range(g.ny):
                bits = g.cover[i][j]
                if bits & (bits - 1) and g.ramp[i][j] < 0:
                    overlap += 1
                if bits and bits & (bits - 1) == 0 and g.ramp[i][j] < 0:
                    for a in range(-3, 4):
                        for b in range(-3, 4):
                            if g.in_range(i + a, j + b) and g.ramp[i + a][j + b] < 0:
                                other = g.cover[i + a][j + b]
                                if other and other != bits and not other & bits:
                                    thin += 1
        if overlap:
            self.err("regions of different levels overlap on %d cells (outside ramps)" % overlap)
        if thin:
            self.err("cliff band under 300 cm between two levels on %d cell pairs" % thin)

    def check_ramps(self):
        g = self.grid
        limit = 12.0
        for ramp in self.data["ramps"]:
            rid = ramp["id"]
            for end, level in (("top", ramp["from_level"]), ("foot", ramp["to_level"])):
                x, y = ramp[end]
                i, j = g.cell(x, y)
                # The end point sits on the level's boundary; look one cell inward.
                ok = any(g.level_of(i + a, j + b) == level for a in (-1, 0, 1) for b in (-1, 0, 1) if g.in_range(i + a, j + b))
                if not ok:
                    self.err("%s: %s point is not on level %s" % (rid, end, level))
            for name in ("z_cm", "z_sc2_cm"):
                rise = abs(self.z[ramp["from_level"]][name] - self.z[ramp["to_level"]][name])
                slope = math.degrees(math.atan2(rise, ramp["length"]))
                ramp["slope_" + name] = slope
                if name == "z_cm" and slope > limit:
                    self.err("%s: slope %.1f deg over %.0f" % (rid, slope, limit))
            if ramp["width"] < 3 * self.const["force_width"]:
                self.err("%s: width %d is under three force widths" % (rid, ramp["width"]))

    def territory_discs(self):
        discs = []
        for h in self.data["headquarters"]:
            discs.append((h["id"], h["pos"], self.const["hq_territory_radius"], h["side"]))
        for s in self.data["sectors"]:
            discs.append(("S%d" % (s["site_index"] + 1), s["pos"], self.const["sector_territory_radius"], s["side"]))
        return discs

    def check_wall_off(self):
        """No ramp or named choke may sit inside a territory disc: buildings cannot plug it."""
        self.margins = []
        for name, pos, radius, _side in self.territory_discs():
            for ramp in self.data["ramps"]:
                d = poly_dist(pos[0], pos[1], ramp["poly"])
                self.margins.append((ramp["id"], name, d - radius))
                if d - radius < 0:
                    self.err("%s lies inside the %.0f territory of %s (%.0f cm)" % (ramp["id"], radius, name, d - radius))
            for choke in self.data["chokes"]:
                half = choke["width"] / 2.0
                a = (choke["center"][0] - choke["across"][0] * half, choke["center"][1] - choke["across"][1] * half)
                b = (choke["center"][0] + choke["across"][0] * half, choke["center"][1] + choke["across"][1] * half)
                d = seg_dist(pos[0], pos[1], a, b)
                self.margins.append((choke["id"], name, d - radius))
                if d - radius < 0:
                    self.err("%s lies inside the territory of %s" % (choke["id"], name))

    def check_sectors(self):
        g = self.grid
        cap = self.const["capture_radius"]
        terr = self.const["sector_territory_radius"]
        self.sector_stats = {}
        for s in self.data["sectors"]:
            x, y = s["pos"]
            ci, cj = g.cell(x, y)
            level = g.level_of(ci, cj)
            if level != s["level"]:
                self.err("sector %s is on %s, not %s" % (s["name"], level, s["level"]))
                continue
            bad = 0
            total = 0
            usable = 0
            for i, j in g.cells_in([[x - terr, y - terr], [x + terr, y - terr], [x + terr, y + terr], [x - terr, y + terr]]):
                cx, cy = g.centre(i, j)
                d = math.hypot(cx - x, cy - y)
                if d <= terr:
                    total += 1
                    same = g.walk[i][j] and g.level_of(i, j) == level and g.ramp[i][j] < 0
                    if same:
                        usable += 1
                    if d <= cap and not same:
                        bad += 1
            if bad:
                self.err("sector %s: capture ring leaves its level (%d cells)" % (s["name"], bad))
            self.sector_stats[s["id"]] = usable / float(total)
        for h in self.data["headquarters"]:
            x, y = h["pos"]
            radius = self.const["hq_territory_radius"]
            total = usable = 0
            for i, j in g.cells_in([[x - radius, y - radius], [x + radius, y - radius], [x + radius, y + radius], [x - radius, y + radius]]):
                cx, cy = g.centre(i, j)
                if math.hypot(cx - x, cy - y) <= radius:
                    total += 1
                    if g.walk[i][j] and g.level_of(i, j) == h["level"] and g.ramp[i][j] < 0:
                        usable += 1
            self.sector_stats[h["id"]] = usable / float(total)
            if usable != total:
                self.err("%s territory disc leaves its plateau (%d/%d cells)" % (h["id"], usable, total))

    def check_chokes(self):
        """A choke's width is twice the clearance (distance to the nearest non-walkable cell) at its centre."""
        g = self.grid
        edge = g.edge_distance()
        self.edge = edge
        self.choke_widths = {}
        for c in self.data["chokes"]:
            i, j = g.cell(*c["center"])
            measured = 2.0 * edge[i][j]
            self.choke_widths[c["id"]] = measured
            if abs(measured - c["width"]) > 150:
                self.err("choke %s measures about %.0f cm across, file says %d" % (c["id"], measured, c["width"]))

    def check_symmetry(self):
        g = self.grid
        bad = 0
        for i in range(g.nx):
            for j in range(g.ny):
                if g.walk[i][j] != g.walk[g.nx - 1 - i][g.ny - 1 - j] or g.level_of(i, j) != g.level_of(g.nx - 1 - i, g.ny - 1 - j):
                    bad += 1
        self.asymmetric_cells = bad
        if bad:
            self.err("layout is not rot180 symmetric (%d cells)" % bad)
        for s in self.data["sectors"]:
            partner = next(t for t in self.data["sectors"] if t["site_index"] == s["pair"])
            if [-s["pos"][0], -s["pos"][1]] != partner["pos"] or partner["level"] != s["level"]:
                self.err("sector %s and %s are not rot180 partners" % (s["name"], partner["name"]))

    # ---- routes
    def closed_cells(self, ramp_ids):
        g = self.grid
        cells = set()
        for rid in ramp_ids:
            cells.update(g.cells_in(self.ramps[rid]["poly"]))
        return cells

    def measure(self):
        g = self.grid
        sources = {}
        for h in self.data["headquarters"]:
            sources[h["id"]] = g.nearest_nav(*h["pos"])
        self.sources = sources
        self.dist = {}
        self.parent = {}
        for key, src in sources.items():
            self.dist[key], self.parent[key] = g.dijkstra(src)
        self.targets = {}
        for h in self.data["headquarters"]:
            self.targets[h["id"]] = sources[h["id"]]
        for s in self.data["sectors"]:
            self.targets["S%d" % (s["site_index"] + 1)] = g.nearest_nav(*s["pos"])
        self.table = {}
        for key in ("HQ_H", "HQ_J"):
            row = {}
            for name, cell in self.targets.items():
                d = self.dist[key][cell[0]][cell[1]]
                if d == float("inf"):
                    self.err("%s cannot reach %s" % (key, name))
                    continue
                pts = g.path(self.parent[key], sources[key], cell)
                row[name] = (path_length(pts), pts)
            self.table[key] = row
        # Routes from the Bunker to the Cluster with one human entrance closed at a time.
        self.routes = {}
        for label, closed_ids in (("open", []), ("no_door", ["ramp_door_H"]), ("no_gate", ["ramp_gate_H"]),
                                  ("no_gate_no_door", ["ramp_gate_H", "ramp_door_H"])):
            closed = self.closed_cells(closed_ids)
            dist, parent = g.dijkstra(sources["HQ_H"], closed)
            t = sources["HQ_J"]
            if dist[t[0]][t[1]] == float("inf"):
                self.routes[label] = None
                continue
            pts = g.path(parent, sources["HQ_H"], t, closed)
            self.routes[label] = (path_length(pts), pts)
        # The optional breach, opened: the route that goes through the Slab's centre.
        if "proposals" in self.data and "breach" in self.data["proposals"]:
            g2 = Grid(self.data, breach_open=True)
            a2, b2, m2 = (g2.nearest_nav(*self.hq["HQ_H"]["pos"]), g2.nearest_nav(*self.hq["HQ_J"]["pos"]), g2.nearest_nav(0, 0))
            _, par_a = g2.dijkstra(a2)
            _, par_b = g2.dijkstra(b2)
            first = g2.path(par_a, a2, m2)
            second = g2.path(par_b, b2, m2)
            self.routes["breach_open"] = (path_length(first) + path_length(second), first + second[::-1])
        if not self.routes["no_door"] or not self.routes["no_gate"]:
            self.err("closing one human entrance disconnects the bases")
            return
        a, b = self.routes["no_door"][0], self.routes["no_gate"][0]
        self.route_gap = abs(a - b) / min(a, b)
        # Two attack routes must both exist and be within 3 % of each other.
        if self.route_gap > 0.03:
            self.err("attack routes differ by %.1f %%" % (100 * self.route_gap))
        if self.routes["no_gate_no_door"] is not None:
            self.err("the human terrace is reachable without the gate and the door")

    # ---- placement (2D rules of ValidateBuildingPlacement)
    def placement_ok(self, x, y, radius, home, hostile, zone, zone_radius, placed=()):
        """The 2D checks of ACommandGameState::ValidateBuildingPlacement for a building centred at (x, y).

        home / hostile are the placing team's and the other team's HQ positions; (zone, zone_radius) is the HQ or
        sector territory being built in; `placed` lists (x, y, radius) of existing buildings. The 3D checks
        (collision overlap, nav projection within 110 cm) become: every sample point is walkable ground of the same
        level and at least 60 cm from an edge, and never on a bare ramp.
        """
        g = self.grid
        hx, hy = self.data["arena"]["half_extent"]
        margin = self.data["arena"]["placement_margin"]
        if abs(x) > hx - margin or abs(y) > hy - margin:
            return False
        if math.hypot(x - hostile[0], y - hostile[1]) <= self.const["hq_exclusion_radius"] + radius:
            return False
        if math.hypot(x - zone[0], y - zone[1]) > zone_radius - radius:
            return False
        if math.hypot(x - home[0], y - home[1]) <= radius + 210 or math.hypot(x - hostile[0], y - hostile[1]) <= radius + 210:
            return False
        for bx, by, br in placed:
            if math.hypot(x - bx, y - by) <= radius + br + 55:
                return False
        i, j = g.cell(x, y)
        if not g.in_range(i, j):
            return False
        level = g.level_of(i, j)
        for a, b in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (.707, .707), (-.707, .707), (.707, -.707), (-.707, -.707)):
            si, sj = g.cell(x + a * (radius + 65), y + b * (radius + 65))
            if not (g.in_range(si, sj) and g.walk[si][sj] and not (g.ramp[si][sj] >= 0 and not g.cover[si][sj])
                    and g.level_of(si, sj) == level) or self.edge[si][sj] < 60:
                return False
        return True

    def placement(self):
        g = self.grid
        friendly = self.hq["HQ_H"]["pos"]
        enemy = self.hq["HQ_J"]["pos"]
        hq_r = self.const["hq_territory_radius"]
        sec_r = self.const["sector_territory_radius"]
        out = {}
        for kind, radius in self.const["footprint_radius"].items():
            if kind == "outpost":
                continue
            for zone, centre, zr in [("HQ_H", friendly, hq_r)] + [("S%d" % (s["site_index"] + 1), s["pos"], sec_r) for s in self.data["sectors"] if s["side"] == "H"]:
                cells = []
                for i, j in g.cells_in([[centre[0] - zr, centre[1] - zr], [centre[0] + zr, centre[1] - zr],
                                        [centre[0] + zr, centre[1] + zr], [centre[0] - zr, centre[1] + zr]]):
                    x, y = g.centre(i, j)
                    if self.placement_ok(x, y, radius, friendly, enemy, centre, zr):
                        cells.append((x, y))
                out[(kind, zone)] = cells
        self.placements = out
        # Greedy packing: hard rule spacing (footprints + 55) and a practical spacing that keeps an exit lane open.
        self.packing = {}
        for (kind, zone), cells in out.items():
            radius = self.const["footprint_radius"][kind]
            for label, spacing in (("hard", 2 * radius + 55), ("practical", 2 * radius + 300)):
                chosen = []
                for x, y in sorted(cells, key=lambda c: (round(c[0] / 50), c[1])):
                    if all(math.hypot(x - a, y - b) >= spacing for a, b in chosen):
                        chosen.append((x, y))
                self.packing[(kind, zone, label)] = chosen
        # Pocket halves of the Bunker disc.
        self.pockets = {}
        for pocket in self.data["build_pockets"]:
            px, py = pocket["half_plane"]["point"]
            kx, ky = pocket["half_plane"]["keep"]
            for kind in ("barracks", "workshop"):
                cells = [c for c in out[(kind, "HQ_H")] if (c[0] - px) * kx + (c[1] - py) * ky > 0]
                radius = self.const["footprint_radius"][kind]
                chosen = []
                for x, y in sorted(cells, key=lambda c: (round(c[0] / 50), c[1])):
                    if all(math.hypot(x - a, y - b) >= 2 * radius + 300 for a, b in chosen):
                        chosen.append((x, y))
                self.pockets[(pocket["id"], kind)] = (len(cells), len(chosen))

    def check_bays(self):
        """Authored bays must pass the placement rules, sit in their pocket half and leave every neighbour 450+ cm."""
        friendly = self.hq["HQ_H"]["pos"]
        enemy = self.hq["HQ_J"]["pos"]
        radius = self.const["footprint_radius"]["barracks"]
        allbays = []
        for pocket in self.data["build_pockets"]:
            px, py = pocket["half_plane"]["point"]
            kx, ky = pocket["half_plane"]["keep"]
            for bay in pocket.get("bays", []):
                x, y = bay["pos"]
                if not self.placement_ok(x, y, radius, friendly, enemy, friendly, self.const["hq_territory_radius"]):
                    self.err("bay %s fails the placement rules" % bay["id"])
                if (x - px) * kx + (y - py) * ky <= 0:
                    self.err("bay %s is in the wrong pocket half" % bay["id"])
                allbays.append(bay)
        self.bay_min_gap = min([math.hypot(a["pos"][0] - b["pos"][0], a["pos"][1] - b["pos"][1])
                                for k, a in enumerate(allbays) for b in allbays[k + 1:]] or [0])
        if self.bay_min_gap < 450:
            self.err("two bays are only %.0f cm apart" % self.bay_min_gap)

    def jev_build_check(self):
        """Replay AEnemyCommander::BuildNear: 4 rings (360 + 150 k) x 12 directions around the Cluster, then outposts."""
        enemy = self.hq["HQ_J"]["pos"]
        friendly = self.hq["HQ_H"]["pos"]
        r0, r1 = self.const["jev_ring"]
        rings = [360 + 150 * k for k in range(4)]
        fp = self.const["footprint_radius"]
        placed = []
        self.jev_builds = []
        for kind in ("barracks", "barracks", "barracks", "workshop"):
            spot = None
            for ring in rings:
                for d in range(12):
                    ang = d * math.pi / 6
                    x, y = enemy[0] + math.cos(ang) * ring, enemy[1] + math.sin(ang) * ring
                    if self.placement_ok(x, y, fp[kind], enemy, friendly, enemy, self.const["hq_territory_radius"], placed):
                        spot = (x, y)
                        break
                if spot:
                    break
            self.jev_builds.append((kind, spot))
            if spot:
                placed.append((spot[0], spot[1], fp[kind]))
        for kind, spot in self.jev_builds:
            if spot is None:
                self.err("JEV cannot place its %s inside the Cluster ring (BuildNear candidates)" % kind)
        # Outposts: any sector, first valid ring candidate.
        self.jev_outposts = {}
        for s in self.data["sectors"]:
            spot = None
            for ring in rings:
                for d in range(12):
                    ang = d * math.pi / 6
                    x, y = s["pos"][0] + math.cos(ang) * ring, s["pos"][1] + math.sin(ang) * ring
                    if self.placement_ok(x, y, fp["outpost"], enemy, friendly, s["pos"], self.const["sector_territory_radius"]):
                        spot = (x, y)
                        break
                if spot:
                    break
            self.jev_outposts["S%d" % (s["site_index"] + 1)] = spot
            if spot is None:
                self.err("JEV cannot place an outpost for %s" % s["name"])

    def check_elevation(self):
        """Buildable levels against ACommandGameState::ValidateBuildingPlacement's height rules.

        1. The overlap box spans Location.Z + 10 .. + 120 (centre +65, half height 55), so a surface higher than
           Location.Z + 10 blocks the footprint; Location.Z is 0 for a click (CursorGround) and 5 for JEV (BuildNear).
        2. Nav samples must project within 110 cm of Location.Z.
        Margins: 5 cm on the first, 15 cm on the second (navmesh cell height is about 10 cm).
        """
        tol = self.const["placement_z_tolerance"]
        box = self.const["placement_overlap_box"]
        top = box["centre_z_offset"] - box["half_z"]
        for lv in self.data["elevation"]:
            for ref_name in ("click_plane_z", "jev_build_z"):
                ref = self.const[ref_name]
                lv["gap_" + ref_name] = abs(lv["z_cm"] - ref)
                if lv["z_cm"] > ref + top - 5:
                    self.err("level %s at %+d cm rises into the placement overlap box (reference z %d)" % (lv["id"], lv["z_cm"], ref))
                if abs(lv["z_cm"] - ref) > tol - 15:
                    self.err("level %s at %+d cm is more than %d cm from z %d" % (lv["id"], lv["z_cm"], tol - 15, ref))
        step = self.const["character_max_step_height"]
        for ramp in self.data["ramps"]:
            rise = abs(self.z[ramp["from_level"]]["z_cm"] - self.z[ramp["to_level"]]["z_cm"])
            if 0 < rise <= step:
                self.err("%s: a %d cm step is within the character's %d cm step height" % (ramp["id"], rise, step))

    def intruder_reach(self):
        """Distance from the Cluster to its terrace lip (JEV switches to Defend when humans stand within 1500)."""
        hq = self.hq["HQ_J"]["pos"]
        best = min(poly_dist(hq[0], hq[1], r["poly"]) for r in self.data["regions"] if r["side"] == "J" and r["level"] == "L1")
        self.intruder_edge = best
        if best > self.const["jev_intruder_radius"]:
            self.err("terrace lip is %.0f cm from the Cluster; JEV's 1500 intruder radius does not reach it" % best)

    def run(self):
        self.check_levels()
        self.check_ramps()
        self.check_wall_off()
        self.check_sectors()
        self.check_chokes()
        self.check_symmetry()
        self.measure()
        self.edge = self.grid.edge_distance()
        self.check_elevation()
        self.placement()
        self.check_bays()
        self.jev_build_check()
        self.intruder_reach()
        return not self.errors

    # ---- text report
    def secs(self, cm):
        return cm / self.speed

    def report(self):
        d = self.data
        lines = []
        add = lines.append
        hx, hy = d["arena"]["half_extent"]
        add("Arena %d x %d cm (%.0f x %.0f m), unit speed %.0f cm/s" % (2 * hx, 2 * hy, 2 * hx / 100, 2 * hy / 100, self.speed))
        add("Symmetry mismatch cells: %d" % self.asymmetric_cells)
        add("")
        add("Walking distance from each Headquarters (cm, seconds at unit speed)")
        names = {s["id"]: s for s in d["sectors"]}
        names_by_key = {"S%d" % (s["site_index"] + 1): s for s in d["sectors"]}
        add("%-4s %-16s %-16s %14s %14s" % ("", "sector", "role", "from Bunker", "from Cluster"))
        for key, s in names_by_key.items():
            a = self.table["HQ_H"].get(key)
            b = self.table["HQ_J"].get(key)
            add("%-4s %-16s %-16s %7.0f %5.1fs %7.0f %5.1fs" % (key, s["name"], s["role"], a[0], self.secs(a[0]), b[0], self.secs(b[0])))
        a = self.table["HQ_H"]["HQ_J"]
        add("HQ to HQ: %.0f cm = %.1f s (open)" % (a[0], self.secs(a[0])))
        add("")
        add("JEV target score, AEnemyCommander::EvaluatePlan: (5 neutral or Machine-held, 3 human-held) - straight 2D distance / 1200")
        jev = self.hq["HQ_J"]["pos"]
        ranked = []
        for s in d["sectors"]:
            dist = math.hypot(s["pos"][0] - jev[0], s["pos"][1] - jev[1])
            ranked.append((5.0 - dist / 1200.0, 3.0 - dist / 1200.0, dist, s))
        for neutral, human, dist, s in sorted(ranked, key=lambda r: (-r[0], r[3]["site_index"])):
            add("  S%d %-16s straight %6.0f cm   neutral %6.2f   human-held %6.2f" % (s["site_index"] + 1, s["name"], dist, neutral, human))
        add("")
        add("Attack routes (Bunker to Cluster, one human entrance closed)")
        for label in ("open", "no_door", "no_gate", "breach_open"):
            r = self.routes.get(label)
            if r:
                add("  %-12s %7.0f cm  %5.1f s" % (label, r[0], self.secs(r[0])))
        add("  route gap %.2f %% (breach_open = through the opened Slab centre; a proposal)" % (100 * self.route_gap))
        add("")
        add("Chokes")
        for c in d["chokes"]:
            add("  %-16s declared %5d  measured %5.0f" % (c["id"], c["width"], self.choke_widths[c["id"]]))
        add("")
        add("Ramps")
        for r in d["ramps"]:
            add("  %-14s %4d wide x %4d long  slope %.1f deg (phase A)  %.1f deg (SC2 heights)"
                % (r["id"], r["width"], r["length"], r["slope_z_cm"], r["slope_z_sc2_cm"]))
        add("")
        add("Tightest ramp/choke clearance from any territory disc (cm beyond the disc edge)")
        worst = sorted(self.margins, key=lambda m: m[2])[:6]
        for what, disc, m in worst:
            add("  %-16s %-6s %6.0f" % (what, disc, m))
        add("Terrace edge to Cluster: %.0f cm (JEV intruder radius %d)" % (self.intruder_edge, self.const["jev_intruder_radius"]))
        add("")
        add("Placement coverage in 100 cm cells (1 cell = 1 m2) and greedy packing (hard / practical)")
        for zone in ["HQ_H"] + ["S%d" % (s["site_index"] + 1) for s in d["sectors"] if s["side"] == "H"]:
            row = []
            for kind in ("barracks", "workshop"):
                row.append("%s %4d m2  pack %2d / %2d" % (kind, len(self.placements[(kind, zone)]),
                                                        len(self.packing[(kind, zone, "hard")]), len(self.packing[(kind, zone, "practical")])))
            add("  %-5s %s" % (zone, "   ".join(row)))
        for (pid, kind), (cells, packed) in sorted(self.pockets.items()):
            add("  %-11s %-8s %4d m2  practical pack %d" % (pid, kind, cells, packed))
        add("")
        add("Build bays authored: %d, closest pair %.0f cm" % (sum(len(p.get("bays", [])) for p in d["build_pockets"]), self.bay_min_gap))
        add("JEV BuildNear replay: %s" % ", ".join("%s %s" % (k, "ok" if sp else "FAIL") for k, sp in self.jev_builds))
        add("JEV outpost candidates: %s" % ", ".join("%s %s" % (k, "ok" if v else "FAIL") for k, v in self.jev_outposts.items()))
        add("Level height gaps to the click plane (z 0) / JEV build height (z 5), cm: %s" % ", ".join(
            "%s %d/%d" % (lv["id"], lv["gap_click_plane_z"], lv["gap_jev_build_z"]) for lv in d["elevation"]))
        add("")
        add("Errors: %s" % ("none" if not self.errors else ""))
        for e in self.errors:
            add("  - " + e)
        return "\n".join(lines)


# ----------------------------------------------------------------------------- drawing
FONT = "DejaVu Sans"
LEVEL_FILL = {"L0": "#33475c", "L1": "#557391", "L2": "#93b0cc"}
CLIFF = "#090b0e"
HUMAN = "#3aa0ff"
MACHINE = "#ff5252"
NEUTRAL = "#f2c14e"
ROUTE_COLORS = {"no_door": "#ff9f1c", "no_gate": "#f051b5"}


def esc(text):
    return str(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Svg:
    def __init__(self, data, an):
        self.d = data
        self.a = an
        self.hx, self.hy = data["arena"]["half_extent"]
        self.map_px = 1400.0
        self.s = self.map_px / (2 * max(self.hx, self.hy))
        self.ml, self.mt = 120.0, 120.0
        self.panel_x = self.ml + 2 * self.hy * self.s + 50.0
        self.width = self.panel_x + 760.0
        self.height = self.mt + 2 * self.hx * self.s + 140.0
        self.out = []

    def P(self, x, y):
        return self.ml + (y + self.hy) * self.s, self.mt + (self.hx - x) * self.s

    def pts(self, poly):
        return " ".join("%.1f,%.1f" % self.P(x, y) for x, y in poly)

    def add(self, text):
        self.out.append(text)

    def text(self, x, y, text, size=16, fill="#e8eef5", anchor="start", weight="normal", italic=False, opacity=1.0, halo=True):
        style = ' font-style="italic"' if italic else ""
        halo_attr = ' stroke="#0a0d12" stroke-width="%.1f" stroke-linejoin="round" paint-order="stroke"' % (size / 5.0) if halo else ""
        self.add('<text x="%.1f" y="%.1f" font-family="%s" font-size="%d" fill="%s" text-anchor="%s" font-weight="%s"%s opacity="%.2f"%s>%s</text>'
                 % (x, y, FONT, size, fill, anchor, weight, style, opacity, halo_attr, esc(text)))

    def world_text(self, x, y, text, **kw):
        px, py = self.P(x, y)
        self.text(px, py, text, **kw)

    def draw(self):
        d, a = self.d, self.a
        s = self.s
        add = self.add
        add('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">' % (self.width, self.height, self.width, self.height))
        add('<defs>'
            '<pattern id="rim" width="12" height="12" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
            '<rect width="12" height="12" fill="#14171c"/><line x1="0" y1="0" x2="0" y2="12" stroke="#1e232b" stroke-width="4"/></pattern>'
            '<pattern id="slab" width="14" height="14" patternUnits="userSpaceOnUse" patternTransform="rotate(-45)">'
            '<rect width="14" height="14" fill="#1b2430"/><line x1="0" y1="0" x2="0" y2="14" stroke="#2c3b4d" stroke-width="5"/></pattern>'
            '<pattern id="plug" width="10" height="10" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
            '<rect width="10" height="10" fill="#3a2a1a"/><line x1="0" y1="0" x2="0" y2="10" stroke="#a5713a" stroke-width="3"/></pattern>'
            '<marker id="arrow" viewBox="0 0 10 10" refX="7" refY="5" markerWidth="4" markerHeight="4" orient="auto-start-reverse">'
            '<path d="M0,0 L10,5 L0,10 z" fill="context-stroke"/></marker>')
        for ramp in d["ramps"]:
            t = self.P(*ramp["top"])
            f = self.P(*ramp["foot"])
            add('<linearGradient id="g_%s" gradientUnits="userSpaceOnUse" x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f">'
                '<stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>'
                % (ramp["id"], t[0], t[1], f[0], f[1], LEVEL_FILL[ramp["from_level"]], LEVEL_FILL[ramp["to_level"]]))
        add('</defs>')
        add('<rect width="%d" height="%d" fill="#0d1014"/>' % (self.width, self.height))
        x0, y0 = self.P(self.hx, -self.hy)
        add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="url(#rim)" stroke="#39414d" stroke-width="3"/>'
            % (x0, y0, 2 * self.hy * s, 2 * self.hx * s))
        # Cliff shadows first (a wide dark stroke centred on each high region's edge), then the fills.
        order = {lv["id"]: k for k, lv in enumerate(d["elevation"])}
        regions = sorted(d["regions"], key=lambda r: order[r["level"]])
        for r in regions:
            if order[r["level"]] > 0:
                add('<polygon points="%s" fill="%s" stroke="%s" stroke-width="%.1f" stroke-linejoin="round" opacity="0.92"/>'
                    % (self.pts(r["poly"]), CLIFF, CLIFF, 2 * 300 * s * 1.0))
        for r in regions:
            add('<polygon points="%s" fill="%s" stroke="#0b0e12" stroke-width="2" stroke-linejoin="round"/>' % (self.pts(r["poly"]), LEVEL_FILL[r["level"]]))
        # HQ territory pockets
        for h in d["headquarters"]:
            if h["side"] != "H":
                continue
            cx, cy = self.P(*h["pos"])
            r = self.const_r("hq_territory_radius") * s
            for k, pocket in enumerate(d["build_pockets"]):
                keep = pocket["half_plane"]["keep"]
                # Half disc on the +/-Y side of the HQ. Screen right is +Y.
                sign = keep[1]
                add('<path d="M %.1f %.1f A %.1f %.1f 0 0 %d %.1f %.1f Z" fill="%s" opacity="0.28"/>'
                    % (cx, cy - r, r, r, 1 if sign > 0 else 0, cx, cy + r, "#7fd0ff" if k == 0 else "#ffd27f"))
        for k, pocket in enumerate(d["build_pockets"]):
            for bay in pocket.get("bays", []):
                bx, by = self.P(*bay["pos"])
                half = 125 * s
                add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="none" stroke="%s" stroke-width="1.6" stroke-dasharray="3 2"/>' % (bx - half, by - half, 2 * half, 2 * half, "#ffffff"))
                self.text(bx, by + 4, bay["id"], 10, "#ffffff", "middle", halo=False)
        # Slab and proposals
        for b in d["blockers"]:
            add('<polygon points="%s" fill="url(#slab)" stroke="#67809b" stroke-width="3"/>' % self.pts(b["poly"]))
        br = d["proposals"]["breach"]
        add('<polygon points="%s" fill="none" stroke="#a5713a" stroke-width="2" stroke-dasharray="8 6"/>' % self.pts(br["poly"]))
        for plug in br["plugs"]:
            add('<polygon points="%s" fill="url(#plug)" stroke="#d99a5b" stroke-width="2" stroke-dasharray="6 4" opacity="0.9"/>' % self.pts(plug["poly"]))
        # Grid
        for k in range(-int(self.hx // 1000), int(self.hx // 1000) + 1):
            px0, py0 = self.P(k * 1000, -self.hy)
            px1, _ = self.P(k * 1000, self.hy)
            major = k % 5 == 0
            add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#ffffff" stroke-width="%s" opacity="%s"/>'
                % (px0, py0, px1, py0, 1.4 if major else 0.7, 0.22 if major else 0.09))
            if k % 2 == 0:
                self.text(self.ml - 8, py0 + 5, "%+d" % (k * 10), 14, "#9aa7b6", "end", halo=False)
        for k in range(-int(self.hy // 1000), int(self.hy // 1000) + 1):
            px0, py0 = self.P(self.hx, k * 1000)
            _, py1 = self.P(-self.hx, k * 1000)
            major = k % 5 == 0
            add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#ffffff" stroke-width="%s" opacity="%s"/>'
                % (px0, py0, px0, py1, 1.4 if major else 0.7, 0.22 if major else 0.09))
            if k % 2 == 0:
                self.text(px0, py1 + 20, "%+d" % (k * 10), 14, "#9aa7b6", "middle", halo=False)
        self.text(12, self.mt - 22, "X (m)  N = +X, up", 14, "#9aa7b6", "start", halo=False)
        self.text(self.ml + 2 * self.hy * s, self.mt + 2 * self.hx * s + 42, "Y (m)  E = +Y, right", 14, "#9aa7b6", "end", halo=False)
        # Ramps
        for r in d["ramps"]:
            add('<polygon points="%s" fill="url(#g_%s)" stroke="#e8eef5" stroke-width="2"/>' % (self.pts(r["poly"]), r["id"]))
            tx, ty = r["top"]
            fx, fy = r["foot"]
            l = math.hypot(fx - tx, fy - ty)
            ux, uy = (fx - tx) / l, (fy - ty) / l
            nx, ny = -uy, ux
            h = r["width"] / 2.0
            steps = int(l // 100)
            for k in range(1, steps):
                px = tx + ux * k * 100
                py = ty + uy * k * 100
                a_ = self.P(px + nx * h, py + ny * h)
                b_ = self.P(px - nx * h, py - ny * h)
                add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#0b0e12" stroke-width="1" opacity="0.45"/>' % (a_[0], a_[1], b_[0], b_[1]))
            mid = ((tx + fx) / 2, (ty + fy) / 2)
            up0 = self.P(mid[0] + ux * 250, mid[1] + uy * 250)
            up1 = self.P(mid[0] - ux * 250, mid[1] - uy * 250)
            flat = abs(self.a.z[r["from_level"]]["z_cm"] - self.a.z[r["to_level"]]["z_cm"]) == 0
            add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#ffffff" stroke-width="3.5" marker-end="url(#arrow)"%s opacity="0.95"/>'
                % (up0[0], up0[1], up1[0], up1[1], ' marker-start="url(#arrow)"' if flat else ""))
        # Territory rings and markers
        cap = self.const_r("capture_radius") * s
        terr = self.const_r("sector_territory_radius") * s
        for sec in d["sectors"]:
            cx, cy = self.P(*sec["pos"])
            col = HUMAN if sec["side"] == "H" else MACHINE
            add('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="none" stroke="%s" stroke-width="1.6" stroke-dasharray="7 6" opacity="0.8"/>' % (cx, cy, terr, col))
            add('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" fill-opacity="0.22" stroke="%s" stroke-width="3"/>' % (cx, cy, cap, NEUTRAL, NEUTRAL))
            add('<circle cx="%.1f" cy="%.1f" r="14" fill="%s" stroke="#0a0d12" stroke-width="2"/>' % (cx, cy, col))
            self.text(cx, cy + 5, str(sec["site_index"] + 1), 16, "#ffffff", "middle", "bold", halo=False)
            self.text(cx, cy - cap - 10, sec["name"], 17, "#ffffff", "middle", "bold")
            self.text(cx, cy + cap + 20, sec["role"].replace("_", " "), 14, "#dfe7f1", "middle", italic=True)
        for h in d["headquarters"]:
            cx, cy = self.P(*h["pos"])
            col = HUMAN if h["side"] == "H" else MACHINE
            half = 150 * s
            add('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s" stroke="#ffffff" stroke-width="3"/>' % (cx - half * 1.3, cy - half * 1.3, half * 2.6, half * 2.6, col))
            self.text(cx, cy + 6, "HQ", 15, "#ffffff", "middle", "bold", halo=False)
            self.text(cx, cy + half * 1.3 + 24, h["name"], 18, "#ffffff", "middle", "bold")
            if h["side"] == "H":
                r = self.const_r("hq_territory_radius") * s
                add('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="none" stroke="%s" stroke-width="1.6" stroke-dasharray="7 6"/>' % (cx, cy, r, col))
            else:
                r = self.const_r("hq_territory_radius") * s
                add('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="none" stroke="%s" stroke-width="1.6" stroke-dasharray="7 6"/>' % (cx, cy, r, col))
        for prop in d["proposals"]["per_player_start"]:
            cx, cy = self.P(*prop["pos"])
            add('<rect x="%.1f" y="%.1f" width="14" height="14" fill="none" stroke="%s" stroke-width="2.5" stroke-dasharray="3 3"/>' % (cx - 7, cy - 7, HUMAN))
            add('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="none" stroke="%s" stroke-width="1.2" stroke-dasharray="2 5" opacity="0.8"/>' % (cx, cy, self.const_r("hq_territory_radius") * s, HUMAN))
        # Routes
        for label, col in ROUTE_COLORS.items():
            r = self.a.routes.get(label)
            if not r:
                continue
            pts = " ".join("%.1f,%.1f" % self.P(x, y) for x, y in r[1])
            add('<polyline points="%s" fill="none" stroke="#0a0d12" stroke-width="10" stroke-linejoin="round" stroke-linecap="round" opacity="0.6"/>' % pts)
            add('<polyline points="%s" fill="none" stroke="%s" stroke-width="5.5" stroke-linejoin="round" stroke-linecap="round" marker-mid="none" opacity="0.95"/>' % (pts, col))
            n = len(r[1])
            # Direction chevrons every ~1000 cm of path.
            total = 0.0
            nextmark = 700.0
            for (ax_, ay_), (bx_, by_) in zip(r[1], r[1][1:]):
                seg = math.hypot(bx_ - ax_, by_ - ay_)
                while total + seg >= nextmark:
                    t = (nextmark - total) / seg
                    mx, my = ax_ + (bx_ - ax_) * t, ay_ + (by_ - ay_) * t
                    p0 = self.P(mx, my)
                    p1 = self.P(mx + (bx_ - ax_) / seg * 40, my + (by_ - ay_) / seg * 40)
                    add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#0a0d12" stroke-width="5" marker-end="url(#arrow)"/>' % (p0[0], p0[1], p1[0], p1[1]))
                    nextmark += 1200.0
                total += seg
        # JEV expansion walks (Cluster to its two nearest sectors)
        for key in ("S7", "S8"):
            row = self.a.table["HQ_J"].get(key)
            if row:
                pts = " ".join("%.1f,%.1f" % self.P(x, y) for x, y in row[1])
                add('<polyline points="%s" fill="none" stroke="#7df9ff" stroke-width="3.5" stroke-dasharray="4 7" stroke-linecap="round" opacity="0.95"/>' % pts)
        # Chokes
        for c in d["chokes"]:
            half = c["width"] / 2.0
            a_ = self.P(c["center"][0] - c["across"][0] * half, c["center"][1] - c["across"][1] * half)
            b_ = self.P(c["center"][0] + c["across"][0] * half, c["center"][1] + c["across"][1] * half)
            add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#ffffff" stroke-width="2" stroke-dasharray="3 3" opacity="0.9"/>' % (a_[0], a_[1], b_[0], b_[1]))
            for e in (a_, b_):
                add('<circle cx="%.1f" cy="%.1f" r="3.5" fill="#ffffff"/>' % e)
            mx, my = (a_[0] + b_[0]) / 2, (a_[1] + b_[1]) / 2
            side = 1 if c["center"][1] < 0 else -1
            offx, offy = (24 * side, 5) if c["across"][1] == 0 else (0, -12)
            anchor = ("start" if side > 0 else "end") if c["across"][1] == 0 else "middle"
            self.text(mx + offx, my + offy, "%s %d" % (c["name"], c["width"]), 14, "#ffffff", anchor, "bold")
        # Vision proposals
        for v in d["proposals"]["vision_points"]:
            cx, cy = self.P(*v["pos"])
            add('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="none" stroke="#b6f28a" stroke-width="1.2" stroke-dasharray="2 6" opacity="0.6"/>' % (cx, cy, v["radius"] * s))
            add('<polygon points="%.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f" fill="none" stroke="#b6f28a" stroke-width="3"/>' % (cx, cy - 12, cx + 12, cy, cx, cy + 12, cx - 12, cy))
        # Slab and region labels
        self.world_text(0, 0, "DATA HALL 0 · THE SLAB", size=20, anchor="middle", weight="bold", fill="#a9c1da")
        self.world_text(-300, 0, "full-depth dam · breach = proposal", size=14, anchor="middle", italic=True, fill="#8ea6bf")
        for r in d["regions"]:
            lv = self.a.z[r["level"]]
            top = r["name"].upper()
            sub = "%s %+d cm" % (r["level"], lv["z_cm"])
            sign = -1 if r["side"] == "H" else 1
            spots = {"pass_W": (1500, -5450), "pass_E": (-1500, 5450),
                     "terrace_H": (-3450, -7000), "terrace_J": (3450, 7000),
                     "main_H": (-6900, -5800), "main_J": (8250, 5450),
                     "pocket_SE": (-7600, 300), "pocket_NW": (7600, -300)}
            x, y = spots[r["id"]]
            dark = r["level"] == "L2"
            self.world_text(x, y, top, size=15, anchor="middle", weight="bold", fill="#0b0e12" if dark else "#e3ecf6", halo=not dark)
            self.world_text(x - 260, y, sub, size=14, anchor="middle", italic=True, fill="#0b0e12" if dark else "#c9d6e4", halo=not dark)
        self.world_text(1500 - 520, -5450, "open field 54 x 47 m", size=13, anchor="middle", italic=True, fill="#c2cfdd")
        self.world_text(-1500 + 520, 5450, "open field 54 x 47 m", size=13, anchor="middle", italic=True, fill="#c2cfdd")
        self.world_text(-9350, 0, "HUMAN FORWARD BASE (SOUTH)", size=22, anchor="middle", weight="bold", fill=HUMAN)
        self.world_text(9150, 0, "MACHINE CAMPUS (NORTH)", size=22, anchor="middle", weight="bold", fill=MACHINE)
        self.world_text(-9350, 5500, "rock rim · out of play", size=14, anchor="middle", italic=True, fill="#7d8896")
        self.world_text(9150, -5500, "rock rim · out of play", size=14, anchor="middle", italic=True, fill="#7d8896")
        self.scale_bar()
        self.title()
        self.panel()
        add('</svg>')
        return "\n".join(self.out)

    def const_r(self, key):
        return float(self.a.const[key])

    def scale_bar(self):
        s = self.s
        x = self.ml
        y = self.mt + 2 * self.hx * s + 78
        self.text(x, y - 14, "SCALE", 14, "#9aa7b6", halo=False)
        for k in range(5):
            col = "#e8eef5" if k % 2 == 0 else "#39414d"
            self.add('<rect x="%.1f" y="%.1f" width="%.1f" height="12" fill="%s" stroke="#e8eef5" stroke-width="1.5"/>' % (x + k * 1000 * s, y, 1000 * s, col))
        for k in range(0, 6):
            self.text(x + k * 1000 * s, y + 32, "%d m" % (k * 10), 14, "#c9d3df", "middle", halo=False)
        # Time bar: distance walked in 10 s at unit speed.
        span = self.a.speed * 10
        y2 = y
        x2 = x + 6300 * s
        self.text(x2, y2 - 14, "TRAVEL TIME  (%.0f cm/s)" % self.a.speed, 14, "#9aa7b6", halo=False)
        for k in range(4):
            col = "#ffd27f" if k % 2 == 0 else "#39414d"
            self.add('<rect x="%.1f" y="%.1f" width="%.1f" height="12" fill="%s" stroke="#ffd27f" stroke-width="1.5"/>' % (x2 + k * span * s, y2, span * s, col))
        for k in range(0, 5):
            self.text(x2 + k * span * s, y2 + 32, "%d s" % (k * 10), 14, "#ffe2ad", "middle", halo=False)

    def title(self):
        d = self.d
        self.text(self.ml, 52, d["title"].upper() + "  ·  1 human (or 2) vs JEV", 34, "#ffffff", weight="bold", halo=False)
        self.text(self.ml, 84, "Top-down layout plan · 200 x 200 m arena · rot180 symmetric · source Build/Maps/%s.json" % d["name"], 17, "#9aa7b6", halo=False)

    def panel(self):
        d, a = self.d, self.a
        x = self.panel_x
        y = self.mt - 10
        add = self.add
        self.text(x, y, "ELEVATION (phase A z / SC2-height z, cm)", 17, "#ffffff", weight="bold", halo=False)
        y += 12
        for lv in reversed(d["elevation"]):
            y += 34
            add('<rect x="%.1f" y="%.1f" width="46" height="24" fill="%s" stroke="#e8eef5" stroke-width="1.5"/>' % (x, y - 18, LEVEL_FILL[lv["id"]]))
            self.text(x + 60, y, "%s %s: %+d cm / %+d cm  (%s)" % (lv["id"], lv["name"], lv["z_cm"], lv["z_sc2_cm"], lv["note"]), 16, "#dfe7f1", halo=False)
        y += 34
        add('<rect x="%.1f" y="%.1f" width="46" height="24" fill="%s" stroke="#e8eef5" stroke-width="1.5"/>' % (x, y - 18, CLIFF))
        self.text(x + 60, y, "Cliff or rock-wall band, 300 cm (no walking)", 16, "#dfe7f1", halo=False)
        y += 44
        self.text(x, y, "SYMBOLS", 17, "#ffffff", weight="bold", halo=False)
        items = [
            ("circle", NEUTRAL, "Sector: capture ring 430 (solid), territory 1000 (dashed)"),
            ("ring", HUMAN, "Human HQ, 900 territory disc; halves = pockets A/B with bays A1-B4"),
            ("ring", MACHINE, "JEV HQ (The Cluster) and its 900 territory disc"),
            ("ramp", "#e8eef5", "Ramp (arrow uphill; two heads = flat gap in a wall); label = width in cm"),
            ("route1", ROUTE_COLORS["no_door"], "Route A (orange): Cluster > West door > NW pocket > West Pass > North gate"),
            ("route2", ROUTE_COLORS["no_gate"], "Route B (magenta): Cluster > South gate > East Pass > SE pocket > East door"),
            ("dots", "#7df9ff", "JEV expansion walks (its two nearest sectors)"),
            ("diamond", "#b6f28a", "Vision tower (proposal, no code support yet)"),
            ("plug", "#d99a5b", "Destructible rock plug / breach (proposal)"),
            ("square", HUMAN, "Per-slot start HQ (proposal, dashed)"),
        ]
        for kind, col, text in items:
            y += 30
            if kind == "circle":
                add('<circle cx="%.1f" cy="%.1f" r="9" fill="%s" fill-opacity="0.3" stroke="%s" stroke-width="3"/>' % (x + 23, y - 5, col, col))
            elif kind == "ring":
                add('<circle cx="%.1f" cy="%.1f" r="9" fill="none" stroke="%s" stroke-width="2" stroke-dasharray="4 3"/>' % (x + 23, y - 5, col))
            elif kind == "ramp":
                add('<rect x="%.1f" y="%.1f" width="34" height="16" fill="%s" stroke="#e8eef5" stroke-width="2"/>' % (x + 6, y - 14, LEVEL_FILL["L1"]))
            elif kind in ("route1", "route2"):
                add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="6" stroke-linecap="round"/>' % (x + 4, y - 5, x + 44, y - 5, col))
            elif kind == "dots":
                add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="4" stroke-dasharray="4 7" stroke-linecap="round"/>' % (x + 4, y - 5, x + 44, y - 5, col))
            elif kind == "diamond":
                add('<polygon points="%.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f" fill="none" stroke="%s" stroke-width="3"/>' % (x + 23, y - 16, x + 34, y - 5, x + 23, y + 6, x + 12, y - 5, col))
            elif kind == "plug":
                add('<rect x="%.1f" y="%.1f" width="34" height="16" fill="url(#plug)" stroke="%s" stroke-width="2"/>' % (x + 6, y - 14, col))
            elif kind == "square":
                add('<rect x="%.1f" y="%.1f" width="16" height="16" fill="none" stroke="%s" stroke-width="2.5" stroke-dasharray="3 3"/>' % (x + 15, y - 14, col))
            self.text(x + 60, y, text, 15, "#dfe7f1", halo=False)
        y += 46
        self.text(x, y, "KEY NUMBERS (measured from this file)", 17, "#ffffff", weight="bold", halo=False)
        rows = []
        hq = a.table["HQ_H"]["HQ_J"][0]
        rows.append("HQ to HQ walk: %.0f m  =  %.0f s at %.0f cm/s" % (hq / 100, a.secs(hq), a.speed))
        ra, rb = a.routes["no_door"][0], a.routes["no_gate"][0]
        rows.append("Route A (via North gate): %.0f m, %.0f s;  Route B (via East door): %.0f m, %.0f s" % (ra / 100, a.secs(ra), rb / 100, a.secs(rb)))
        rows.append("Route gap %.2f %%; opened breach (proposal) = third route, %.0f m" % (100 * a.route_gap, a.routes["breach_open"][0] / 100))
        s1 = a.table["HQ_H"]["S1"][0]
        s2 = a.table["HQ_H"]["S2"][0]
        s3 = a.table["HQ_H"]["S3"][0]
        s4 = a.table["HQ_H"]["S4"][0]
        rows.append("Bunker to sectors 1-4: %.0f, %.0f, %.0f, %.0f s" % (a.secs(s1), a.secs(s2), a.secs(s3), a.secs(s4)))
        rows.append("Cluster to sectors 8, 7, 6, 5: %.0f, %.0f, %.0f, %.0f s" % tuple(a.secs(a.table["HQ_J"]["S%d" % k][0]) for k in (8, 7, 6, 5)))
        rows.append("Narrowest passage: main ramp 700 cm = 3.9 formation widths")
        rows.append("Terrace edge to Cluster: %.0f cm (JEV intruder ring 1500)" % a.intruder_edge)
        for row in rows:
            y += 27
            self.text(x, y, row, 15, "#dfe7f1", halo=False)
        y += 44
        self.text(x, y, "SECTORS (SiteIndex order = HUD SECTOR n)", 17, "#ffffff", weight="bold", halo=False)
        y += 28
        self.text(x, y, "#", 14, "#9aa7b6", halo=False)
        self.text(x + 34, y, "name", 14, "#9aa7b6", halo=False)
        self.text(x + 250, y, "role", 14, "#9aa7b6", halo=False)
        self.text(x + 470, y, "from Bunker", 14, "#9aa7b6", "start", halo=False)
        self.text(x + 610, y, "from Cluster", 14, "#9aa7b6", "start", halo=False)
        for sec in d["sectors"]:
            y += 26
            key = "S%d" % (sec["site_index"] + 1)
            col = HUMAN if sec["side"] == "H" else MACHINE
            add('<circle cx="%.1f" cy="%.1f" r="8" fill="%s"/>' % (x + 8, y - 5, col))
            self.text(x + 34, y, sec["name"], 15, "#ffffff", halo=False)
            self.text(x + 250, y, sec["role"].replace("_", " "), 15, "#dfe7f1", halo=False)
            self.text(x + 470, y, "%.0f s" % a.secs(a.table["HQ_H"][key][0]), 15, "#dfe7f1", halo=False)
            self.text(x + 610, y, "%.0f s" % a.secs(a.table["HQ_J"][key][0]), 15, "#dfe7f1", halo=False)
            self.text(x + 8, y, str(sec["site_index"] + 1), 12, "#ffffff", "middle", "bold", halo=False)
        y += 46
        self.text(x, y, "RAMPS (identical on both sides)", 17, "#ffffff", weight="bold", halo=False)
        y += 28
        for col, head in ((0, "ramp"), (250, "width x length"), (450, "phase A slope (rise)"), (610, "SC2 slope (rise)")):
            self.text(x + col, y, head, 14, "#9aa7b6", halo=False)
        for r in d["ramps"]:
            if r["side"] != "H":
                continue
            y += 26
            self.text(x, y, r["name"].replace("Human ", ""), 15, "#ffffff", halo=False)
            self.text(x + 250, y, "%d x %d cm" % (r["width"], r["length"]), 15, "#dfe7f1", halo=False)
            rise_a = abs(self.a.z[r["from_level"]]["z_cm"] - self.a.z[r["to_level"]]["z_cm"])
            rise_b = abs(self.a.z[r["from_level"]]["z_sc2_cm"] - self.a.z[r["to_level"]]["z_sc2_cm"])
            self.text(x + 450, y, "%.1f deg (%d cm)" % (r["slope_z_cm"], rise_a), 15, "#dfe7f1", halo=False)
            self.text(x + 610, y, "%.1f deg (%d cm)" % (r["slope_z_sc2_cm"], rise_b), 15, "#dfe7f1", halo=False)


def rasterise(svg_path, png_path, width):
    if shutil.which("rsvg-convert"):
        subprocess.run(["rsvg-convert", "-w", str(width), "-o", png_path, svg_path], check=True)
    elif shutil.which("magick"):
        subprocess.run(["magick", "-density", "96", svg_path, png_path], check=True)
    else:
        raise RuntimeError("Install rsvg-convert or ImageMagick to rasterise the SVG")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("map", nargs="?", default=os.path.join(ROOT, "Build", "Maps", "AvailabilityZone.json"))
    parser.add_argument("--out", help="PNG path (default Art/Maps/<Map>-layout.png)")
    parser.add_argument("--svg", help="also keep the SVG here")
    parser.add_argument("--report", action="store_true", help="print the measured tables")
    parser.add_argument("--quiet", action="store_true", help="print errors only")
    args = parser.parse_args()
    with open(args.map) as handle:
        data = json.load(handle)
    an = Analysis(data)
    ok = an.run()
    if args.report:
        print(an.report())
    elif not args.quiet:
        print("checks: %s" % ("all passed" if ok else "FAILED"))
        for e in an.errors:
            print("  - " + e)
    if not args.report and args.quiet:
        for e in an.errors:
            print(e)
    out = args.out or os.path.join(ROOT, "Art", "Maps", data["name"] + "-layout.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    svg = Svg(data, an).draw()
    svg_path = args.svg or out[:-4] + ".svg.tmp"
    with open(svg_path, "w") as handle:
        handle.write(svg)
    try:
        rasterise(svg_path, out, int(Svg(data, an).width))
    finally:
        if not args.svg and os.path.exists(svg_path):
            os.remove(svg_path)
    print("wrote " + os.path.relpath(out, ROOT))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
