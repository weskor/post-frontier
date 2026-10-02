"""Boot/CampusZero post rules on the geometry emitted by their real generators."""

from collections.abc import Mapping, Sequence
from copy import deepcopy

from AvailabilityZoneLayout import env_sizes
import MatchLayout
import pytest


class GeometryRecorder:
    """Record ground footprints, not editor properties or decorative mesh bounds."""

    def __init__(self) -> None:
        self.blockers: dict[str, tuple[float, ...]] = {}
        self.kit_sizes = env_sizes()
        self.decorations: list[tuple[tuple[object, ...], dict[str, object]]] = []

    def primitive(
        self,
        label: str,
        location: Sequence[float],
        scale: Sequence[float],
        surface: object,
        mesh: object = None,
        collision: bool = True,
    ) -> None:
        if collision and label != "ArenaFloor":
            self.blockers[label] = (*location[:2], scale[0] * 100, scale[1] * 100, 0)

    def block(
        self,
        label: str,
        center: Sequence[float],
        size: Sequence[float],
        surface: object,
        yaw: float = 0.0,
        base: float = 0.0,
        collision: bool = True,
        mesh: object = None,
        check: bool = True,
    ) -> None:
        if collision and check:
            self.blockers[label] = (*center, *size[:2], yaw)

    def kit(
        self,
        name: str,
        label: str,
        center: Sequence[float],
        size: Sequence[float],
        yaw: float = 0.0,
        base: float = 0.0,
        collision: bool = True,
        round_shape: bool = False,
        materials: Mapping[str, object] | None = None,
    ) -> None:
        if collision:
            # The kit contract has a narrower ground body for the high-cross-arm Pylon.
            self.blockers[label] = (*center, *self.kit_sizes[name][3:], yaw)

    def kit_hall(
        self,
        label: str,
        center: Sequence[float],
        size: Sequence[float],
        doors: Mapping[str, int],
    ) -> None:
        self.blockers[label] = (*center, *size, 0)

    def kit_run(
        self,
        name: str,
        label: str,
        center: Sequence[float],
        size: Sequence[float],
        along_local_x: bool,
        materials: Mapping[str, object] | None = None,
    ) -> None:
        self.blockers[label] = (*center, *size, 0)

    def decoration(self, *args: object, **kwargs: object) -> None:
        # Keep the emitted nonblocking operations separate from ground collision.
        self.decorations.append((args, kwargs))

    def clear_ground(self, point: MatchLayout.Point, clearance: float) -> bool:
        return MatchLayout.clear_of_blockers(
            point, list(self.blockers.values()), clearance
        )


LegacyMap = tuple[tuple[int, int], GeometryRecorder, list[MatchLayout.GameplayRegion]]


@pytest.fixture(params=["Boot", "CampusZero"])
def legacy_map(request: pytest.FixtureRequest) -> LegacyMap:
    # Both shipped legacy arenas use ArenaBounds' 4500 cm default half extent.
    half_extent = (4500, 4500)
    geometry = GeometryRecorder()
    if request.param == "Boot":
        MatchLayout.place_boot_geometry(
            half_extent,
            geometry.primitive,
            {
                "floor": None,
                "obstacle": None,
                "wall": None,
                "teams": [None] * 5,
                "cylinder": None,
            },
        )
        posts = MatchLayout.BOOT_DEFEND_POSTS
    else:
        MatchLayout.place_campus_zero_geometry(
            half_extent,
            block=geometry.block,
            kit=geometry.kit,
            kit_hall=geometry.kit_hall,
            kit_run=geometry.kit_run,
            strip=geometry.decoration,
            light=geometry.decoration,
            surfaces={
                "floor_mat": None,
                "scrap_ground": None,
                "campus_ground": None,
                "road_line": None,
                "concrete": None,
                "steel": None,
                "cyan": None,
                "cyan_dim": None,
                "rust": None,
                "olive": None,
                "container_blue": None,
                "sandbag": None,
                "amber": None,
                "team_materials": [None] * 5,
                "cylinder": None,
                "human_glow": None,
                "capture_radius": 430,
            },
        )
        posts = MatchLayout.CAMPUS_ZERO_DEFEND_POSTS
    regions = MatchLayout.region_plan(half_extent, defend_posts=posts)
    return half_extent, geometry, regions


def errors(
    legacy_map: LegacyMap, regions: list[MatchLayout.GameplayRegion] | None = None
) -> list[str]:
    half_extent, geometry, authored_regions = legacy_map
    return MatchLayout.defend_post_errors(
        authored_regions if regions is None else regions,
        half_extent,
        geometry.clear_ground,
        headquarters=[home[1] for home in MatchLayout.HEADQUARTERS],
    )


def test_real_legacy_authored_posts(legacy_map: LegacyMap) -> None:
    assert errors(legacy_map) == []


@pytest.mark.parametrize("count", [0, 1, 4])
def test_legacy_post_count(legacy_map: LegacyMap, count: int) -> None:
    regions = deepcopy(legacy_map[2])
    regions[0]["defend_posts"] = [
        regions[0]["defend_posts"][0][:] for _ in range(count)
    ]
    assert any(
        "requires 2-3 defend posts" in error for error in errors(legacy_map, regions)
    )


def test_legacy_posts_on_real_blockers(legacy_map: LegacyMap) -> None:
    _, geometry, regions = legacy_map
    # Exercise every actual ground footprint, including Boot's boundary walls,
    # CampusZero's rotated containers/wrecks and its narrow Pylon ground body.
    for label, (x, y, *_shape) in geometry.blockers.items():
        region = deepcopy(
            next(
                region
                for region in regions
                if MatchLayout.contains(region["poly"], (x, y))
            )
        )
        region["defend_posts"][0] = [x, y]
        assert any(
            "defend post 0 off walkable ground" in error
            for error in errors(legacy_map, [region])
        ), label


def test_legacy_posts_on_headquarters(legacy_map: LegacyMap) -> None:
    for index, (_, center, _) in enumerate(MatchLayout.HEADQUARTERS):
        region = deepcopy(legacy_map[2][index])
        region["defend_posts"][0] = list(center)
        assert any(
            "defend post 0 off walkable ground" in error
            for error in errors(legacy_map, [region])
        )


def test_legacy_uncovered_buildable_ground(legacy_map: LegacyMap) -> None:
    region = deepcopy(legacy_map[2][0])
    # Both remain legal ground posts, but clustering at the northern post leaves
    # the Friendly Main's southern buildable floor farther than 35 metres away.
    region["defend_posts"][1] = region["defend_posts"][0][:]
    assert any("maximum 3500 cm" in error for error in errors(legacy_map, [region]))
