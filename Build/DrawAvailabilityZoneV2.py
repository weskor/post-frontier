#!/usr/bin/env python3
"""Derive AvailabilityZoneV2.json from the country outlines and validate/render it.

    python3 Build/DrawAvailabilityZoneV2.py --derive
    python3 Build/DrawAvailabilityZoneV2.py [--out Art/Maps/AvailabilityZoneV2-layout.png]

Only --derive reads the Excalidraw source. Raster ownership (2 source pixels per
cell) reconciles the independently sampled, slightly overlapping country paths;
shared cell edges become IDENTICAL polygon edges, not approximate near-matches.
The nearest adjacent country owns the narrow unpainted gaps and the square arena
margin. Default validation uses only the resulting JSON (no sketch dependency).
"""
import argparse
from collections import Counter, defaultdict, deque
import heapq
import json
import math
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "Build/Maps/AvailabilityZoneV2.json"
SKETCH = ROOT / "Art/Maps/AvailabilityZone-v2-extractor-sites.excalidraw"
PNG = ROOT / "Art/Maps/AvailabilityZoneV2-layout.png"
SCALE = 20000 / 1140                 # one source pixel in world centimetres
STEP = 2                            # raster reconciliation resolution in source pixels
L = 570                             # exact 200 x 200 m arena: 570 * 2 pixels
ORIGIN_X, ORIGIN_Y = 64, 102        # upper-left sketch corner of the arena square
WALK_CELL = 100                     # world-centimetre walking grid
SPEED = 420                        # source unit movement speed in cm/s
NAMES = ("Human Main", "Human Near", "West Cut", "Uplink", "North Ridge",
         "Relay Plant", "Interchange", "Switchback", "Power Yard", "South Outcrop",
         "East Spur", "Cooling", "Canal Walk", "Machine Near", "Machine Main")
ROLES = ("main", "natural", "tactical", "reward", "tactical", "reward", "tactical",
         "tactical", "reward", "tactical", "tactical", "reward", "tactical", "natural", "main")
# Source sketch coordinates: visually chosen well inside each country, clear of dark rocks.
ANCHORS = (None, (480, 895), (210, 805), (300, 495), (250, 240),
           (565, 290), (605, 575), (700, 785), (880, 1050), (510, 1130),
           (1000, 865), (1050, 630), (900, 455), (855, 275), None)
HQ_SITES = ((240, 1060), (1080, 200))  # leave >11 m from main deposits for future extractors


def world(point):
    px, py = point
    return [round((ORIGIN_Y + L * STEP / 2 - py) * SCALE, 3),
            round((px - ORIGIN_X - L * STEP / 2) * SCALE, 3)]


def inside(point, poly):
    x, y = point
    odd = False
    for a, b in zip(poly, poly[1:] + poly[:1]):
        if (a[1] > y) != (b[1] > y) and x < a[0] + (y - a[1]) * (b[0] - a[0]) / (b[1] - a[1]):
            odd = not odd
    return odd


def distance_segment(point, a, b):
    vx, vy = b[0] - a[0], b[1] - a[1]
    t = max(0., min(1., ((point[0] - a[0]) * vx + (point[1] - a[1]) * vy) / (vx * vx + vy * vy))) if vx or vy else 0
    return math.hypot(point[0] - a[0] - t * vx, point[1] - a[1] - t * vy)


def clearance(point, poly):
    return min(distance_segment(point, a, b) for a, b in zip(poly, poly[1:] + poly[:1]))


def area(poly):
    return sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(poly, poly[1:] + poly[:1])) / 2


