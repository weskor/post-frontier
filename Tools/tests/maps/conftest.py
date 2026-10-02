"""In-memory real maps; expensive measurements are shared, never rendered."""

from copy import copy, deepcopy
import json
from pathlib import Path
import sys
from typing import TYPE_CHECKING, cast

import pytest

if TYPE_CHECKING:
    from AvailabilityZoneLayout import Layout
    from DrawAvailabilityZoneV2 import MapData as V2Data
    from DrawMapLayout import Analysis, MapData

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "Build"))


@pytest.fixture(scope="session")
def real_analysis() -> "Analysis":
    import AvailabilityZoneLayout
    import DrawMapLayout

    analysis = DrawMapLayout.Analysis(AvailabilityZoneLayout.load())
    analysis.check_ramps()
    analysis.measure()
    analysis.edge = analysis.grid.edge_distance()
    assert analysis.errors == []
    return analysis


@pytest.fixture
def analysis(real_analysis: "Analysis") -> "Analysis":
    result = copy(real_analysis)
    result.data = deepcopy(real_analysis.data)
    result.const = result.data["constants"]
    result.hq = {h["id"]: h for h in result.data["headquarters"]}
    result.ramps = {r["id"]: r for r in result.data["ramps"]}
    result.regions = {r["id"]: r for r in result.data["regions"]}
    result.z = {z["id"]: z for z in result.data["elevation"]}
    result.errors = []
    return result


@pytest.fixture(scope="session")
def real_layout(real_analysis: "Analysis") -> "Layout":
    from AvailabilityZoneLayout import Layout

    layout = Layout.__new__(Layout)
    layout.data = real_analysis.data
    layout.const = layout.data["constants"]
    layout.hx, layout.hy = map(float, layout.data["arena"]["half_extent"])
    layout.kit_sizes = {}
    layout.analysis = real_analysis
    layout.analysis_ok = True
    layout.hq, layout.regions, layout.ramps = (
        real_analysis.hq,
        real_analysis.regions,
        real_analysis.ramps,
    )
    layout.sectors = layout.data["sectors"]
    layout.issues = []
    layout._classify()
    layout._walls()
    layout._parapets()
    layout._rim()
    layout._keep_out()
    layout.props, layout.rim_props, layout.halls = [], [], []
    layout.slab = None
    layout._towers()
    layout._dress_slab()
    layout._check()
    layout.check_seal()
    assert layout.issues == []
    return layout


@pytest.fixture(scope="session")
def v2_data() -> "V2Data":
    from DrawAvailabilityZoneV2 import DATA, MapData

    return cast(MapData, json.loads(DATA.read_text()))


@pytest.fixture
def v2_map(v2_data: "V2Data") -> "V2Data":
    result = copy(v2_data)
    result["arena"] = deepcopy(v2_data["arena"])
    result["regions"] = [copy(region) for region in v2_data["regions"]]
    for region in result["regions"]:
        region["poly"] = [point[:] for point in region["poly"]]
        region["neighbours"] = region["neighbours"][:]
        region["defend_posts"] = [point[:] for point in region["defend_posts"]]
        if region["anchor"] is not None:
            region["anchor"] = region["anchor"][:]
    result["headquarters"] = deepcopy(v2_data["headquarters"])
    result["deposits"] = deepcopy(v2_data["deposits"])
    result["blockers"] = deepcopy(v2_data["blockers"])
    return result


def change(
    data: "MapData | V2Data", path: tuple[str | int, ...], value: object
) -> None:
    current: object = data
    for key in path[:-1]:
        current = (
            cast(list[object], current)[key]
            if isinstance(key, int)
            else cast(dict[str, object], current)[key]
        )
    key = path[-1]
    if isinstance(key, int):
        cast(list[object], current)[key] = value
    else:
        cast(dict[str, object], current)[key] = value
