# Availability Zone

*"Guaranteed 99.99% uptime. Humans are counted as downtime."*

First real map: **you (and later a friend) against JEV**, laid out the way a StarCraft 2 ladder map is: a main on high ground behind one ramp, a natural just below it, exposed thirds and fourths, chokes, open fields, and two attack routes of equal length. **Design only.** Nothing here has been built in Unreal, and the numbers marked *[INFERENCE]* come from reading the code, not from a run.

| File | Role |
| --- | --- |
| `Build/Maps/AvailabilityZone.json` | Source of truth: arena, HQs, sectors, elevation regions, ramps, blockers, chokes, pockets and bays, decoration zones, proposals. The Unreal generator reads the same file. |
| `Build/DrawMapLayout.py` | Validates the JSON, measures walking distances, replays the placement rules, draws the plan. `python3 Build/DrawMapLayout.py --report` prints every table quoted below. |
| `Art/Maps/AvailabilityZone-layout.png` | Top-down plan: grid, scale and time bars, elevation shading, ramps, sectors, HQs, routes, chokes, pockets. |
| `Docs/Maps/AvailabilityZone-implementation.md` | How to build it in Unreal, the code proposals, the verification plan. |

![Layout plan](../../Art/Maps/AvailabilityZone-layout.png)

Orientation follows the game's minimap and camera: **+X is up on screen ("north"), +Y is right ("east")**, Z is up. The Bunker is south-west, the Cluster north-east. `EnvKit.py` uses a different compass for hall doors; this document always means the minimap.

## At a glance

