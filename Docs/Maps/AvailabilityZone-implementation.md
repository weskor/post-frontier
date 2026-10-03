# Availability Zone: implementation plan

Companion to [AvailabilityZone.md](AvailabilityZone.md) (design) and `Build/Maps/AvailabilityZone.json` (data). [Built] Phase A is the flat greybox. The dependency procedures live only in [`./x help gen`](../../x); the sections below describe outputs and acceptance criteria. [Candidate] Numbered proposals **P1–P8** belong to the gameplay team; P8 and the historical P3-income integration are noted below.

Two heights profiles come from the same JSON (`elevation[].z_cm` and `z_sc2_cm`):

| | Plateau | Terrace | Lowland | Needs |
| --- | ---: | ---: | ---: | --- |
| **[Built] Phase A, flat greybox** | 0 | 0 | 0 | Selected-map launch support (P8). Level changes are walls; ramps are open floor |
| **Phase B, kit heights** | +600 | +300 | 0 | **P1** (ground-height code) |

**Recommendation: do P1 first and build Phase B directly.** P1 is about five call sites (below), and Phase A is a throwaway greybox: the terrain kit's cliffs cannot stand at buildable heights in today's code. Build Phase A only if P1 will slip and a playtest is wanted sooner.

## 1. Elevation approach: the modular kit, not a Landscape

**Decision: assemble the walkable levels, cliffs and ramps from the terrain kit (`Art/Terrain`, `Build/GenerateTerrainKit.py`) on its 400 cm grid, on a flat base. No Landscape for anything the game walks on.**

Why:

1. **The kit already exists and matches the map.** Its grid (400 cm cells), step (300 cm, one or two steps), ramp (run 800, rise 300, walkable 700), rock gap widths (400 / 800) and 400 cm watchtower pad are the numbers the JSON is drawn on; the script fails the JSON if anything leaves the grid. The two steps (+300, +600) are exactly this map's Terrace and Plateau.
2. **Nav and placement need exact surfaces.** Ramps are a single plane at 20.56° with a 700 cm clear strip (asserted in the kit), so the navmesh is predictable. Cliff cells are solid hulls whose tops are the walkable plateau plane: no steep Landscape faces for Recast to guess about.
3. **Placement checks read collision.** `ValidateBuildingPlacement` tests overlap against `WorldStatic`; flat kit tops give a clean top surface at exactly 300 k.
4. **It reuses the pipeline.** Blender to FBX to Unreal Python import and placement (`Build/GenerateEnvironmentKit.py`, `EnvKit.py`, `GenerateCampusZero.py`). Landscape creation from headless `-nullrhi` editor Python is unproven in this repo *[INFERENCE, not tested]*.
5. **Phase B is a placement change, not new art.**

A Landscape may still be used for the **out-of-play backdrop** beyond the arena (nothing walks or builds there); keep it a separate optional layer.

### From JSON to kit pieces (the generator's job)

