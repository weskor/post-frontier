"""Generated geometry must preserve keep-outs, territory and level seals."""

from copy import copy, deepcopy

from AvailabilityZoneLayout import (
    CELL,
    Layout,
    PropData,
    Rect,
    dist_point_rect,
    rects_close,
)
import pytest


@pytest.fixture
def layout(real_layout: Layout) -> Layout:
    result = copy(real_layout)
    result.issues = []
    result.data = deepcopy(real_layout.data)
    for field in ("walls", "parapets", "rim_blocks", "rim_pieces", "halls"):
        setattr(result, field, [value.copy() for value in getattr(real_layout, field)])
    result.keep_circles = real_layout.keep_circles[:]
    result.level = real_layout.level.copy()
    result.slab = real_layout.slab.copy() if real_layout.slab is not None else None
    return result


def clear_of_circles(layout: Layout, rect: Rect) -> bool:
    return all(
        dist_point_rect(point[0], point[1], rect) >= radius
        for _name, point, radius in layout.keep_circles
    )


@pytest.mark.parametrize(
    "group", ["hq", "sector", "bay", "player", "ramp", "corridor", "arena"]
)
def test_keep_out(layout: Layout, group: str) -> None:
    # A known clear floor point, found from the real map rather than a fake response.
    clear = next(
        (i * CELL, j * CELL, 20.0, 20.0, 0.0)
        for (i, j), kind in layout.kind.items()
        if kind == "P"
        and layout.keep_out_reason((i * CELL, j * CELL, 20, 20, 0)) is None
    )
    assert layout.keep_out_reason(clear) is None
    exempt = ("hq",) if group == "bay" else ()
    if group in ("hq", "sector", "bay", "player"):
        label, centre, _radius = next(
            c for c in layout.keep_circles if c[0].startswith(group)
        )
        rect: Rect = (centre[0], centre[1], 20, 20, 0)
        expected = "keep-out " + label
    elif group == "ramp":
        label, keep = next(
            k
            for k in layout.keep_rects
            if clear_of_circles(layout, (k[1][0], k[1][1], 20, 20, 0))
        )
        rect = (keep[0], keep[1], 20, 20, 0)
        expected = "keep-out ramp"
    elif group == "corridor":
        point = next(
            p
            for p in layout.corridor_points
            if clear_of_circles(layout, (p[0], p[1], 20, 20, 0))
            and all(
                not rects_close((p[0], p[1], 20, 20, 0), keep)
                for _name, keep in layout.keep_rects
            )
        )
        rect = (point[0], point[1], 20, 20, 0)
        expected = "keep-out corridor"
    else:
        rect = (layout.hx + 1, 0, 20, 20, 0)
        expected = "outside the arena"
    reason = layout.keep_out_reason(rect, exempt)
    assert reason is not None and reason.startswith(expected)


@pytest.mark.parametrize(
    "rule",
    [
        "metres",
        "strip",
        "territory-wall",
        "territory-rim",
        "wall-coverage",
        "territory-levels",
    ],
)
def test_geometry_checks(layout: Layout, rule: str) -> None:
    if rule == "territory-levels":
        layout.kind, layout.walls = {}, []
    layout._check()
    assert layout.issues == []
    if rule == "metres":
        assert layout.slab is not None
        layout.slab["size"] = (100.5, 100)
    elif rule == "strip":
        strip = layout.rect_of_poly(layout.data["ramps"][0]["strips"][0])
        layout.parapets.append(
            {
                "label": "intruder",
                "center": (strip[0], strip[1]),
                "size": (10, 10),
                "look": "human",
            }
        )
    elif rule == "territory-wall":
        centre = layout.hq["HQ_H"]["pos"]
        layout.rim_blocks.append(
            {
                "label": "intruder",
                "center": (centre[0], centre[1]),
                "size": (10, 10),
                "look": "human",
            }
        )
    elif rule == "territory-rim":
        centre = layout.hq["HQ_H"]["pos"]
        layout.rim_pieces[0]["center"] = (centre[0], centre[1])
    elif rule == "wall-coverage":
        layout.walls.pop()
    else:
        centre = layout.hq["HQ_H"]["pos"]
        layout.level[layout.cell_of(*centre)] = "L0"
    layout._check()
    messages = {
        "metres": "whole number of metres",
        "strip": "touches the walkable strip",
        "territory-wall": "enters hq",
        "territory-rim": "enters hq",
        "wall-coverage": "wall runs cover",
        "territory-levels": "spans levels",
    }
    assert layout.issues and all(messages[rule] in issue for issue in layout.issues)