def raster_countries(elements):
    """Take the actual fifteen Excalidraw paths, preserving their irregular edges."""
    outlines = [[(elt['x'] + x, elt['y'] + y) for x, y in elt['points']] for elt in elements[:15]]
    assert len(outlines) == 15 and all(len(poly) >= 80 for poly in outlines)
    labels = [[-1] * L for _ in range(L)]
    overlaps = 0
    for k, poly in enumerate(outlines):
        lo_y = max(0, int((min(y for _, y in poly) - ORIGIN_Y) // STEP))
        hi_y = min(L - 1, int((max(y for _, y in poly) - ORIGIN_Y) // STEP))
        for j in range(lo_y, hi_y + 1):
            y = ORIGIN_Y + (j + .5) * STEP
            xs = sorted(a[0] + (y - a[1]) * (b[0] - a[0]) / (b[1] - a[1])
                        for a, b in zip(poly, poly[1:] + poly[:1])
                        if (a[1] > y) != (b[1] > y))
            for left, right in zip(xs[::2], xs[1::2]):
                lo = max(0, int(math.ceil((left - ORIGIN_X) / STEP - .5)))
                hi = min(L - 1, int(math.ceil((right - ORIGIN_X) / STEP - .5)) - 1)
                for i in range(lo, hi + 1):
                    prev = labels[j][i]
                    if prev >= 0:
                        overlaps += 1
                        p = (ORIGIN_X + (i + .5) * STEP, y)
                        if clearance(p, outlines[prev]) >= clearance(p, poly):
                            continue
                    labels[j][i] = k
    # Unpainted slivers of independently drawn borders, and the thin peripheral
    # arena margin: propagate ownership outward from the real painted outlines.
    queue = deque((i, j) for j in range(L) for i in range(L) if labels[j][i] >= 0)
    uncovered = L * L - len(queue)
    while queue:
        i, j = queue.popleft()
        for di, dj in ((1, 0), (0, 1), (-1, 0), (0, -1)):
            a, b = i + di, j + dj
            if 0 <= a < L and 0 <= b < L and labels[b][a] == -1:
                labels[b][a] = labels[j][i]
                queue.append((a, b))
    # A raster contour is non-manifold when two cells of the same country meet
    # only diagonally. Two such single-pixel junctions occur in this sketch.
    diagonal_contacts = 0
    for j in range(L - 1):
        for i in range(L - 1):
            a, b = labels[j][i], labels[j][i + 1]
            c, d = labels[j + 1][i], labels[j + 1][i + 1]
            if a == d and a != b and a != c:
                labels[j + 1][i + 1] = c
                diagonal_contacts += 1
            elif b == c and b != a and b != d:
                labels[j + 1][i] = d
                diagonal_contacts += 1
    # Preserve the large real country, absorb only detached edge pixels into
    # their bordering neighbour (never silently discard a substantive island).
    slivers = 0
    for region in range(15):
        seen = set()
        components = []
        for j in range(L):
            for i in range(L):
                if labels[j][i] != region or (i, j) in seen:
                    continue
                component = [(i, j)]
                seen.add((i, j))
                queue = deque(component)
                while queue:
                    x, y = queue.popleft()
                    for a, b in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                        if 0 <= a < L and 0 <= b < L and labels[b][a] == region and (a, b) not in seen:
                            seen.add((a, b))
                            component.append((a, b))
                            queue.append((a, b))
                components.append(component)
        largest = max(components, key=len)
        for component in components:
            if component is largest:
                continue
            if len(component) > 20:
                raise ValueError(f"Country {region} has substantive detached component of {len(component)} cells")
            for x, y in component:
                neighbours = [labels[b][a] for a, b in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))
                              if 0 <= a < L and 0 <= b < L and labels[b][a] != region]
                labels[y][x] = Counter(neighbours).most_common(1)[0][0]
                slivers += 1
    if any(labels[j][i] == labels[j + 1][i + 1] != labels[j][i + 1] and
           labels[j][i] != labels[j + 1][i] for j in range(L - 1) for i in range(L - 1)):
        raise ValueError("Unresolved diagonal country contact")
    print(f"Sketch contour cleanup: {diagonal_contacts} corner contacts, {slivers} detached edge cells")
    print(f"Sketch reconciliation: {overlaps} overlapping, {uncovered} uncovered 2px cells; nearest painted country fills gaps/margin")
    return labels


def cell_rings(labels, region):
    # Directed cell edges have the owner's inside to their LEFT in sketch pixels.
    # Their orientation flips after the vertical world-axis inversion below.
    following = {}
    for j, row in enumerate(labels):
        for i, k in enumerate(row):
            if k != region:
                continue
            if j == 0 or labels[j - 1][i] != k:
                following[(i, j)] = (i + 1, j)
            if i == L - 1 or row[i + 1] != k:
                following[(i + 1, j)] = (i + 1, j + 1)
            if j == L - 1 or labels[j + 1][i] != k:
                following[(i + 1, j + 1)] = (i, j + 1)
            if i == 0 or row[i - 1] != k:
                following[(i, j + 1)] = (i, j)
    rings = []
    while following:
        first = next(iter(following))
        cur, chain = first, []
        while True:
            chain.append(cur)
            cur = following.pop(cur)
            if cur == first:
                break
        rings.append(chain)
    return rings


def rock_poly(element):
    path = [(element['x'] + x, element['y'] + y) for x, y in element['points']]
    # Four-vertex oriented footprint enclosing the actual short, curved stroke.
    # Keep 4 sketch pixels (~70 cm) of rock thickness on either side.
    dx, dy = path[-1][0] - path[0][0], path[-1][1] - path[0][1]
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length
    vx, vy = -uy, ux
    along = [x * ux + y * uy for x, y in path]
    across = [x * vx + y * vy for x, y in path]
    lo_a, hi_a = min(along) - 4, max(along) + 4
    lo_b, hi_b = min(across) - 4, max(across) + 4
    poly = [(a * ux + b * vx, a * uy + b * vy)
            for a, b in ((lo_a, lo_b), (hi_a, lo_b), (hi_a, hi_b), (lo_a, hi_b))]
    converted = [world(p) for p in poly]
    return converted if area(converted) > 0 else converted[::-1]


def derive():
    elements = json.loads(SKETCH.read_text())['elements']
    assert [e['type'] for e in elements[:15]] == ['line'] * 15
    assert len([e for e in elements if e['type'] == 'diamond']) == 16
    labels = raster_countries(elements)
    # Simplify only after collecting bends from ALL rings. A corner on one
    # country's border must split its neighbour's otherwise straight edge.
    raw = [cell_rings(labels, k) for k in range(15)]
    bends = set()
    for rings in raw:
        if len(rings) != 1:
            raise ValueError(f"Country has {len(rings)} components/holes; source needs reconciliation")
        ring = rings[0]
        bends.update(p for n, p in enumerate(ring) if
                     (ring[n - 1][0] - p[0]) * (ring[(n + 1) % len(ring)][1] - p[1]) !=
                     (ring[n - 1][1] - p[1]) * (ring[(n + 1) % len(ring)][0] - p[0]))
    regions = []
    for k, rings in enumerate(raw):
        poly = [world((ORIGIN_X + i * STEP, ORIGIN_Y + j * STEP))
                for i, j in rings[0] if (i, j) in bends]
        if area(poly) < 0:
            poly.reverse()
        regions.append(dict(index=k, name=NAMES[k], role=ROLES[k],
                            home_team=0 if k == 0 else 5 if k == 14 else -1,
                            poly=poly, anchor=world(ANCHORS[k]) if ANCHORS[k] else None,
                            neighbours=[]))
    # Shared-edge incidence is the source of truth for neighbour relations.
    edges = defaultdict(set)
    for region in regions:
        p = region['poly']
        for a, b in zip(p, p[1:] + p[:1]):
            edges[tuple(sorted((tuple(a), tuple(b))))].add(region['index'])
    for owners in edges.values():
        if len(owners) == 2:
            a, b = sorted(owners)
            regions[a]['neighbours'].append(b)
            regions[b]['neighbours'].append(a)
    for r in regions:
        r['neighbours'] = sorted(set(r['neighbours']))
    # Index deposits against their source country, not nearest label centre.
    deposits = []
    for elt in elements:
        if elt['type'] != 'diamond':
            continue
        px, py = elt['x'] + elt['width'] / 2, elt['y'] + elt['height'] / 2
        i, j = int((px - ORIGIN_X) / STEP), int((py - ORIGIN_Y) / STEP)
        k = labels[j][i]
        deposits.append(dict(region=k, pos=world((px, py)), kind='rich' if ROLES[k] == 'reward' else 'normal'))
    data = dict(name='AvailabilityZoneV2', title='Availability Zone V2',
                unreal_map='/Game/Maps/AvailabilityZoneV2', schema=2,
                units='Centimetres. X is minimap-up, Y is minimap-right, Z is up. Point = [x, y].',
                provenance='15 Excalidraw country outlines and 16 diamonds, 2 source-pixel ownership reconciliation; sketch-space arena x=64..1204 y=102..1242, world X=(672-sketch_y)*20000/1140, world Y=(sketch_x-634)*20000/1140. Peripheral unpainted margin/gaps belong to nearest painted region.',
                symmetry='asymmetric: drawn outlines, rock marks, deposits, and the seven tactical regions do not admit 180-degree rotation',
                arena=dict(half_extent=[10000, 10000], placement_margin=100, half_height=1000),
                headquarters=[dict(id='HQ_H', team=0, name='The Bunker', pos=world(HQ_SITES[0])),
                              dict(id='HQ_J', team=5, name='The Cluster', pos=world(HQ_SITES[1]))],
                regions=regions, deposits=deposits,
                blockers=[dict(id=f'rock_{n + 1}', kind='rock', name=f'Partial Rock Cover {n + 1}', poly=rock_poly(elt))
                          for n, elt in enumerate(elements[16:21])],
                navigation=dict(bounds_volume=dict(centre=[0, 0, 0], half_extent=[10200, 10200, 1400])))
    DATA.write_text(json.dumps(data, indent=2) + '\n')
    print(f"Derived {len(regions)} regions, {len(deposits)} deposits, {len(data['blockers'])} partial rock marks -> {DATA}")


def audit(data):
    errors = []
    regions = data['regions']
    if len(regions) != 15 or [r['index'] for r in regions] != list(range(15)):
        errors.append('Region indices must be 0..14')
    if len(data['deposits']) != 16:
        errors.append('Exactly 16 sketch diamonds expected')
    if Counter(r['role'] for r in regions) != {'main': 2, 'natural': 2, 'reward': 4, 'tactical': 7}:
        errors.append('Region role counts differ from sketch')
    hx, hy = data['arena']['half_extent']
    for r in regions:
        p = r['poly']
        if area(p) <= 0:
            errors.append(f"{r['index']} is not CCW")
        if not all(-hx <= x <= hx and -hy <= y <= hy for x, y in p):
            errors.append(f"{r['index']} extends beyond arena")
        if r['home_team'] != (0 if r['index'] == 0 else 5 if r['index'] == 14 else -1):
            errors.append(f"{r['index']} home team mismatch")
        if r['anchor'] is None:
            if r['role'] != 'main':
                errors.append(f"{r['index']} missing anchor")
        elif r['role'] == 'main' or not inside(r['anchor'], p):
            errors.append(f"{r['index']} invalid/outside anchor")
    for h in data['headquarters']:
        main = regions[0 if h['team'] == 0 else 14]
        if not inside(h['pos'], main['poly']):
            errors.append(f"HQ {h['id']} outside main")
    for d in data['deposits']:
        if not inside(d['pos'], regions[d['region']]['poly']):
            errors.append(f"deposit {d} outside country")
        # An extractor resolves to the deposit position, then needs its complete
        # 95 cm half-extent inside the controlled region, not just its pivot.
        if any(not inside((d['pos'][0] + dx, d['pos'][1] + dy), regions[d['region']]['poly'])
               for dx in (-95, 95) for dy in (-95, 95)):
            errors.append(f"deposit extractor footprint crosses country border: {d}")
        if d['kind'] != ('rich' if regions[d['region']]['role'] == 'reward' else 'normal'):
            errors.append(f"deposit kind mismatch in region {d['region']}")
        if d['region'] in (0, 14):
            hq = next(h for h in data['headquarters'] if h['team'] == (0 if d['region'] == 0 else 5))
            if math.dist(d['pos'], hq['pos']) < 1100:
                errors.append(f"main deposit too close to HQ exclusion footprint: {d}")
        elif regions[d['region']]['anchor'] is not None:
            if math.dist(d['pos'], regions[d['region']]['anchor']) < 530:
                errors.append(f"deposit conflicts with capture anchor footprint: {d}")
    sites = [(f"HQ {h['id']}", h['pos']) for h in data['headquarters']]
    sites += [(f"anchor {r['index']}", r['anchor']) for r in regions if r['anchor'] is not None]
    sites += [(f"deposit {n + 1}", d['pos']) for n, d in enumerate(data['deposits'])]
    for name, pos in sites:
        for b in data['blockers']:
            if inside(pos, b['poly']) or clearance(pos, b['poly']) < 90:
                errors.append(f"{name} within 90 cm of {b['id']}")
    # EXACT topology: every internal edge has exactly two opposing users. Counts
    # are computed from polygon edges, not from the stated neighbour metadata.
    incidence = defaultdict(list)
    for r in regions:
        p = r['poly']
        for a, b in zip(p, p[1:] + p[:1]):
            key = tuple(sorted((tuple(a), tuple(b))))
            incidence[key].append((r['index'], tuple(a), tuple(b)))
    actual = [set() for _ in regions]
    boundary_length = 0.
    for key, users in incidence.items():
        if len(users) == 1:
            a, b = key
            boundary_length += math.dist(a, b)
            if not (abs(a[0]) == hx and abs(b[0]) == hx or abs(a[1]) == hy and abs(b[1]) == hy):
                errors.append(f"Unmatched interior edge: {key}")
        elif len(users) == 2:
            a, b = users
            if a[1] != b[2] or a[2] != b[1]:
                errors.append(f"Edge direction not opposed: {key}")
            actual[a[0]].add(b[0])
            actual[b[0]].add(a[0])
        else:
            errors.append(f"Edge has {len(users)} users: {key}")
    for r in regions:
        n = r['index']
        if sorted(actual[n]) != sorted(r['neighbours']):
            errors.append(f"Neighbour metadata mismatch for {n}: {r['neighbours']} vs {sorted(actual[n])}")
        if any(n not in regions[k]['neighbours'] for k in r['neighbours']):
            errors.append(f"Neighbour incidence not symmetric for {n}")
    total_area = sum(area(r['poly']) for r in regions)
    if abs(total_area - 4 * hx * hy) > 0.01 or abs(boundary_length - 4 * (hx + hy)) > .01:
        errors.append(f"Not a perfect arena tiling: area {total_area}, boundary {boundary_length}")
    # Independent 100cm grid raster: catch possible interior overlaps even when
    # aggregate area is correct; sample 4 offsets/cell to avoid border-only ties.
    mismatches = 0
    for ix in range(-hx // WALK_CELL, hx // WALK_CELL):
        for iy in range(-hy // WALK_CELL, hy // WALK_CELL):
            for dx, dy in ((.25, .25), (.25, .75), (.75, .25), (.75, .75)):
                x, y = (ix + dx) * WALK_CELL, (iy + dy) * WALK_CELL
                count = sum(inside((x, y), r['poly']) for r in regions)
                mismatches += count != 1
    if mismatches:
        errors.append(f"100cm quarter-cell samples with gaps/overlap: {mismatches} / 160000")
    print(f"Exact shared-edge topology: {len(incidence)} unique edges; {boundary_length:.2f} cm perimeter")
    print(f"100cm quarter-cell tiling: {160000 - mismatches}/160000 singly owned; area {total_area / 1e10:.6f} km²")
    for r in regions:
        print(f"  {r['index']:2} {r['name']:<17} {area(r['poly']) / 1e10:8.6f} km²  {area(r['poly']) / 1e4:10.4f} m²  neighbours {r['neighbours']}")
    counts = Counter(d['region'] for d in data['deposits'])
    print('Deposits by region:', dict(sorted(counts.items())))
    hq_h, hq_j = (h['pos'] for h in data['headquarters'])
    miss = math.dist(hq_h, [-hq_j[0], -hq_j[1]])
    if miss < 1:
        errors.append('HQ sites unexpectedly mirror despite asymmetric sketch; review symmetry claim')
    print(f'Sketch symmetry: NOT rot180; HQ mirror offset {miss:.1f} cm; main areas {area(regions[0]["poly"]) / 1e4:.1f}/{area(regions[14]["poly"]) / 1e4:.1f} m²')
    if errors:
        for error in errors[:30]:
            print('FAIL:', error)
        if len(errors) > 30:
            print(f'... {len(errors) - 30} additional errors')
        raise ValueError(f'{len(errors)} validation errors')
    print('VALID: exact shared topology, CCW/roles, grid tiling, symmetric neighbours, 13 anchors, 16 buildable deposits, HQs, rock clearance')


def walking(data):
    """8-neighbour Dijkstra on 100cm ground cells with 100cm rock clearance."""
    n = 200
    rocks = data['blockers']
    passable = bytearray(n * n)
    for i in range(n):
        x = (i - 99.5) * WALK_CELL
        for j in range(n):
            y = (j - 99.5) * WALK_CELL
            passable[i * n + j] = all(not inside((x, y), b['poly']) and clearance((x, y), b['poly']) >= 100
                                      for b in rocks)
    def nearest(pos):
        i, j = int((pos[0] + 10000) // WALK_CELL), int((pos[1] + 10000) // WALK_CELL)
        if passable[i * n + j]:
            return i * n + j
        return min((a * n + b for a in range(max(0, i - 5), min(n, i + 6))
                    for b in range(max(0, j - 5), min(n, j + 6)) if passable[a * n + b]),
                   key=lambda idx: math.dist(((idx // n - 99.5) * WALK_CELL, (idx % n - 99.5) * WALK_CELL), pos))
    def distances(pos):
        start = nearest(pos)
        dist = [math.inf] * (n * n)
        dist[start] = 0.
        todo = [(0., start)]
        moves = [(di, dj, WALK_CELL * math.hypot(di, dj)) for di in (-1, 0, 1) for dj in (-1, 0, 1) if di or dj]
        while todo:
            length, idx = heapq.heappop(todo)
            if length != dist[idx]:
                continue
            i, j = divmod(idx, n)
            for di, dj, step in moves:
                a, b = i + di, j + dj
                if not (0 <= a < n and 0 <= b < n and passable[a * n + b]):
                    continue
                if di and dj and (not passable[(i + di) * n + j] or not passable[i * n + j + dj]):
                    continue
                node = a * n + b
                if length + step < dist[node]:
                    dist[node] = length + step
                    heapq.heappush(todo, (dist[node], node))
        return dist
    home, enemy = (h['pos'] for h in data['headquarters'])
    from_home, from_enemy = distances(home), distances(enemy)
    def report(dist, point):
        length = dist[nearest(point)]
        if not math.isfinite(length):
            raise ValueError(f'Unreachable site {point}')
        return f'{length / 100:.1f} m / {length / SPEED:.1f} s'
    print(f'Walking HQ_H -> HQ_J: {report(from_home, enemy)}')
    for r in data['regions']:
        if r['anchor'] is not None:
            print(f"  anchor {r['index']:2} {r['name']:<17}: H {report(from_home, r['anchor'])}; J {report(from_enemy, r['anchor'])}")


def render(data, out):
    hx, hy = data['arena']['half_extent']
    # SVG's horizontal screen axis is world Y, vertical screen axis is -world X.
    def pt(p):
        return f'{(p[1] + hy) / 20:.2f},{(hx - p[0]) / 20:.2f}'
    def polygon(poly):
        return ' '.join(pt(p) for p in poly)
    fill = {'main': '#a5cae8', 'natural': '#c9e4ee', 'reward': '#f6d891', 'tactical': '#c6dfc6'}
    out_svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="1640" viewBox="-60 -65 1120 1150">',
               '<rect x="-60" y="-65" width="1120" height="1150" fill="#f1f3f0"/>',
               '<text x="500" y="-30" text-anchor="middle" fill="#273c48" font-size="22" font-family="sans-serif">AVAILABILITY ZONE V2 — sketch-derived playable regions</text>',
               '<rect width="1000" height="1000" fill="#ffffff"/>']
    for r in data['regions']:
        color = '#e7b6bc' if r['index'] in (13, 14) else fill[r['role']]
        out_svg.append(f'<polygon points="{polygon(r["poly"])}" fill="{color}" stroke="#53686a" stroke-width="1.2" stroke-linejoin="round"/>')
    for b in data['blockers']:
        out_svg.append(f'<polygon points="{polygon(b["poly"])}" fill="#394844" stroke="#1b2928" stroke-width="2"/>')
    for r in data['regions']:
        if r['anchor']:
            x, y = map(float, pt(r['anchor']).split(','))
            out_svg.append(f'<circle cx="{x}" cy="{y}" r="8" fill="#f7fff2" stroke="#264d34" stroke-width="2"/><circle cx="{x}" cy="{y}" r="2.5" fill="#264d34"/>')
        xs = [p[0] for p in r['poly']]
        ys = [p[1] for p in r['poly']]
        mid = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)
        # Region labels are visually offset from the anchor and retain index/role.
        x, y = map(float, pt(mid).split(','))
        out_svg.append(f'<text x="{x:.1f}" y="{y - 12:.1f}" font-family="sans-serif" font-size="15" font-weight="bold" fill="#26383b" stroke="#fff" stroke-width="3" paint-order="stroke" text-anchor="middle">{r["index"]:02} {r["name"]}</text>')
    for d in data['deposits']:
        x, y = map(float, pt(d['pos']).split(','))
        color = '#a46700' if d['kind'] == 'rich' else '#286687'
        out_svg.append(f'<polygon points="{x:.2f},{y - 8:.2f} {x + 8:.2f},{y:.2f} {x:.2f},{y + 8:.2f} {x - 8:.2f},{y:.2f}" fill="#fff8df" stroke="{color}" stroke-width="2.6"/>')
    for h in data['headquarters']:
        x, y = map(float, pt(h['pos']).split(','))
        out_svg.append(f'<circle cx="{x}" cy="{y}" r="16" fill="{("#2274a8" if h["team"] == 0 else "#b3475a")}" stroke="#fff" stroke-width="3"/><text x="{x + 20}" y="{y + 29}" font-family="sans-serif" font-size="17" fill="#273c48" stroke="#fff" stroke-width="3" paint-order="stroke">{h["id"]}</text>')
    out_svg += ['<text x="500" y="1040" text-anchor="middle" fill="#344b54" font-family="sans-serif" font-size="17">◇ normal / rich deposits (blue / amber)     ◎ capture anchors     ● HQs     ▰ partial rock cover</text>',
                '<text x="500" y="1069" text-anchor="middle" fill="#53616b" font-family="sans-serif" font-size="15">200 × 200 m | X points up | Y points right | borders mark ownership, not walls | flat Z=0</text>', '</svg>']
    svg = '\n'.join(out_svg).encode()
    subprocess.run(['rsvg-convert', '-f', 'png', '-o', str(out)], input=svg, check=True)
    print(f'Rendered {out}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--derive', action='store_true', help='rebuild JSON from source Excalidraw paths')
    parser.add_argument('--out', type=Path, default=PNG)
    args = parser.parse_args()
    if args.derive:
        derive()
    data = json.loads(DATA.read_text())
    audit(data)
    walking(data)
    render(data, args.out)


if __name__ == '__main__':
    main()
