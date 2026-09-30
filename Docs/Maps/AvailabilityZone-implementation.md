# Availability Zone: implementation plan

Companion to [AvailabilityZone.md](AvailabilityZone.md) (design) and `Build/Maps/AvailabilityZone.json` (data). Nothing here has been built. Steps are ordered so each one can fail early and cheaply. Everything the current code cannot do is a numbered **proposal (P1–P8)** owned by the gameplay team; a map that ships **Phase A** needs none of them except the harness change (P8).

Two profiles come from the same JSON (`elevation[].z_cm` and `z_sc2_cm`):

| | Plateau | Terrace | Lowland | Needs |
| --- | ---: | ---: | ---: | --- |
| **Phase A** | +5 | −90 | −90 | Nothing. Works with today's code |
| **Phase B (SC2 heights)** | +600 | +300 | 0 | P1 (ground-height code) |

## 1. Elevation approach: modular meshes, not a Landscape

**Decision: build every walkable level as a static-mesh slab, every cliff and ramp from modular kit pieces, on a flat base. No Landscape for anything the game walks on.**

Why:

1. **The game's height rules need exact, flat, low-relief surfaces.** Placement (`ValidateBuildingPlacement`) rejects any collision above +10 cm of the click plane and any navmesh more than 110 cm below it, and JEV places at z = 5. Phase A therefore uses two exact heights, +5 and −90, and every buildable pad has to be dead flat to within a few centimetres. Slabs give that by construction. A heightmap gives it only after per-pad flattening, and a ±5 cm tolerance is below the height quantisation of a 100 cm quad unless the Z scale is tuned for it.
2. **Cliffs and ramps must be exact.** Ramp width, length and slope are numbers in the JSON (700 / 1200 / 900 wide, 700 long, 7.7° in Phase A, 23.2° in Phase B). A ramp module reproduces them; a sculpt does not.
3. **Nav follows collision.** The slabs are convex, so their collision is one box or hull each. The navmesh is then predictable, and the 300 cm cliff bands are literally empty space or a rock wall, not steep terrain that Recast may or may not treat as walkable.
4. **It reuses the existing pipeline.** Blender generates meshes, Unreal Python imports and places them (`Build/GenerateEnvironmentKit.py`, `EnvKit.py`, `GenerateCampusZero.py`). Landscape creation from headless `-nullrhi` editor Python is unproven in this repo *[INFERENCE, not tested]*.
5. **Phase B is a Z change, not a rebuild.** The slab top Z and the ramp rise read `z_cm` or `z_sc2_cm`; cliff modules stack to the new height.

What a Landscape would be good for, and where it is allowed: the **out-of-play backdrop** (mountains and horizon beyond the arena, where nothing walks or builds). Keep it a separate optional layer, or use meshes.

### Region slabs

All eight regions and the Slab are convex (rectangles and chamfered octagons), so:

- One mesh per region: the polygon extruded from `z_cm` down to −300 cm, top face UV-tiled 1:1 in cm (so the world-aligned texture scale is uniform), a 12 cm chamfered lip on the top edge.
- Collision: one simple convex hull per slab (`UCX_` object in the FBX, as the kit does).
- A 300 cm **cliff band** surrounds every region that stands above a neighbour; where the neighbour is the same height in Phase A (Terrace next to a pass), the band is a **rock wall** at least 300 cm tall (a blocker). In Phase B the same band is a cliff face.

### Ramp modules