def test_tower_keep_out(layout: Layout) -> None:
    layout._towers()
    assert layout.issues == []
    layout.data["proposals"]["vision_points"][0]["pos"] = layout.hq["HQ_H"]["pos"][:]
    layout._towers()
    assert len(layout.issues) == 1 and "keep-out hq" in layout.issues[0]


def test_parapet_yaw(layout: Layout) -> None:
    layout._parapets()
    layout.data["ramps"][0]["pieces"][0]["yaw"] = 1
    with pytest.raises(ValueError, match="not a multiple of 90"):
        layout._parapets()


def miniature_seal(layout: Layout) -> Layout:
    layout.extent = 3
    layout.region_of = {
        (i, j): ("main_H" if i < 0 else "terrace_H")
        for i in (-2, -1, 1, 2)
        for j in range(-2, 3)
    }
    layout.level = {cell: ("L2" if cell[0] < 0 else "L1") for cell in layout.region_of}
    layout.regions = {rid: layout.regions[rid] for rid in ("main_H", "terrace_H")}
    layout.rim_blocks, layout.rim_pieces, layout.parapets = [], [], []
    layout.data["blockers"] = []
    layout.walls = [
        {"label": "wall", "center": (0, y), "size": (100, 1200), "look": "human"}
        for y in (-800, 800)
    ]
    layout.data["ramps"] = [layout.data["ramps"][0]]
    layout.data["ramps"][0]["footprints"] = [
        [[-200, -200], [200, -200], [200, 200], [-200, 200]]
    ]
    return layout


@pytest.mark.parametrize("damage", ["blocked-start", "leak", "disconnected"])
def test_level_seals(layout: Layout, damage: str) -> None:
    assert layout.seal["open"] == sorted(layout.regions)
    layout = miniature_seal(layout)
    layout.check_seal()
    assert layout.issues == []
    if damage == "leak":
        layout.walls = []
        message = "should reach only"
    else:
        cells = [c for c, rid in layout.region_of.items() if rid == "main_H"]
        point = (
            layout.centre_of(cells[len(cells) // 2])
            if damage == "blocked-start"
            else (0.0, 0.0)
        )
        layout.rim_blocks.append(
            {"label": "plug", "center": point, "size": (400, 400), "look": "human"}
        )
        message = (
            "start of main_H is not free"
            if damage == "blocked-start"
            else "not every region"
        )
    layout.check_seal()
    assert any(message in issue for issue in layout.issues)


@pytest.mark.parametrize(
    "obstacle", ["ground", "wall", "prop", "rim-prop", "tower", "hall"]
)
def test_deposit_clearance(layout: Layout, obstacle: str) -> None:
    point = next(
        layout.centre_of(cell)
        for cell, kind in layout.kind.items()
        if kind == "P" and layout.deposit_clear(layout.centre_of(cell), 95)
    )
    assert layout.deposit_clear(point, 95)
    if obstacle == "ground":
        point = (layout.hx, layout.hy)
    elif obstacle == "wall":
        layout.walls.append(
            {"label": "plug", "center": point, "size": (100, 100), "look": "human"}
        )
    elif obstacle in ("prop", "rim-prop"):
        prop: PropData = {
            "kit": "fixture",
            "label": "plug",
            "center": point,
            "size": (100, 100),
            "yaw": 0.0,
            "look": "human",
            "round": False,
        }
        layout.props = [prop] if obstacle == "prop" else []
        layout.rim_props = [prop] if obstacle == "rim-prop" else []
    elif obstacle == "tower":
        layout.towers = [{"label": "plug", "center": point, "look": "human"}]
    else:
        layout.halls.append(
            {"label": "plug", "center": point, "size": (100, 100), "doors": {}}
        )
    assert not layout.deposit_clear(point, 95)


def test_rim_footprint(layout: Layout) -> None:
    cell = next(iter(layout.interior))
    x, y = layout.centre_of(cell)
    assert layout._rim_footprint_ok((x, y, 20, 20, 0))
    edge = layout.rim_pieces[0]["center"]
    assert not layout._rim_footprint_ok((edge[0], edge[1], 20, 20, 0))