| | |
| --- | --- |
| Arena | `AArenaBounds.HalfExtent` = (10000, 10000): **200 × 200 m**, square so the square minimap is not stretched. About 38 % is walkable (15 100 m²); the rest is rock rim. |
| Symmetry | 180° rotation about the origin. Every sector, ramp and choke has a partner at (−x, −y); the script proves it cell by cell. |
| Levels | **Phase A (today's code): Plateau +5 cm, everything else −90 cm**: a 95 cm drop around each main, and the Terrace is walled off from the passes rather than stepped. Target after code proposal P1: Lowland 0, Terrace +300, Plateau +600. |
| Bases | Bunker (human main) `(-7500, -4600)`, Cluster (JEV main) `(7500, 4600)`, both on a Plateau with **one 700 cm ramp**. |
| Sectors | 8: two naturals per side, a third, a fourth. Nothing sits in the exact centre. |
| Routes | Two attack routes, **19 444 cm and 19 404 cm (46.3 s and 46.2 s, 0.2 % apart)**, ending at different human entrances. |
| Bunker → Cluster | 194 m walking, **46 s** at 420 cm/s. |
| Narrowest passage | Main ramp, 700 cm = 3.9 force widths. |
| Sector income at stake | +6/s per commander per established sector (README), so 8 sectors are worth up to +48/s each. |

## Setting

A hyperscale campus was being graded into terraces when the Machine stopped needing the contractors. The Offline dug in on the south pad and hung floodlights on the retaining walls. The Machine kept building on the north pad, one data hall at a time, and one very large hall in the middle of the valley: **Data Hall 0, the Slab**, which is what makes the map two lanes wide.

Sector names use the World.md power-infrastructure vocabulary. One joke per surface: the name carries it, the HUD role line stays plain.

| # | Sector | Side | Role (plain) | Flavour line |
| ---: | --- | --- | --- | --- |
| 1 | Relay Shack | Human | Safe natural | "Two bars of signal and a kettle." |
| 2 | Diesel Yard | Human | Exposed natural | "Smells like uptime." |
| 3 | Battery Hall | Human | Third | "Charged. Not by us. Yet." |
| 4 | Fibre Junction | Human | Fourth | "Their cable. Our cable now." |
| 5 | Cooling Plant | Machine | Fourth | "Keeps forty thousand GPUs from noticing us." |
| 6 | Substation 7 | Machine | Third | "Substations 1 to 6 have been reassigned." |
| 7 | Transformer Row | Machine | Exposed natural | "Steps power down. Steps people down too." |
| 8 | Switchyard | Machine | Safe natural | "Please hold. Your call is important to the grid." |

The number is `SiteIndex + 1`, the HUD's `SECTOR n`. Index `i` and `7 − i` are rot180 partners.

## SC2 layout language, as built

| SC2 idea | On this map | Code support |
| --- | --- | --- |
| Main on high ground, one ramp | Plateau 2100 cm deep, 4200 wide; one 700 cm ramp; nothing else touches it. Phase A drop is only 95 cm (see below) | Elevation is geometry, but buildable ground must stay between about −105 and +10 cm, see [Code assumptions](#code-assumptions-that-shaped-the-design) |
| Natural just below the main | Two naturals on the Terrace, **7.9 s** from the Bunker, 4.5 s from the ramp foot | Yes: sectors and outposts already work at any Z the navmesh reaches |
| Natural choke | **North gate**, 1200 wide, plus an **East door**, 900 wide; in Phase A they are flat gaps in a tall rock wall | Yes |
| Third and fourth, progressively more exposed | Third in a pocket behind a 1000 choke (18.3 s from the Bunker, 34 s from the Cluster); fourth in the open pass (19.1 s from the Bunker, **27.5 s** from the Cluster) | Yes |
| Wide-open fields | West Pass and East Pass, 54 × 47 m each | Yes |
| Chokepoints | Main ramp 700, pocket mouth 1000, door 900, gate 1200 | Yes |
| Multiple attack paths | Route A: Cluster › West door › NW pocket › West Pass › North gate. Route B: Cluster › South gate › East Pass › SE pocket › East door. Same length; different human entrance | Geometry yes. JEV's choice between them is **not** designed by code, see [JEV on this map](#jev-on-this-map) |
| Watchtower vision points | 4 towers (Survey Tower W/E, Pocket Tower SE/NW) | **No.** There is no fog of war or vision system. Listed as proposal P5; the meshes can still stand there as landmarks |
| Destructible rocks | A 1000 wide **breach** through the Slab plugged with two rock plugs, joining the two pockets | **No.** Nothing in the code has hit points and toggles navigation. Proposal P4. Phase A ships the Slab solid |
| Static rock blockers | The Slab and the rock rim | Yes: static collision; nav mesh follows |
| Enemy main opposite with its own ramp and natural | Cluster north-east, its own ramp, gate, door, two naturals | Yes |

## Size and travel times

Unit speed is `MaxWalkSpeed = 420` cm/s (`ArmyUnit.cpp`), so **10 s = 4200 cm** (the second bar on the plan). The README fixes only the match arc, roughly 12–18 minutes, and the economy (10/s plus 6/s per established sector) sets the pace inside it, not distance. Distance decides two things: how expensive a losing push is (recruits walk the whole way to their moving formation; there are no teleports) and when JEV's first wave lands. The targets below give a 46 s base-to-base crossing, 2.7 × Boot's straight-line 17 s, and single-digit-second hops between a base and its naturals.

Measured on a 100 cm raster with line-of-sight smoothing, 100 cm clearance ring, 16-neighbour Dijkstra. Real Detour paths will differ by a few percent; crowds and formation offsets add more *[INFERENCE: not measured]*.

| Leg | Length | Time | Target |
| --- | ---: | ---: | --- |
| Bunker → main ramp top | 1050 cm | 2.5 s | under 3 s |
| Ramp foot → Relay Shack or Diesel Yard | 1900 cm | 4.5 s | about 5 s |
| Ramp foot → gate top / door top | 2750 / 3022 cm | 6.5 / 7.2 s | about 7 s |
| Gate top ↔ door top (across the Terrace) | 3467 cm | 8.3 s | the width of one response window |
| **Bunker → natural** | 3335 cm | **7.9 s** | 8 s |
| Bunker → third (Battery Hall) | 7672 cm | 18.3 s | 15–20 s |
| Bunker → fourth (Fibre Junction) | 8020 cm | 19.1 s | 15–20 s |
| Bunker → Cluster's exposed natural | 16 167 cm | 38.5 s | 35–45 s |
| **Bunker → Cluster** | 19 404 cm | **46.2 s** | 45 s |
| Cluster → human gate foot / door foot | 14 376 / 14 418 cm | 34.2 / 34.3 s | equal |

The Cluster's row is the mirror of the Bunker's (Cluster → Transformer Row 3310 cm, 7.9 s; → Switchyard 3445 cm, 8.2 s).

## Elevation and ramps

Three levels, one step between neighbours, cliff or wall bands 300 cm thick where two levels meet (the script fails if two different levels touch or come within 300 cm without a ramp).

| Level | Phase A z | SC2-height z | What lives here |
| --- | ---: | ---: | --- |
| L2 Plateau | +5 | +600 | Mains (858 m² each) |
| L1 Terrace | −90 | +300 | Naturals (1786 m² each) |
| L0 Lowland | −90 | 0 | Passes (2538 m²), pockets (2352 m²) |

**Why the Phase A drop is only 95 cm, and why the Terrace is not a step.** Two rules in `ACommandGameState::ValidateBuildingPlacement` and its callers fix the buildable height band (every HQ disc and every sector disc is buildable, so they all have to sit in it):

- The footprint overlap test is a box `Location.Z + 10 … + 120` (centre +65, half height 55), and `Location.Z` is **0** for a player click (`CursorGround` forces z = 0) and **5** for JEV (`BuildNear`). A collision surface higher than about +10 cm blocks every footprint on it. **So no buildable ground may be higher than +5 cm.**
- The navmesh sample must project within 110 cm of the same Z. **So none may be lower than about −95 cm** (the script keeps a 15 cm margin for the navmesh cell height).

That leaves about 100 cm of relief in total. Two 50 cm steps would sit at the character's 45 cm `MaxStepHeight`, where crowd pushes can climb a ledge, so the plateau takes the whole drop (95 cm, twice the step height) and the Terrace shares the lowland's height. The Terrace's edge is therefore a **300 cm thick rock wall, not a drop**, with the gate and door as flat gaps in it (the ramp rows below show "flat gap" for them). The plan shows both levels because SC2 heights need them, and they are separate regions in the JSON.

Reading it: a 95 cm drop is a curb next to a 120 cm tall unit. Phase A relies on material breaks, lip trim, contact shadow, and tall rock walls (blockers, so any height) at every level change; see Themes. This is the single biggest limit the code puts on the "SC2 look" and the reason proposal P1 exists.

After P1 (click Z from the ground, placement box and JEV Z from the navmesh) the same file switches to the SC2-height column (Lowland 0, Terrace +300, Plateau +600) with no change to the layout; the Terrace edge becomes a real cliff and the gate and door become real ramps.

| Ramp (both sides identical) | Width | Length | Phase A slope (rise) | SC2 slope (rise) | Role |
| --- | ---: | ---: | ---: | ---: | --- |
| Main ramp, Plateau → Terrace | 700 | 700 | 7.7° (95 cm) | 23.2° (300 cm) | The only way into a main |
| North gate, Terrace → Lowland | 1200 | 700 | flat gap | 23.2° (300 cm) | Front choke |
| East door, Terrace → Lowland | 900 | 700 | flat gap | 23.2° (300 cm) | Side choke |

**Sizing against the units.** Capsule radius 34, half-height 60 (`ArmyUnit.cpp`). A produced force uses columns 110 apart and rows 110 apart (`AArmyGroup::FormationOffset`, `bProducedGroup`): **180 cm wide, 290 cm deep** for a Frontline force of six, less for the smaller forces. Rules used:

- Every passage on an army route is at least **3 force widths (540 cm) plus the navmesh's agent erosion (35 per side)**: the narrowest is the 700 cm main ramp (630 cm navigable).
- Doors and gates: 900 and 1200, so two and three forces abreast never queue.
- The last 103 cm of a recruit's path disables predictive avoidance (`PrecisionRadius = 2 × radius + 35`), so no ramp corner may have a turning radius under 300 cm. Regions have chamfered corners (350–450 cm) and the arena's pocket and pass corners are rounded in the art.
- The Phase A main ramp at 7.7° is far under the 12° recommendation and the character's 44.7° walkable limit. At SC2 heights lengthen ramps to 1000 cm (16.7°).
- Ramp side skirts: because a ramp is 700 long and the cliff band is 300 thick, the ramp is bordered by short retaining walls tapering from 95 cm to 0. They are collision, not ground.
- **Nothing on a ramp or at a choke can be walled off**: no ramp or named choke lies inside any territory disc. Tightest margins: main ramp extension 50 cm beyond the HQ disc (its real top edge, 1050 cm from the HQ, is 150 cm beyond), East door 100 cm beyond Diesel Yard's disc. Buildings are navigation obstacles here (`NavArea_Null`), so this matters: territory circles are what would let a player plug a choke.

## Sector list

Territory: HQ 900, sector 1000 (`ACapturePoint::TerritoryRadius`); capture ring 430. Every capture ring and every territory disc except the Bunker's and Cluster's lies fully on one level (the script checks the ring and reports coverage).

| # | Sector | Position (x, y) | Level | Role | Why here |
| ---: | --- | --- | --- | --- | --- |
| 1 | Relay Shack | (−4900, −6300) | Terrace | Safe natural | Deep behind the North gate. 4.5 s from the ramp foot. The production site for a second commander |
| 2 | Diesel Yard | (−4900, −2900) | Terrace | Exposed natural | Beside the East door: route B passes 348 cm from it |
| 3 | Battery Hall | (−6300, 1400) | Lowland | Third | In the SE pocket behind a 1000 wide mouth. Route B passes 2800 cm away, so it is not swept automatically |
| 4 | Fibre Junction | (400, −3600) | Lowland | Fourth | Open field 425 cm off route A. Any JEV push on route A passes it |
| 5 | Cooling Plant | (−400, 3600) | Lowland | Fourth (Machine) | Rot180 of 4, on route B |
| 6 | Substation 7 | (6300, −1400) | Lowland | Third (Machine) | Rot180 of 3 |
| 7 | Transformer Row | (4900, 2900) | Terrace | Exposed natural (Machine) | Rot180 of 2 |
| 8 | Switchyard | (4900, 6300) | Terrace | Safe natural (Machine) | Rot180 of 1 |

Distances from each HQ are in the plan's sector table. The progression the brief asks for shows up in the distance from the *enemy* HQ: naturals 38–42 s, thirds 34 s, fourths 27–28 s.

### Headquarters, plateau and pockets

| | Bunker | Cluster |
| --- | --- | --- |
| Position (x, y) | (−7500, −4600) | (7500, 4600) |
| HQ actor z (Phase A) | 5 + 110 = 115 | 115 |
| Plateau | x −8550…−6450, y −6700…−2500 | rot180 |
| Plateau edge to HQ | 1050 cm north and south, 2100 east and west | same |
| Ramp top edge to HQ | 1050 cm | same |

The HQ disc (radius 900) sits fully on the plateau with ≥ 150 cm to spare; buildings need their nav samples at footprint + 65 cm on navigable ground, so about the outer 200 cm of the disc is unusable and the plateau is sized so that is not a loss on the sides.

**Build pockets.** The Bunker disc is split into two halves by the line `y = −4600`: **Pocket A** (west) and **Pocket B** (east), with four authored barracks **bays** each (`A1–A4`, `B1–B4`, radius 750 from the HQ, at least 476 cm apart, all passing the placement rules and none in the ramp's approach). The disc is an unclaimed shared space in today's code, so the pockets are a **convention made legible by paint**: numbered bay markings on the deck. Measured capacity (100 cm cells, greedy packing at 305 cm hard spacing / 550 cm practical spacing that keeps an exit lane):

| Zone | Barracks cells | Barracks packed (hard / practical) | Workshop cells |
| --- | ---: | ---: | ---: |
| Bunker disc | 156 m² | 18 / 7 | 136 m² |
| Each pocket half | 78 m² | – / 4 | 68 m² |
| Each natural, third, fourth | 248 m² | 24 / 10 | 232 m² |

Each of JEV's needs is met too: `AEnemyCommander::BuildNear` (4 rings at 360, 510, 660, 810 cm, 12 directions, at z = 5) finds valid spots for **three barracks and one workshop** around the Cluster, and for an outpost at every sector.

## How it plays with the current code

### One player

- The Bunker disc holds four to seven barracks, all in one place. Start with two (440 of the 600 opening resources).
- Take Relay Shack, then Diesel Yard, in the first minute. Both are 4.5 s off the ramp foot, and the Terrace's two entrances are 8.3 s apart: **hold the ramp foot** (6.5 s to the gate, 7.2 s to the door) instead of either choke.
- Battery Hall is quiet behind its mouth. Fibre Junction is the greedy expansion: it sits on route A.
- Realistically **three sectors by about 2:15, four by about 4:00** if the first JEV wave is beaten *[INFERENCE]*.

### Two players, same map, no rebuild

The level places exactly what one player needs and two players fit without changes: `HandleStartingNewPlayer` gives each commander a slot (0–4), a private wallet and a camera; every commander builds anywhere in team territory; income is per commander. The layout adds what a second person needs:

- **Two pockets** (A west, B east) in the shared Bunker disc, four bays each.
- **Two naturals with different jobs**: Relay Shack is the safe production site, Diesel Yard is the front. A friend takes one each, or swaps.
- **Two entrances to guard** (gate and door), which are also two natural fronts: one commander sets barracks fronts on the gate, the other on the door.
- **No new code**: nothing here needs a second HQ. All players share the Bunker, the win condition (destroy the Cluster) and the loss condition (lose the Bunker).

Honest limits of the shared-HQ arrangement (all from the code):

- **No claim system.** The server only checks territory, overlap, navigation and money, not which commander stands where. Two players can build over each other's bays; the pockets are a courtesy, and the bay paint makes the courtesy visible.
- **Same camera start.** `bInitialFocusPending` focuses the friendly HQ for every commander. Both start on the Bunker.
- **JEV does not scale.** Its income is one wallet: 10/s plus 6/s per established sector. Two humans earn twice that baseline. With three sectors each the team makes 2 × 28 = 56/s against JEV's 22/s. Expect 2P to be easy without proposal P3.
- **Space.** 2P needs about eight barracks plus workshops. The Bunker disc holds seven practically, so the second player's production lives on the naturals (10 practical each).

### PROPOSAL: per-player start locations (code the gameplay team would own)

*A proposal, not a promise, and not required for 2P testing.* Give each commander their own start HQ in the same plateau. The plateau is already 4200 cm wide for this: two HQ discs of radius 900 centred `(−7500, −5500)` and `(−7500, −3700)` tile it with ≥ 300 cm margin, and the ramp stays on the centre line between them. The two entries `per_player_start` in the JSON give the positions.

What it needs (P2 in the implementation doc): `AHeadquarters` gets a `StartSlot`; `ACommandGameState` holds an array of friendly HQs; `ValidateBuildingPlacement` uses the placing commander's HQ; the camera focuses on the owner's HQ; HUD shows one bar per HQ; the loss rule needs a decision (any HQ, all HQs, or the primary). Benefits: real ownership of a base, no pocket courtesy, a clean 3–5 player story. Costs: a second HQ is a second target, and a second `AHeadquarters` currently only logs an error (`InitGameState`) and is ignored.

## JEV on this map

Facts from `EnemyCommander.cpp` that decide the map's role:

- **JEV builds around its HQ**, and at fixed z = 5, in rings of 360 to 810 cm. Its plateau therefore has to be flat, open and at most +10 cm above z = 5. It is at +5.
- **Target score** for each sector is `(5 if neutral or Machine-held, 3 if human-held) − straight 2D distance / 1200`, not path length, so cliffs do not count. Ranked from the Cluster:

  | Sector | Straight | Neutral | Human-held |
  | --- | ---: | ---: | ---: |
  | 7 Transformer Row | 3106 | 2.41 | 0.41 |
  | 8 Switchyard | 3106 | 2.41 | 0.41 |
  | 6 Substation 7 | 6119 | −0.10 | −2.10 |
  | 5 Cooling Plant | 7963 | −1.64 | −3.64 |
  | 4 Fibre Junction | 10 847 | −4.04 | −6.04 |

  7 and 8 tie; the strict `>` keeps the first in `SiteIndex` order, so **JEV goes for Transformer Row first** (the exposed natural), then Switchyard.
- **Assault trigger**: two established sectors, or a living force of 12 (6 + 4 + 2). With two naturals established JEV **stops expanding and marches on the Bunker**, so it never takes Substation 7 or Cooling Plant unless a natural falls. Those two are free ground for the human, and so is the rest of the map.
- **Approach route**: Detour's shortest path, always. With today's code JEV cannot choose a lane.
- **Defend / retreat points** are hard-coded offsets from its HQ: Defend at `HQ + (−400, −250)` and fall back to `HQ + (−500, 0)`. They point toward −x, i.e. toward the Bunker, on its own plateau. **This map only works if the Bunker is on JEV's −x side.** It is: the Cluster's plateau extends 1050 cm south of the HQ.
- **Intruder rule**: it switches to Defend when human units stand within 1500 cm of its HQ. The Cluster's terrace lip is 1350 cm away, so a raid on the lip triggers the defence; a raid on the natural (3310 cm) does not.

### Expected timeline *[INFERENCE from the rules, unmeasured]*

| Time | JEV | Human (efficient) |
| --- | --- | --- |
| 0:02 | First planner tick places a barracks | Place a barracks around 0:05 |
| 0:14–0:17 | Barracks done (12 s); production starts | Barracks done ~0:17 |
| ~0:35 | Six Frontline out; first unit at Transformer Row ~0:27, capture 8 s | First unit at Relay Shack ~0:28, capture 8 s |
| ~0:46 | Outpost (9 s) up: 1 established | Outpost up ~0:47 |
| ~1:25 | Switchyard established: 2 established, plan flips to **assault HQ** at the next 12 s commit | Diesel Yard by ~1:10 |
| ~1:35–1:45 | Wave leaves (6–12 units) | Battery Hall ~1:40–2:00 |
| ~2:10–2:20 | Wave at the Terrace, 34 s of walking (gate and door 34.2 and 34.3 s) | Ramp foot held |

A human rush leaving the barracks at ~0:40 reaches Transformer Row 38.5 s later, about 1:20, before JEV's second sector.

### Attack routes

Closing one human entrance forces the other, so the script measures both. **Route A (North gate) 19 444 cm, route B (East door) 19 404 cm: a 0.2 % gap.** By symmetry they are equal, and the raster resolves them to within its own noise, so **which one JEV uses is not decided by this map**. Crowd offsets, per-unit start positions and navmesh tile choices are all bigger than 0.2 %; expect either, possibly a split. Two real facts stay true regardless: a defender who sits on one choke does not cover the other (8.3 s apart across the Terrace), and JEV cannot be starved into a single route by building, because no choke is buildable.

What the map alone **cannot** promise is that JEV will *rotate* routes over several waves or flank on purpose. That needs P3 (waypoints or route choice in the planner); authored waypoints for both routes are already in the JSON (`routes.jev_to_human`).

## Chokes, fields, blockers, vision

| Feature | Position | Size | Status |
| --- | --- | --- | --- |
| Main ramp | (−6300, −4600) | 700 | phase A |
| North gate | (−2850, −4600) | 1200 | phase A |
| East door | (−4900, −1550) | 900 | phase A |
| Pocket mouth SE | (−2700, 2300) | 1000 | phase A |
| Mirrors of the four above | rot180 | same | phase A |
| West Pass, East Pass | (0, ∓4150) | 5400 × 4700 | phase A |
| The Slab (Data Hall 0) | (0, 0) | 5400 × 3600 | phase A, solid |
| Rock rim | everything outside the walkable regions | – | phase A |
| Breach through the Slab, plugs at each end | x −2700…2700, y −500…500 | 1000 wide | **proposal P4** |
| 4 vision towers | (0, ∓2300), (∓3300, ±2300) | radius 2500 | **proposal P5** |

The breach, if opened, adds a third route from the SE pocket through the Slab into the NW pocket: 19 537 cm, 46.5 s, 0.7 % longer than the other two, so the planner would still prefer A or B. That is a mid-game map change: whoever blasts a plug decides when the map opens.

## Themes per area

Kit pieces are in `Art/Environment/` (`EnvKit.SPEC`); textures are the CC0 set in `Art/Textures` (`SOURCES.md`). *Proposed* new pieces are marked and specified in the implementation doc. The master material `M_Shared` and the colour language in World.md apply: dark environment, saturated colour only for team, glow and power.

| Area | Look | Ground | Kit | New pieces |
| --- | --- | --- | --- | --- |
| **Bunker plateau** (human main) | Prefab forward base on landing struts; painted bay numbers; floodlights along the lip | `Panel/MetalPlates002`, `Concrete/Concrete016`; ramp trim `Tread/DiamondPlate001` | `GeneratorShack`, `Container`, `SandbagWall`, `BurnBarrel`, `FenceSegment`, `CableSpool` | Bunker annex, `FloodlightMast`, helipad pad |
| **Human Terrace** | Graded pad, haul roads, hazard-striped edge | `Concrete/Concrete024` pads, `Asphalt/Asphalt026A` roads; lip trim `Hazard/PaintedMetal016` (worn only) | `Container`, `Wreck`, `SandbagWall`, `FenceSegment`, `BurnBarrel`, `Transformer` | `HaulTruck`, `SpoilHeap` |
| **Battery Hall pocket** | Half-dug battery bank, cable runs | `Ground/Gravel006`, `Asphalt026A` | `Chiller`, `Transformer`, `CableSpool`, `Wreck`, `Container` | `SpoilHeap` |
| **Cluster plateau** (Machine main) | The Cluster's lit plaza, pearl deck, cyan seams, ring of `ClusterPylon` | `SciFiFloor/Tiles108`, `Panel/MetalPlates006`; trim `PaintedMetal/Metal032` | `ClusterPylon`, `Pylon`, `CommsMast`, `Transformer`, `Chiller` | `SentinelMast` |
| **Machine Terrace** | The Machine campus: server halls, cooling towers, cyan cables to every sector | `Concrete024`, `Tiles108` inlays | `DataHallBay/Door/Corner/Roof`, `CoolingTower`, `Chiller`, `Transformer`, `Pylon` | `SentinelMast` |
| **Substation 7 pocket** | Transformer yard, chain of pylons | `Concrete024`, `Asphalt026A` | `Transformer`, `Pylon`, `Chiller`, `CableSpool`, `CoolingTower` | – |
| **Passes** | Dry lakebed and haul road, wrecks and spoil for scale, no cover that blocks | `Asphalt026A`, `Gravel006`, `Gravel004` patches | `Wreck`, `Container`, `CableSpool`, `Pylon` | `SpoilHeap`, `RockChunk` |
| **The Slab** | Data Hall 0: one huge hall, 54 × 36 m, cooling towers on the roof | – | `DataHallBay/Door/Corner/Roof` via `assemble_hall`, `CoolingTower` | `SlabWall` cladding |
| **Rock rim and backdrop** | Cut rock, retaining walls, dark; sun and sky visible beyond it | `Gravel004`, `Concrete016`; a rock/cliff set is **not** in the library | – | `CliffStraight`, `CliffCorner`, `RockChunk`, `BackdropRing` |

**Making 95 cm read like SC2.** Hard material break at every level change, a bright chamfered lip trim on the high side, a dark contact-shadow band and a vertical fascia on the low side, tall rock walls (non-walkable, any height) around the Terrace and the passes, a cliff face that visibly overhangs the band, and the sun at 45° so every wall casts a shadow. Props in bays and territory discs must stay off the footprint: any collision there blocks placement. The human side is amber-lit and gunmetal, the Machine side cyan-lit and pearl. Faction colour splits the map along the diagonal.

## Balance notes

- **Rush distance**: 194 m, 46 s. JEV's first wave lands about 2:10–2:20; a human rush at 0:40 can be at JEV's exposed natural at 1:20 and, if it stands on the lip, forces a Defend.
- **What each side can hold**: human realistically 4 sectors by mid-game, 5–6 if it pushes; JEV **2** (its naturals) because it assaults at two. That is 34/s per commander with four sectors (46/s with six) against JEV's 22/s. Whether this is a fair fight depends on JEV's force sizes, not on the map: the numbers show why JEV wants help (P3).
- **What JEV contests first**: Transformer Row, then Switchyard. It does not contest the middle at all until it assaults, and it never picks Cooling Plant or Substation 7 while two sectors are established.
- **2P vs 1P**: 2P doubles the human income against a fixed JEV wallet (see above). If the friend test feels trivial, that is the cause, not the map.
- **Ranged and siege ignore height.** `WeaponRange` is a 2D distance and there is no line-of-sight or high-ground rule (`ArmyUnit.cpp`). Siege (range 1150) standing on the Terrace lip (x = −6150) reaches any building whose centre is north of x = −7300: bays A1 and B1 (x = −6982) are in reach, A2/B2 (−7450) and the HQ (−7500, 1350 cm from the lip) are not. Ranged (560) reaches nothing on the plateau from the lip. *[INFERENCE from the range check]* Proposal P5 (high ground) would fix this properly.
- **Intruder margin**: 1350 vs 1500 cm. The 150 cm margin is what makes a raid on the lip trigger the Defend behaviour, so do not deepen the plateau.

## Code assumptions that shaped the design

| Assumption in the code | Where | What it does to a map | How this map copes |
| --- | --- | --- | --- |
| **One shared friendly HQ**, one enemy HQ | `CommandGameMode::InitGameState`, `ACommandGameState::FriendlyHeadquarters`, `ValidateBuildingPlacement` (`Home`) | A second team-0 HQ only logs an error. All commanders share one 900 cm disc. Only one HQ bar in the HUD | One HQ, two pockets, two naturals; per-player start is proposal P2 |
| **Territory is 2D**: HQ 900 cm, sector 1000 cm (only after capture *and* outpost) | `Near()` in `ValidateBuildingPlacement`; `ACapturePoint::TerritoryRadius` | Discs ignore height and cliffs and can plug a choke; capture rings ignore cliffs | Discs sit on one level; no ramp or choke lies in any disc (checked) |
| **Rectangular, origin-centred arena**, `\|z\| ≤ 1000` | `AArenaBounds::ContainsTravel`, `HalfHeight` | Non-rectangular play areas are made with rock and navmesh, not bounds. The minimap is square and stretches a non-square arena | Square arena; rock rim; rim dressing needed because the camera can pan to the edge |
| **Clicks are on the plane z = 0** and forced to z = 0 | `ACommandPlayerController::CursorGround`, `CommandCamera::FocusOn`, `CommandMinimap` | A placement or front on any level is evaluated at z = 0 | Buildable levels stay within about −95…+5 cm; the camera parallax at 95 cm is about 55 cm (a preview circle sits that far off the cursor on the plateau) |
| **Placement validation**: overlap box `Location.Z + 10…120` against world collision; nav samples within 110 cm of `Location.Z`, extents (45, 45, 200), samples at footprint + 65 | `ValidateBuildingPlacement` | **Ground higher than +10 cm blocks every building on it**; ground lower than −110 cm is unplaceable; buildings need about 200 cm of navigable margin from cliffs | Plateau +5, the rest −90; the script replays the rules (`placement_ok`, `check_elevation`) |
| **JEV builds and marches at z = 5**, rings 360–810, Defend `−400, −250`, fall back `−500, 0` | `EnemyCommander.cpp` (`BuildNear`, `Front.Z`) | JEV cannot use ground far from z = 5. Its offsets fix which side of its HQ the plateau must extend | z ∈ {−90, +5}; Cluster on the north-east with the Bunker at −x |
| **JEV chooses by straight distance** and assaults at 2 sectors or 12 units | `EvaluatePlan` | Path length and cliffs are invisible; JEV never takes the third or fourth | Naturals are nearest; the rest is free ground |
| **JEV always takes the shortest nav path** | Detour | One lane per wave; two equal routes give no guaranteed split | Equal routes plus proposal P3 |
| **No vision, no fog, no high-ground rule**; range is 2D | `ArmyUnit::WeaponRange` | Watchtowers and cliff advantages do nothing in code | Proposals P5; Phase A uses cliffs only for movement |
| **Buildings are nav obstacles; none can be destroyed except by fire; finished ones cannot be cancelled** | `ACommandBuilding` (`NavArea_Null`) | Walls and plugs are possible in territory; nothing in code opens a path later | No choke in a disc; breach is proposal P4 |
| **JEV wallet does not scale with player count** | `ACommandGameState::GetEnemyIncomePerSecond` | 2P is easier by construction | Noted; proposal P3 |
| **Five commanders, slots 0–4** | `PreLogin`, `HandleStartingNewPlayer` | No per-slot start, spawn or camera position | Same start for all |
| **Maps are hard-coded in the harness** | `verify.py` (`/Game/Maps/Boot`), `network.py`, `Build/MatchLayout.py` | The new map cannot be smoke-tested until the harness takes a map argument | Listed in the implementation doc |

## Data file and regeneration

```bash
python3 Build/DrawMapLayout.py --report          # all checks and tables; exit status 1 on any failure
python3 Build/DrawMapLayout.py                   # rewrites Art/Maps/AvailabilityZone-layout.png
python3 Build/DrawMapLayout.py --svg /tmp/az.svg # also keep the SVG
```

Needs `rsvg-convert` or ImageMagick, no Python packages. Checks: level overlap and cliff bands, ramp ends and slopes, ramp widths of at least three force widths, sectors and capture rings on one level, HQ discs on their plateau, no ramp or choke inside a territory disc, choke widths, rot180 symmetry, reachability, route equality within 3 %, the human terrace unreachable without gate and door, placement coverage, bays, JEV's `BuildNear` replay and outposts, level height against the two build heights, and terrace lip within JEV's intruder radius.

## Questions for the owner

1. **Elevation profile.** Today's code allows only a 95 cm drop around each main (Plateau +5, everything else −90). Is that acceptable for the first playable, or should art wait for proposal P1 so the mains can be +600?
2. **JEV routes.** Are two equal routes enough, or is a lane choice in `EnemyCommander` (P3) a precondition for calling the map done?
3. **Second start.** Do you want the per-player start proposal (P2) built before the friend test, or is the shared Bunker with paint bays enough for the first co-op session?
4. **JEV at two players.** Should JEV's income scale with the commander count for 2P, or should the first friend test show the raw difference?
5. **Names.** The map name and sector names are placeholders in the World.md tone; confirm or replace them before art and HUD copy depend on them.
