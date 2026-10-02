# Availability Zone v2 — playable greybox, strategy regression pending

Source of truth: [`Build/Maps/AvailabilityZoneV2.json`](../../Build/Maps/AvailabilityZoneV2.json), derived from [`AvailabilityZone-v2-extractor-sites.excalidraw`](../../Art/Maps/AvailabilityZone-v2-extractor-sites.excalidraw). `./x gen draw-availability-zone-v2` validates the JSON, measures travel on a 100 cm raster with rock clearance and renders this plan; derivation and validation procedures are in [`./x help gen`](../../x):

![Availability Zone v2 plan](../../Art/Maps/AvailabilityZoneV2-layout.png)

**Orientation:** +X is minimap-up, +Y right. The arena is 200 × 200 m at z=0; the Bunker is at (−6807, −6912) cm and the Cluster at (8281, 7825) cm. Fifteen shared-border polygons cover all 40,000 m²; ownership borders are **not** walls. Five short rock barriers provide partial cover. There are thirteen capture anchors, one per non-main region, and sixteen proposed deposits: two normal in each main and natural, two rich in each amber region. The economy proposal's *three* rich deposits per amber conflicts with the sketch's two diamonds; this data follows the drawn sites. No deposit type beyond Power or extractor yield is encoded here.

The source drawing is **not 180° symmetric**: its HQs miss a rotational mirror by 1733 cm and the two mains are 3136 versus 2139 m². Mirroring it would change the approved geography; balance must be checked in play. Map-scale fairness is an open design question.

| Index | Region | Role | Area (m²) | H → anchor (s) | J → anchor (s) |
| ---: | --- | --- | ---: | ---: | ---: |
| 0 | Human Main | main | 3136 | — | — |
| 1 | Human Near | natural | 2658 | 12.9 | 39.5 |
| 2 | West Cut | tactical | 2059 | 11.2 | 46.9 |
| 3 | Uplink | reward | 3613 | 24.9 | 37.6 |
| 4 | North Ridge | tactical | 3285 | 35.5 | 35.5 |
| 5 | Relay Plant | reward | 2997 | 38.0 | 23.1 |
| 6 | Interchange | tactical | 1942 | 27.1 | 26.4 |
| 7 | Switchback | tactical | 1923 | 24.1 | 30.9 |
| 8 | Power Yard | reward | 4805 | 27.1 | 38.9 |
| 9 | South Outcrop | tactical | 2075 | 12.6 | 48.7 |
| 10 | East Spur | tactical | 2929 | 35.4 | 30.2 |
| 11 | Cooling | reward | 1510 | 41.3 | 18.4 |
| 12 | Canal Walk | tactical | 3298 | 38.2 | 13.6 |
| 13 | Machine Near | natural | 1632 | 43.5 | 10.8 |
| 14 | Machine Main | main | 2139 | — | — |

Bunker → Cluster: **50.8 s** at 420 cm/s by the same 100 cm, eight-neighbour path estimator. These are *design estimates*, not measured Unreal Detour paths, and omit crowding and capture time. `./x gen draw-availability-zone-v2` produces the complete areas/travel table ([`./x help gen`](../../x)). The validator checks exact shared-edge topology/outer perimeter, CCW winding, 100 cm sampled tiling, symmetric neighbour lists, anchors, all 16 extractor footprints within their regions, HQ placement and rock clearance.

## Generated greybox and playtest gate

[Built] `./x gen generate-availability-zone-v2` ([`./x help gen`](../../x)) creates only `/Game/Maps/AvailabilityZoneV2`; it does not replace v1. The runner owns module freshness and generation locking.

[Built] The menu defaults to **Availability Zone v2**, with classic **Availability Zone** selectable beside it. Both **Play vs JEV** and **Host co-op** use the selection; Play Again stays on the current map. Offline menu play uses `./x play` ([`./x help play`](../../x)); cooked Development/Shipping artifacts use `./x package` ([`./x help package`](../../x)).

`Config/DefaultGame.ini` includes v2 in `MapsToCook`. Configuration alone does not prove an old package contains it; a fresh requested-map launch is required.

[Built] The generator places an arena, two HQs, 13 resource-kind `ACapturePoint`s with **SiteIndex equal to region index**, 15 native `AMapRegion`s, 16 native `ADepositSite`s, ground, rock collision and dynamic navigation. Native actors and non-colliding painted borders/deposit diamonds use the same JSON. Generation is not strategy regression proof; see [Balance.md](../Balance.md) for the later autonomous-match record and its limits. [Candidate] Height levels, destructible rocks, watchtower vision, per-player HQs and economy balance remain outside this greybox pass.