Ramps are strips 100 cm wide, 700 cm long, with a rise parameter. Place `width / 100` strips across the width (7, 9 and 12). Phase A gate and door strips have zero rise (flat thresholds); the main ramp has 95 cm. Add the side skirts (wedge walls tapering to the ramp's rise) and hazard-stripe edge trim.

## 2. New kit pieces

Same contract as `EnvKit.SPEC`: origin at the footprint centre, base at z = 0, front +X, slots `Shell`, `Dark`, `Glow`, `Accent`, FBX from Blender with the shared master material, footprint and height in the table (metres).

| Piece | Footprint × height | Slots | Purpose |
| --- | --- | --- | --- |
| `TerrainSlab` (generated per region, not a fixed piece) | polygon × (level z + 3) | Shell, Dark | The walkable levels. From `Build/GenerateMapTerrain.py` (new, Blender) |
| `CliffStraight` | 4.0 × 3.0 × 3.0 (and 6.0 for Phase B) | Shell, Dark, Accent | Edge module along a region edge; the 3.0 m plan depth is the cliff band |
| `CliffOuterCorner`, `CliffInnerCorner`, `CliffChamfer45` | 3.0 × 3.0 × 3.0 | Shell, Dark | Corners and the 350–450 cm chamfers |
| `RampStrip` | 1.0 × 7.0 × rise | Shell, Accent | One strip of a ramp; tread texture on top |
| `RampSkirt` | 0.5 × 7.0 × 0.95 wedge | Shell | Retaining wall along a ramp side |
| `RockChunk` (3 variants) | 3–6 m | Shell, Dark | Rim, ridge, blockers |
| `SpoilHeap` | 6 × 5 × 2 | Shell | Pass and pocket dressing (non-blocking cores; kept out of bays) |
| `DebrisPlug` (intact and rubble) | 10 × 10 × 3 | Shell, Dark, Accent | Destructible plug for the breach (P4); until P4 it is a static blocker or absent |
| `SurveyTower` (human), `SentinelMast` (Machine) | 2.5 × 2.5 × 9; 1.5 × 1.5 × 12 | Shell, Dark, Glow, Accent | Vision landmarks (P5). Placeable now as decoration; the Machine one keeps the single red lens |
| `SlabWall` | 4 × 0.5 × 6.5 | Shell, Dark, Glow | Cladding to complete Data Hall 0's long sides beyond `assemble_hall` |
| `BackdropRing` | 4 pieces spanning 5000 cm beyond the arena | Shell | Out-of-play horizon (the camera can pan to the arena edge) |
| `FloodlightMast`, `HaulTruck`, bunker annex, helipad pad | 0.8 × 0.8 × 8; 8 × 3.2 × 3.4; 10 × 10 × 3; 12 × 12 × 0.3 | as kit | Human base dressing |
| `BayDecal` | 2.5 × 2.5 (flat, no collision) | Accent | Painted numbered bay markings at the JSON's `bays` |

**Existing pieces reused** (`Art/Environment`): `DataHall*` and `CoolingTower` (Slab, Machine terrace), `Pylon`, `ClusterPylon`, `CommsMast`, `Transformer`, `Chiller` (Machine), `Container`, `Wreck`, `SandbagWall`, `BurnBarrel`, `GeneratorShack`, `FenceSegment`, `CableSpool` (human). No new base kit is needed for Phase A gameplay; the map is playable on box slabs before the new pieces exist.

**Textures:** all from `Art/Textures` (CC0, ambientCG): `Panel/MetalPlates002`, `Panel/MetalPlates006`, `Concrete/Concrete024`, `Concrete/Concrete016`, `Asphalt/Asphalt026A`, `Ground/Gravel004`, `Ground/Gravel006`, `SciFiFloor/Tiles108`, `Tread/DiamondPlate001`, `Hazard/PaintedMetal016`, `PaintedMetal/Metal032`. **Missing:** a cut-rock/cliff set for the rim and walls. Add one with `Build/FetchTextures.py` (choose an ambientCG rock set at that time; none is verified here) and record it in `SOURCES.md`. A terrain material (world-aligned, blended by vertex colour) and a texture import step are new too; `M_Shared` does not sample ground textures today.

## 3. Generator

New scripts, following the Campus Zero pattern (each Unreal step in its own editor process, editor otherwise closed, `-nullrhi`, and a log token):

```text
python3 Build/DrawMapLayout.py --quiet                       # fail fast: JSON checks
blender -b -P Build/GenerateMapTerrain.py -- Build/Maps/AvailabilityZone.json   # new: slabs, cliffs, ramps FBX
UnrealEditor-Cmd ... -ExecutePythonScript=Build/ImportMapTerrain.py             # new: import to /Game/Art/MapKit; token MAP_KIT_IMPORTED
UnrealEditor-Cmd ... -ExecutePythonScript=Build/GenerateAvailabilityZone.py     # new: token AVAILABILITY_ZONE_GENERATED
```

`GenerateAvailabilityZone.py`:

1. Load the JSON. Refuse to run if `DrawMapLayout.py` reports an error (import its `Analysis`).
2. Choose the profile (`--profile phaseA|sc2`, default Phase A) and read heights from `elevation`.
3. Place the **match actors** from `match_actors` exactly: one `AArenaBounds` (half extent 10000 × 10000), `FriendlyHeadquarters` team 0 at z = plateau + 110, `EnemyHeadquarters` team 5, eight `ACapturePoint` with `site_index` 0–7, `site_kind` Resource. The current `MatchLayout.place()` hard-codes Boot's coordinates; write a JSON-driven variant rather than editing it, so Boot and the tests stay untouched.
4. Place slabs, cliff/wall modules along region boundaries, ramp strips and skirts, the Slab (`assemble_hall` with size 54 × 36 m, four doors per long side), rim rocks.
5. Place decoration by `decoration[]` zone: kit lists, ground textures, density rules. **Keep-out:** every `bays` position and every sector capture ring (radius 430), every HQ disc bay, plus a 365 cm exit ring around each bay, a 600 cm corridor along each route (`routes.jev_to_human`), and every ramp. Any collision in a territory disc can block placement (the overlap box tests `WorldStatic`, `WorldDynamic` and `Pawn`).
6. Navigation: a `NavMeshBoundsVolume` at the origin, half extent (10200, 10200, 600) as in `navigation.bounds_volume`; `RecastNavMesh` with dynamic runtime generation (as `GenerateCommandMap.py` does), agent radius 35, agent height 144, max slope 44.8 (defaults; buildings are dynamic obstacles). The arena is 20 × Boot's area: **measure** navmesh build time and memory in the cooked game before accepting it.
7. Lighting like Campus Zero (dusk, `SUN_LUX`, `SKY_INTENSITY`, `EXPOSURE_BIAS` knobs): amber floodlights on the human half, cyan on the Machine half, the diagonal as the boundary.
8. Save `/Game/Maps/AvailabilityZone`; log `AVAILABILITY_ZONE_GENERATED` with counts.

Packaging: add `/Game/Maps/AvailabilityZone` to `+MapsToCook` in `Config/DefaultGame.ini` and to the README `-map=` list. `Content/Maps/*.umap` is Git LFS.

After it lands: add a row to the World.md maps table, a section to the README, and a feature-map entry for the verify skill (all of which this task deliberately did not touch).

## 4. Code proposals for the gameplay team

Nothing below is required for a Phase A playtest except P8. Each names the code that has to change and what "done" looks like.

| # | Proposal | Change | Done when |
| --- | --- | --- | --- |
| **P1** | **Ground-height-aware placement, orders and JEV** (unlocks Phase B, +600 mains) | `CursorGround` (`CommandPlayerController.cpp`): trace the ground and project to the navmesh instead of intersecting the plane z = 0; `ValidateBuildingPlacement`: take the nav-projected Z first and build the overlap box (`Z + 65`, half 55) and the ±110 sample test around **that** Z; `EnemyCommander::BuildNear` and `Front.Z = 5`, the fall-back `HQ + (−500, 0, −Home.Z + 5)`: project onto the navmesh; `ArmyGroup` attack anchors `Anchor.Z = 0` (about lines 431–434); `CommandCamera::FocusOn` and the minimap click use z = 0, which at +600 shifts the view by about 350 cm | A barracks places on +600 ground for a human and for JEV; the same JSON with `--profile sc2` passes the placement checks; the camera and minimap land on the clicked plateau point |
| **P2** | **Per-player start locations** | `AHeadquarters::StartSlot`; `ACommandGameState` holds an array of friendly HQs; `ValidateBuildingPlacement` uses the placing commander's HQ as `Home`; `HandleStartingNewPlayer` assigns the slot's HQ and `CommandPlayerController.cpp:116–120` focuses it; HUD one bar per HQ (`CommandHUD.cpp:560`), minimap already loops; `CommandGameMode::Tick` loss rule (any / all / primary: needs a decision); `EnemyCommander` assault target = nearest living HQ | Two commanders start on their own discs at `(−7500, −5500)` and `(−7500, −3700)`; territories are independent; the match ends per the chosen rule |
| **P3** | **JEV route choice, expansion and scaling** | Route waypoints from the level (`AJevWaypoint` from `routes.jev_to_human`), chosen per assault and rotated after a failed one; keep expanding after two sectors (a rule, not a fixed trigger); Defend and fall-back points relative to the plateau (toward the ramp) instead of hard-coded `−400, −250` and `−500, 0`; `GetEnemyIncomePerSecond` multiplied by commander count or a handicap | Over five 2P runs JEV uses both routes; it takes a third sector if it can; 2P income ratio matches the chosen handicap |
| **P4** | **Destructible blockers** | `ADestructibleBlocker`: health, `ReceiveAttack` (so units target it), replicated, a `UNavModifierComponent` that goes from `NavArea_Null` to the default area on death; mesh swap to rubble | The breach plugs stay closed until shot; the navmesh updates; route length through the breach matches the report (19 537 cm) |
| **P5** | **Vision towers and high ground** | Minimum: towers reveal minimap markers within `radius` for the owning team. Full: fog of war; a high-ground range or line-of-sight rule in `AArmyUnit::WeaponRange` and target acquisition (today range is `Dist2D`, so siege on the lip reaches bays A1/B1). Only meaningful after P1 | Units on the terrace do not hit the plateau's north half; a tower shows its radius |
| **P6** | **Non-square arenas** | `AArenaBounds` is a centred axis-aligned rectangle and the minimap stretches it into a square. Only needed if a future map is not square | Minimap shows the true aspect |
| **P7** | **Level-aware territory** | `Near()` is 2D; a disc over a cliff also covers ground on the other level. Optional: require the same level as the anchor | A sector's disc does not authorise building across a cliff |
| **P8** | **Harness map argument** | `.agents/skills/verify-cooprts/scripts/verify.py` (line 262) and `network.py` (lines 124, 127) hard-code `/Game/Maps/Boot`; add `--map`. `Build/MatchLayout.py` hard-codes Boot's coordinates | `verify.py regression --scenario strategy --map /Game/Maps/AvailabilityZone` runs |

## 5. Verification plan

Order matters: each tier is cheaper than the next, and none of them replaces the one after.

### Tier 0: the data (done here)

`python3 Build/DrawMapLayout.py --report` must print `Errors: none`. It has to pass in Phase A now and again with `z_cm` replaced by `z_sc2_cm` after P1 (the elevation check applies the Phase A rules, so run it against a copy with its `elevation` block set to the SC2 values and expect only the overlap-box and nav-tolerance messages).

### Tier 1: navigation reachability, in the editor or a standalone world

For each pair in the table, call `FindPathToLocationSynchronously` with the unit's agent properties from `GetDefaultNavDataInstance`; **pass:** a complete (not partial) path whose length is within ±6 % of the script's number. If a ramp or choke is narrower than designed, the path will fail or be much longer.

| From → to | Expected length (cm) |
| --- | ---: |
| Bunker → Relay Shack / Diesel Yard | 3335 / 3335 |
| Bunker → Battery Hall | 7672 |
| Bunker → Fibre Junction | 8020 |
| Bunker → Cooling Plant / Substation 7 | 11 579 / 14 337 |
| Bunker → Transformer Row / Switchyard | 16 167 / 17 549 |
| Bunker → Cluster, gate closed / door closed / open | 19 404 / 19 444 / 19 404 |
| Cluster → Transformer Row / Switchyard | 3310 / 3445 |
| Each ramp: foot → top | 700 ± 6 % |

Also for every ramp, gate, door and pocket mouth: send a fixture of **three Frontline forces of six abreast** through it and assert every unit arrives (the `movement` scenario's per-member arrival criteria) with no unit under 5 cm/s for 5 s while its order is Secure. Run for both directions on both sides.

### Tier 2: placement coverage

An automation test on the built level, in the style of the `construction` scenario: sample a 100 cm grid inside every territory disc and call `ValidateBuildingPlacement` for Barracks and Workshop from the correct team and z (0 for humans, 5 for JEV).

**Pass:** at least 90 % of the script's valid cells are valid in the level (script: Bunker 156 / 136 m² barracks / workshop, each natural 248 / 232); **no** valid cell on any ramp, gate, door or mouth; bays `A1–A4`, `B1–B4` all valid; `BuildNear` replay in a test world places three barracks and a workshop around the Cluster and an outpost at every sector.

### Tier 3: JEV smoke on the real map (needs P8)

`verify.py regression --scenario strategy --map /Game/Maps/AvailabilityZone` for one commander, observed through live state:

1. JEV places a barracks within 20 s and completes it 12 s later.
2. Transformer Row is captured, then outposted, then Switchyard (order per the score table: 7 then 8), all with 30 s of the design timeline (about 0:46 and 1:25).
3. `EnemyPlan` flips to `ASSAULT HQ` after two established sectors.
4. The first unit of the wave crosses the human **gate or door**; record which, over five runs. This is data, not a pass/fail, until P3.
5. The wave reaches the ramp foot within 60 s of leaving the Terrace (design: 42 s), with no stalled units.

### Tier 4: 1P and 2P matches (real network)

`network.py` slices with `--clients 0` (1P) and `--clients 1` (2P), then the acceptance chain once:

| Check | 1P | 2P |
| --- | --- | --- |
| Each commander builds 4 barracks in their pocket (A, B) | 4 in the Bunker | 4 + 4, all accepted, wallets independent |
| Each captures and outposts a natural | one | Relay Shack and Diesel Yard, one each; income +6/s **each** |
| Team territory is shared | – | commander 1 builds a barracks on commander 2's outposted natural |
| JEV assault is beaten at the gate/door; match ends | Cluster or Bunker destroyed | same |
| `Enter` after the result | travels to the same map (`RequestRestart` uses the current map path) | connected client follows |

### Tier 5: what only a person can check

Native window: the click-plane parallax on the plateau (about 55 cm), building preview visibility, minimap orientation and clicks, camera pan to the arena edge (backdrop present), readability of the 95 cm drop from the default zoom, and whether the two-route defence is fun. Playtesting decides route balance, JEV's strength, 2P difficulty, and whether the naturals-first opening is too safe.

## 6. Risks and unknowns

- **Readability of Phase A elevation** is the main product risk; only art and P1 change it.
- **Navmesh cost** of a 200 × 200 m dynamic navmesh with dynamic obstacles is unmeasured.
- **Camera at the edge:** the camera pans to the arena rectangle, 1350 cm past the mains; the rim and backdrop must look finished from there.
- **Crowd behaviour on a 700 cm ramp with three forces** is predicted, not measured; Tier 1 is the test.
- **Tie between routes A and B** (0.2 %) is inside the raster's error; the real navmesh may prefer one every time. If so, the map still has two routes but JEV will not use both until P3.
- **JEV never takes thirds and fourths.** Free real estate for the human by design of the current planner; may make 1P too easy.
- **Harness and generator changes** touch files outside this task (`Config`, `.agents/skills`, README, World.md); none was edited here.