1. Rasterise every `regions[].poly` onto the 400 cm grid (cell `(i, j)` covers `[400 i − 200, 400 i + 200]`). Each cell gets level `k` (L0 = 0, L1 = 1, L2 = 2) or is **rim**.
2. **Rim cells** take a level one step above their highest playable neighbour (so a 3-step rim stands around each Plateau), and are never walkable from any playable cell.
3. Classify each cell by its lower orthogonal neighbours, exactly as the kit's contract lists: none → `Plateau_Fill`; one → `Cliff_Straight` facing the low cell; two adjacent → `Cliff_CornerOuter`; a low cell with plateau on two adjacent sides → `Cliff_CornerInner` (the fillet); the cells beside a ramp's top edge → `Plateau_Fill`.
4. Ramps: for each `ramps[].pieces[]` place `Ramp_Wide` (`_Machine` on the Machine half) at `centre` with `yaw`; the JSON `top`/`foot` and `footprints` document where the deck meets each level.
5. A cell at level `k` is a column: fill pieces from z = 0 up, the classified piece on top at z = 300 (k − 1) (the kit's origin is always the ground the piece stands on).
6. Human look (`SM_*`) on the south-west half, Machine look (`SM_*_Machine`) on the north-east half; the seam is the rot180 centre.
7. `proposals.breach.plugs[]` → `SM_Rocks_Destructible_Large` at `centre`, `yaw` (P4). `proposals.vision_points[]` → `SM_Watchtower` (pad centre on a cell centre; P5).
8. The Slab is the Environment kit's data hall: `EnvKit.assemble_hall` at (0, 0), 52 × 36 m (whole metres, both ≥ 6 m), doors two per long side.

### Phase A (flat greybox)

[Built] The flat profile uses the same JSON: every playable cell is at z = 0; each level boundary is a **collision wall 100 cm thick and 300 cm tall** centred on the boundary line (one box per boundary run, cliff material), ramp pieces are open floor between two short parapet boxes, and rim boxes are 600 cm tall. The script's 100 cm clearance ring already covers the walls. Phase A uses the kit only for the Slab, towers and dressing. Profile-selection procedure: [`./x help gen`](../../x).

**Phase A+ (optional, not recommended):** today's buildable band is about −95…+5 cm (see below), so a plateau could stand 95 cm above the rest with a custom low cliff piece. The kit's 300 cm pieces do not fit, and 95 cm reads as a curb next to a 120 cm unit.

## 2. Kit pieces: have and need

**In `Art/Terrain` now** (contract in `Build/GenerateTerrainKit.py`; Human and `_Machine` looks, Blender-authored, collision intent stated there and authored on the Unreal side):

| Piece | Used for |
| --- | --- |
| `SM_Cliff_Straight`, `_CornerOuter`, `_CornerInner` | Every level boundary and the rim |
| `SM_Plateau_Fill` | Plateau and Terrace interiors, the cells beside a ramp's top edge |
| `SM_Ramp_Wide` (800 × 800, walkable 700) | Main ramp (1), North gate (2), East door (1), both sides |
| `SM_Ramp_Narrow` (800 × 400, walkable 300) | **Not used**: 300 walkable is under the 540 cm three-force rule |
| `SM_Rocks_Destructible_Large` (800 gap) | Breach plugs (P4) |
| `SM_Rocks_Destructible_Small` (400 gap) | Not used; available for future pocket plugs |
| `SM_Watchtower` (400 pad, 900 tall) | 4 vision landmarks (P5) |

**Still to be made** (same Blender contract as `EnvKit.SPEC`: origin at the footprint centre, base at z = 0, front +X, slots Shell/Dark/Glow/Accent):

| Piece | Footprint × height | Purpose |
| --- | --- | --- |
| `BackdropRing` | 4 pieces spanning about 5000 cm beyond the arena | Out-of-play horizon; the camera can pan to the arena edge |
| `SpoilHeap` | 6 × 5 × 2 m | Pass and pocket dressing (kept out of bays and rings) |
| `HaulTruck` | 8 × 3.2 × 3.4 m | Human terrace |
| `FloodlightMast`, bunker annex, helipad pad | 0.8 × 0.8 × 8 m; 10 × 10 × 3 m; 12 × 12 × 0.3 m | Human plateau |
| `BayDecal` | 2.5 × 2.5 m flat, no collision | Painted numbered bays at `build_pockets[].bays` |

**Existing Environment kit** (`Art/Environment`) covers the rest: `DataHall*`, `CoolingTower`, `Pylon`, `ClusterPylon`, `CommsMast`, `Transformer`, `Chiller` (Machine) and `Container`, `Wreck`, `SandbagWall`, `BurnBarrel`, `GeneratorShack`, `FenceSegment`, `CableSpool` (human).

**Textures** (`Art/Textures`, CC0): `Panel/MetalPlates002`, `Panel/MetalPlates006`, `Concrete/Concrete024`, `Concrete/Concrete016`, `Asphalt/Asphalt026A`, `Ground/Gravel004`, `Ground/Gravel006`, `SciFiFloor/Tiles108`, `Tread/DiamondPlate001`, `Hazard/PaintedMetal016`, `PaintedMetal/Metal032`. The terrain kit's rock, plates and glow are baked into the meshes (`SC2Mask` colours through `M_Shared`); a terrain material blending the ground textures under the kit's fill tops is new, as is a texture import step, because `M_Shared` does not sample ground textures today.

## 3. Generator

Generator commands and their ordered dependencies: [`./x help gen`](../../x).

| Command | Output / purpose |
| --- | --- |
| `./x gen draw-map-layout` | JSON validation and measured layout tables; fail early on invalid data. |
| `./x gen import-terrain-kit` | Terrain FBXs in `/Game/Art/Terrain`, with authored collision; `TERRAIN_KIT_IMPORTED`. |
| `./x gen generate-availability-zone` | `/Game/Maps/AvailabilityZone`; `AVAILABILITY_ZONE_GENERATED`. |

`ImportTerrainKit.py` follows `ImportEnvironmentKit.py`: import scale 1, Convert Scene on, Force Front X Axis off, vertex colour Replace; author the simple collision the kit documents (cliffs and fill: one hull or box from z = 0 to the top; ramps: walkable deck, a hull per parapet and one for the wedge; rocks: one box; watchtower: column hull, walkable pad); assert bounds against the kit's `SPEC` table.

`GenerateAvailabilityZone.py`:

1. Load the JSON. Refuse to run if `DrawMapLayout.py` reports an error (import its `Analysis`).
2. Select the flat (Phase A) or kit-height (Phase B) profile, with heights from `elevation`; invocation options belong in [`./x help gen`](../../x).
3. Place the **match actors** from `match_actors`: one `AArenaBounds` (half extent 10000 × 10000), `FriendlyHeadquarters` team 0, `EnemyHeadquarters` team 5, eight `ACapturePoint` with `site_index` 0–7, `site_kind` Resource. Add the level height (`z_sc2_cm − z_cm`) in Phase B. `Build/MatchLayout.py` hard-codes Boot's coordinates; write a JSON-driven variant rather than editing it, so Boot and the tests stay untouched.
4. Place terrain from section 1, the Slab, rim, then dressing by `decoration[]` zone. **Keep-out:** every `bays` position and every sector capture ring (radius 430), a 365 cm exit ring around each bay, a 600 cm corridor along each route (`routes.jev_to_human`), and every ramp footprint. Any collision in a territory disc can block placement (the overlap box tests `WorldStatic`, `WorldDynamic`, `Pawn`).
5. Navigation: a `NavMeshBoundsVolume` at the origin, half extent (10200, 10200, 1400), as in `navigation.bounds_volume`; `RecastNavMesh` with dynamic runtime generation (as `GenerateCommandMap.py` does), agent radius 35, height 144, max slope 44.8 (defaults). The arena is about 5 × Boot's area (40 000 m² against 8100): **measure** navmesh build time and memory in the cooked game. `AArenaBounds::HalfHeight` is 1000: units on the Plateau stand at about +690, inside it.
6. Lighting as Campus Zero (dusk; `SUN_LUX`, `SKY_INTENSITY`, `EXPOSURE_BIAS`): amber floodlights on the human half, cyan on the Machine half.
7. Save `/Game/Maps/AvailabilityZone`; log `AVAILABILITY_ZONE_GENERATED` with counts.

Packaging uses `./x package` ([`./x help package`](../../x)); `Config/DefaultGame.ini` owns the cook map list. The generated map is `Content/Maps/AvailabilityZone.umap` (Git LFS).

## 4. [Candidate] Code proposals for the gameplay team

Nothing below is required to *start* a Phase A greybox except P8. Each names the code that has to change and what "done" looks like.

[Built] P8 added map selection to verification launchers (Boot remains their default). The historical integration checkpoint exercised strategy on Boot and AvailabilityZone plus eight-sector connected restart in editor and packaged worlds. Its income observations predate the later region/Extractor economy and are not current balance guidance; see [Balance.md](../Balance.md). Menu is the separate frontend world. [Candidate] Route choice, height-aware placement and per-player HQs remain the proposals below, not proof supplied by generation.

| # | Proposal | Change | Done when |
| --- | --- | --- | --- |
| **P1** | **[Candidate] Ground-height-aware placement, orders and JEV** (unlocks Phase B: Terrace +300, Plateau +600) | `CursorGround` (`CommandPlayerController.cpp`): trace the ground and project to the navmesh instead of intersecting the plane z = 0; `ValidateBuildingPlacement`: take the nav-projected Z first and build the overlap box (`Z + 65`, half 55) and the ±110 sample test around **that** Z; `EnemyCommander::BuildNear` (`Location.Z = 5`), `Front.Z = 5`, the fall-back `HQ + (−500, 0, −Home.Z + 5)`: project onto the navmesh; `ArmyGroup` attack anchors (`Anchor.Z = 0`, about lines 431–434); `CommandCamera::FocusOn` and the minimap click use z = 0, which at +600 shifts the view by about 350 cm | A barracks places on +600 ground for a human and for JEV; the kit-height profile passes placement checks; the camera and minimap land on the clicked plateau point |
| **P2** | **Per-player start locations** | `AHeadquarters::StartSlot`; `ACommandGameState` holds an array of friendly HQs; `ValidateBuildingPlacement` uses the placing commander's HQ as `Home`; `HandleStartingNewPlayer` assigns the slot's HQ and `CommandPlayerController.cpp:116–120` focuses it; HUD one bar per HQ (`CommandHUD.cpp`); `CommandGameMode::Tick` loss rule (any / all / primary: needs a decision); `EnemyCommander` assault target = nearest living HQ | Two commanders start on their own discs at `(−7400, −5500)` and `(−7400, −3700)`; territories are independent; the match ends per the chosen rule |
| **P3** | **JEV route choice, expansion and scaling** | Route waypoints from the level (`AJevWaypoint` from `routes.jev_to_human`), chosen per assault and rotated after a failed one; keep expanding after two sectors (a rule, not a fixed trigger); Defend and fall-back points relative to the plateau (toward the ramp) instead of hard-coded `−400, −250` and `−500, 0`; `GetEnemyIncomePerSecond` scaled by commander count or a handicap | Over five 2P runs JEV uses both routes; it takes a third sector if it can; the 2P income ratio matches the chosen handicap |
| **P4** | **Destructible blockers** (matches `SM_Rocks_Destructible_*`: "the destroyed actor replaces the mesh; rubble has no collision") | `ADestructibleBlocker`: health, `ReceiveAttack` (so units target it), replicated, a `UNavModifierComponent` that goes from `NavArea_Null` to the default area on death; mesh swap to rubble | The breach plugs stay closed until shot; the navmesh updates; the route through the breach matches the report (18 970 cm) |
| **P5** | **Vision towers and high ground** | Minimum: towers reveal minimap markers within `radius` for the owning team. Full: fog of war; a high-ground range or line-of-sight rule in `AArmyUnit::WeaponRange` and target acquisition (today range is `Dist2D`, so siege on the lip reaches bays A1–B2). Only meaningful after P1 | Units on the Terrace do not hit the plateau's north half; a tower shows its radius |
| **P6** | **Non-square arenas** | `AArenaBounds` is a centred axis-aligned rectangle and the minimap stretches it into a square. Only needed if a future map is not square | Minimap shows the true aspect |
| **P7** | **Level-aware territory** | `Near()` is 2D; a disc over a cliff also covers ground on the other level. Optional: require the same level as the anchor | A sector's disc does not authorise building across a cliff |
| **P8** | **[Built] Harness map selection** | Selected-map readiness fails rather than accepting fallback; startup/HUD/restart use the selected map's sector count. Boot coordinate fixtures remain deliberately Boot-specific. Network and native/desktop checks use `./x verify` ([`./x help verify`](../../x)) | The historical checkpoint exercised eight-sector editor/package connected restart; this is not proof of subsequent gameplay changes |

**Where the height limit comes from** (the reason for P1): a footprint overlap box `Location.Z + 10 … + 120` (centre +65, half 55) tests world collision, and `Location.Z` is **0** for a click (`CursorGround` forces z = 0) and **5** for JEV. Ground higher than about +10 cm blocks every building on it. The navmesh sample must project within 110 cm of the same Z, so ground lower than about −95 cm is unplaceable. Buildable relief is therefore about 100 cm, less than one 300 cm kit step.

## 5. Verification plan

Order matters: each tier is cheaper than the next, and none of them replaces the one after.

### Tier 0: the data (done here)

`./x gen draw-map-layout` ([`./x help gen`](../../x)) validates the data and reports `Errors: none` for the Phase A layout. Phase B's L1 +300 and L2 +600 exceed the original buildable ceiling; that is a proposed placement change, not a JSON validation error.

### Tier 1: navigation reachability, in the editor or a standalone world

[New] Proposed navigation assertion: each pair below has a complete (not partial) path with the unit's navigation-agent properties and length within ±6 % of the script's number. If a ramp or choke is narrower than designed, the path fails or is much longer. This map-specific assertion matrix is not an existing test scope; interactive map inspection uses `./x editor` ([`./x help editor`](../../x)).

| From → to | Expected length (cm) |
| --- | ---: |
| Bunker → Relay Shack / Diesel Yard | 3730 / 3478 |
| Bunker → Battery Hall | 7403 |
| Bunker → Fibre Junction | 7642 |
| Bunker → Cooling Plant / Substation 7 | 11 591 / 14 248 |
| Bunker → Transformer Row / Switchyard | 15 941 / 16 768 |
| Bunker → Cluster, door closed (route A) / gate closed (route B) / open | 19 031 / 19 032 / 19 032 |
| Cluster → Transformer Row / Switchyard | 3456 / 3852 |
| Each ramp strip, top → foot | 800 ± 6 % |

[New] Proposed crowd assertion: three Frontline forces of six abreast traverse every ramp lane, gate lane and pocket mouth in both directions, with every unit arriving and no unit below 5 cm/s for 5 s while its order is Secure. The 100 cm parapet between the gate's two lanes is not crossable; lanes are independent cases. Existing fixture movement checks use `./x test movement` ([`./x help test`](../../x)); they do not implement this map-specific matrix.

### Tier 2: placement coverage

[New] Proposed placement assertion on the built level: a 100 cm sample grid in every territory disc compares Barracks/Workshop acceptance for the correct team and ground height. Existing lifecycle checks use `./x test construction` ([`./x help test`](../../x)); they do not implement this coverage scan.

**Pass:** at least 90 % of the script's valid cells are valid in the level (script: Bunker 156 / 136 m² barracks / workshop, each natural 248 / 232); **no** valid cell on any ramp footprint or mouth; bays `A1–A4`, `B1–B4` all valid; a `BuildNear` replay in a test world places three barracks and a workshop around the Cluster and an outpost at every sector.

### Tier 3: JEV smoke on the real map (needs P8)

Strategy automation on this map uses `./x test strategy --map /Game/Maps/AvailabilityZone` ([`./x help test`](../../x)); the selected map overrides the scope's configured world and must start for a pass. [Candidate] The following one-commander timeline is the original map-specific acceptance target, not additional assertions guaranteed by the strategy scope:

1. JEV places a barracks within 20 s and completes it 12 s later.
2. Transformer Row is captured, then outposted, then Switchyard (order per the score table: 7 then 8), all within 30 s of the design timeline (about 0:46 and 1:25).
3. A JEV force publishes an Attack plan on the human HQ's region after two established sectors (`ACommandGameState::EnemyPlans`).
4. The first unit of the wave crosses the human **gate or door**; record which, over five runs. This is data, not a pass/fail, until P3.
5. The wave reaches the ramp foot within 60 s of leaving the Terrace (design: 40 s), with no stalled units.

### Tier 4: 1P and 2P matches (real network)

Network slices and the continuous acceptance chain use `./x verify network` ([`./x help verify`](../../x)); topology and map selection are described only in help. [Candidate] The table below describes the original 1P/2P map-playtest target, not additional assertions supplied by the runner:

| Check | 1P | 2P |
| --- | --- | --- |
| Each commander builds 4 barracks in their pocket (A, B) | 4 in the Bunker | 4 + 4, all accepted, wallets independent |
| Each captures and outposts a natural | one | Relay Shack and Diesel Yard, one each; income +6/s **each** |
| Team territory is shared | – | commander 1 builds a barracks on commander 2's outposted natural |
| JEV assault is beaten at the gate or door; match ends | Cluster or Bunker destroyed | same |
| `Enter` after the result | travels to the same map (`RequestRestart` uses the current map path) | connected client follows |

### Tier 5: what only a person can check

Native input/capture verification uses `./x verify native` ([`./x help verify`](../../x)); human play uses `./x play` ([`./x help play`](../../x)). [Candidate] Human review covers building preview and Plateau clicks, minimap orientation, camera-edge dressing, level readability and whether the two-route defence is fun. It decides route balance, JEV strength, 2P difficulty and whether the naturals-first opening is too safe.

## 6. Risks and unknowns

- **Phase A readability**: a flat map with walls is a greybox, not the SC2 look. The kit pays off in Phase B.
- **Navmesh cost** of a 200 × 200 m dynamic navmesh with dynamic obstacles is unmeasured.
- **Camera at the edge**: the camera pans to the arena rectangle, 1400 cm past the mains; the rim and backdrop must look finished from there.
- **Crowd behaviour on a 700 cm ramp lane with three forces** is predicted, not measured; Tier 1 is the test.
- **Ties between routes A and B** (0.01 %) are inside the raster's error; the real navmesh may prefer one every time. If so the map still has two routes, but JEV will not use both until P3.
- **JEV never takes thirds and fourths**: free real estate for the human by design of the current planner; may make 1P too easy.
- **Kit cell classification** at the pocket and pass corners produces concave and convex corners on both sides of the same cliff line; `check_assembly` in the kit only covers its own preview layout, so assert seams on the real map.
- **Outside the map-generation lane**: that lane did not edit `Config`, `.agents/skills`, README or World.md. Subsequent P8/P3-income integration changed the default-map config, harness and README as recorded above; World.md remains unchanged.
