# Post-Frontier — Map Structure Research (regions, tiles, variants, spheres)

Scope: whether to keep 15 organic polygon regions on a 200 × 200 m map, switch to snap-together modular regions, or use a hex sphere (Goldberg look). Also covers the playtest bug where forces on Hold did not defend a building inside their own region.
Format per entry: **Mechanic → Why it works (evidence) → Applicability → Pitfall**. My own extrapolation is tagged `[INFERENCE]`.

---

## 1. Organic polygon regions (territory games)

**1.1 Company of Heroes: sectors + supply chain**
- **Mechanic:** The map is split into sectors, and you take a sector by capturing its strategic point. Income only counts if "sectors must be connected all the way back to HQ, those that are not will not give its cache" ([CoH wiki: Resources](https://companyofheroes.fandom.com/wiki/Resources)). Wikipedia calls sector supply "a departure from mainstream RTS" ([Wikipedia](https://en.wikipedia.org/wiki/Company_of_Heroes)).
- **Why it works:** Every sector is a node in a connectivity graph, so cutting one link starves everything behind it. That makes flanking and the shape of the front line strategic, not just tactical.
- **Applicability:** This is almost exactly Post-Frontier's supply rule, and it is proven in shipped RTS. CoH maps are handcrafted and fixed. Sector shapes follow roads, hedgerows and buildings, so they are irregular `[INFERENCE from the maps]`.
- **Pitfall:** On a graph with many links per node, cutting supply almost never happens. Cuts need narrow necks (degree-2 regions) near the back line `[INFERENCE]`.

**1.2 Dawn of War: strategic points + listening posts**
- **Mechanic:** Most requisition comes from capturing strategic points and building listening posts on them ([DoW wiki: Getting Started](https://game.wiki/dawn-of-war/getting-started)).
- **Why it works:** Income sits at visible points, which pulls fights to places everyone can see.
- **Applicability:** Supports Post-Frontier's capture anchors. A fortifiable structure on the anchor gives Hold a natural place to idle (see §6).
- **Pitfall:** Points with no region semantics lead to "dot hopping". Post-Frontier's regions avoid this only if the region, not just the anchor, matters in fights.

**1.3 Northgard: tiles with one defining trait each**
- **Mechanic:** Each random map is made of "Tiles" (zones). Each zone has a building capacity of 2–4 and **one base "area trait" per tile**, plus optional extra resources. The centre tile is drawn from a pool ([Northgard wiki: Tile](https://northgard.fandom.com/wiki/Tile)). A player analysis finds that move orders are only allowed inside your "Zone of Influence": your connected safe tiles plus hostile tiles next to them ([Steam guide](https://steamcommunity.com/sharedfiles/filedetails?id=3116873079)).
- **Why it works:** One trait per zone means you can read a zone at a glance. Zone-level movement slows attackers down and gives defenders time. A PC Gamer quote credits the map generator for each game being "fat with potential" ([northgard.net](https://northgard.net/game)).
- **Applicability:** Northgard is the closest shipped analogue to Post-Frontier: zone-level orders, region traits and random maps. Copy the rule of **one primary trait per region**, with deposits as secondary tags.
- **Pitfall:** Northgard tiles are small and many. With 15 large regions, a single trait per region is a bigger swing and needs a fairness budget (§3).

**1.4 Total War / Dune: Spice Wars: provinces with special regions**
- **Mechanic (Total War):** Provinces hold "key settlements … that cannot defend themselves" ([TW Academy](https://academy.totalwar.com/the-province-system)).
- **Mechanic (Dune: Spice Wars):** Every region has a village to annex. Mountains, Deep Desert (supply drain, −20 % speed) and main bases are regions you cannot capture ([Dune wiki: The Map](https://dunespicewars.fandom.com/wiki/The_Map)).
- **Why it works:** Regions you cannot capture shape the adjacency graph (chokepoints) without walls. Undefended settlements force a real garrison-or-respond decision.
- **Applicability:** Post-Frontier can get chokepoints with "impassable" or "hazard-only" regions instead of extra terrain. This is also a cheap per-seed variant lever (§4).
- **Pitfall:** Too many dead regions shrink the playable graph. In 15 regions, 1–2 is the most that fits `[INFERENCE]`.

## 2. Hex and cell grids, and the hybrid

**2.1 Civilization V hexes**
- **Mechanic:** Civ V moved to hexes. Sid Meier: "in a square grid, some distances are longer than others, it's not clear whether the corners connect … It makes the graphics look more natural … coastlines and rivers" ([Kotaku](https://kotaku.com/casting-a-hex-on-civilization-v-453108223)).
- **Why it works:** Every neighbour is the same distance away and there is no diagonal ambiguity, so movement and adjacency are unambiguous.
- **Applicability:** Post-Frontier has continuous movement on a navmesh, so the distance argument barely applies. Only the **adjacency** argument carries over, and organic polygons already give unambiguous shared-edge adjacency.
- **Pitfall:** A pure hex region grid gives every region 6 neighbours by default. That is lots of fronts and no natural chokepoints `[INFERENCE]`.

**2.2 Red Blob Games: hex maths**
- **Mechanic:** Cube/axial coordinates give simple neighbour, distance (`(|dq|+|dr|+|ds|)/2`), ring and line algorithms ([Red Blob: Hexagonal Grids](https://www.redblobgames.com/grids/hexagons/)). Hexes "offer less distortion of distances than square grids" ([Amit's grid notes](http://www-cs-students.stanford.edu/~amitp/game-programming/grids/)).
- **Why it works:** The tooling is trivial: map storage, rotation and mirroring for fairness, and procedural placement.
- **Applicability:** Useful if regions are *built from* sub-cells (§2.3) or if a meta-map is hex-based. Little use for 15 hand-placed polygons, where adjacency is just a list of 15 nodes and their links.
- **Pitfall:** The maths is cheap, but the art and readability cost of hex-shaped borders on 3D terrain is not.

**2.3 Endless Legend / Humankind: regions made of hex tiles (hybrid)**
- **Mechanic:**
  - Endless Legend's world "is formed of separate regions"; one city claims a whole region, and each region can host only one city ([Wikipedia](https://en.wikipedia.org/wiki/Endless_Legend)).
  - Humankind territories are fixed, randomly generated, multi-tile, and **one biome per territory**. Border style shows the claim state: white dashed = unclaimed, coloured dashed = outpost, solid = city ([Humankind wiki](https://humankind.fandom.com/wiki/Territory)).
- **Why it works:** Tiles handle local maths. Regions handle strategy and legibility, and their outlines look organic because they are irregular groups of hexes.
- **Applicability:** This is the best "modular but organic" model for Post-Frontier. Let a fine hidden cell grid (hex or irregular) define region membership, and generate or edit borders as groups of cells. Use **border styling for state** (neutral / contested / owned / supplied).
- **Pitfall:** Borders built from cells zig-zag. In 3D they need smoothing so they read as lines, not staircases `[INFERENCE]`.

**2.4 Polytopia: square grid with guarantees**
- **Mechanic:** Every map is a random square grid. Land is "distributed roughly equally between all tribes", and on Lakes maps "every player is guaranteed to spawn with land connections to at least two villages" ([Polytopia wiki: Map Generation](https://polytopia.fandom.com/wiki/Map_Generation)).
- **Why it works:** Random layout is paired with **explicit, testable guarantees** about what each player can reach.
- **Applicability:** Write Post-Frontier's guarantees the same way. For example: "main region is linked to ≥ 2 capturable deposit regions within 2 hops", and "JEV start is ≥ 3 hops from any player main".
- **Pitfall:** Guarantees only cover what you thought to write down. Playtest for new degenerate cases.

**2.5 Dorfromantik: edge-matched hexes**
- **Mechanic:** A "simple, yet very scalable core mechanic": hexes that score on matching edges ([Game Developer](https://www.gamedeveloper.com/business/sparking-joy-through-tile-placement-in-idyllic-village-builder-i-dorfromantik-i-)).
- **Applicability:** Shows that **edge-socket rules** are enough to keep tiled landscapes coherent. The same principle applies to snapping macro-tiles (§4, Option B).
- **Pitfall:** Dorfromantik has no opponent, so its coherence rules do nothing for fairness.

## 3. Shuffled modular maps and fairness constraints

**3.1 Catan: shuffled hexes with one adjacency veto**
- **Mechanic:** The official variable setup randomises tokens, but "the tokens with the red numbers must not be next to each other. You may have to swap tokens" ([Catan base rules PDF](https://www.catan.com/sites/default/files/2021-06/catan_base_rules_2020_200707.pdf)). There is also a fixed beginner layout.
- **Why it works:** One cheap local rule removes the worst outcome, a hotspot.
- **Applicability:** Add an identical rule for deposits: **no two high-yield deposits in adjacent regions**, and no high-yield deposit adjacent to a JEV start. Keep one fixed "beginner seed" per map.
- **Pitfall:** Local vetoes do not ensure global fairness. You still need distance-based checks (§3.3).

**3.2 Twilight Imperium 4: players build the map**
- **Mechanic:** Players place system tiles in snake order, ring by ring. Anomalies "cannot be placed next to one another unless there is no other option", and neither can matching wormholes. In 5-player games the disadvantaged seats get trade goods as **compensation** ([TI4 Rules Reference PDF](https://images-cdn.fantasyflightgames.com/filer_public/c2/69/c269b9e2-8d9a-420b-a807-2b164dd54977/ti-k0289_rules_referencecompressed.pdf)).
- **Why it works:** The structure (rings, fixed home slots, a central objective) is fixed. Only the *contents* of slots vary, under adjacency vetoes. When geometry cannot be fair, a resource payout makes up the difference.
- **Applicability:** Post-Frontier can use **fixed region slots with shuffled contents**, adjacency vetoes and compensation: a bonus starting resource if a seed gives the players a worse deposit layout.
- **Pitfall:** In a PvE co-op game, "fair" means *fair to the players versus JEV*, not between seats. Budget the challenge, not symmetry.

**3.3 Age of Empires II: random map scripts**
- **Mechanic:** Script commands place resources per player with distance bands, for example `set_place_for_every_player`, `min_distance_to_players`, `max_distance_to_players` and `find_closest` ([Forgotten Empires RMS features](https://www.forgottenempires.net/age-of-empires-ii-definitive-edition/rms-features); [aoe2-rms docs](https://docs.racket-lang.org/aoe2-rms/attributes.html)). A map author notes that the older group-distance method "didn't always produce consistent results" while `set_place_for_every_player` is "much more effective" ([AoE forum](https://forums.ageofempires.com/t/some-random-map-scripts-ive-created/221035)). DE added `enable_balanced_elevation` because hills were biased to the south of the map ([aoe2-rms docs](https://docs.racket-lang.org/aoe2-rms/attributes.html)).
- **Why it works:** Placement is *relative to each player* within distance bands, not uniformly random.
- **Applicability:** Place deposits by **hop distance from the main region in the supply graph**, not by world position. Example: tier-1 deposit at 1 hop, tier-2 at 2–3 hops, contested tier-3 between the players and JEV.
- **Pitfall:** Generators hide biases (the south-hill bug). **Batch-generate about 1,000 seeds and histogram the stats** (hops to the nearest deposit, JEV distance, number of contested links) before shipping `[INFERENCE]`.

**3.4 Spelunky: room templates in a 4×4 grid**
- **Mechanic:** Each level is a 4×4 grid of rooms, with "only about 50 basic room layouts for every tile set". Rooms are "80 % hand crafted and 20 % randomly generated", while traps and monsters are "100 % procedural". Veterans learn to recognise the rooms, but replayability comes "far more" from the obstacle generation ([Tiny Subversions](http://tinysubversions.com/2009/09/spelunkys-procedural-space/index.html)).
- **Why it works:** Handcrafted chunks keep quality high. A guaranteed path through the grid keeps levels solvable. The variety players actually feel comes from what fills the space.
- **Applicability:** This is strong evidence for Post-Frontier's current plan: **freshness comes from reshuffling contents (deposits, traits, objectives, JEV start and behaviour), not from new geometry.**
- **Pitfall:** Players learn ~50 templates per tileset after a few hundred plays. Three maps get learned much faster, so contents and variant layers must carry the variety.

**3.5 Into the Breach and Risk of Rain 2: handcrafted pools with variants**
- **Into the Breach:** Each island is regenerated per timeline "with different region names, terrains, and bonus objectives. The only consistent variable is their biome, the **region layout**, and the controlling corporation" ([ITB wiki: Islands](https://intothebreach.fandom.com/wiki/Islands)). Within missions, special facilities replace a civilian building in the map ([ITB wiki: Missions](https://intothebreach.fandom.com/wiki/Missions)).
- **Risk of Rain 2:** Each environment has "three or more stage variations, with most changes being single features, such as a door being opened to reveal extended areas" ([RoR2 wiki](https://riskofrain2.fandom.com/wiki/Environments)).
- **Why it works:** A fixed authored layout gives readability and mastery. Swapping slots and toggling single features makes runs feel different at low content cost.
- **Applicability:** This is the closest match to "3 handcrafted maps reshuffled per seed". Add **2–4 authored variant toggles per map that change adjacency edges**: a bridge out, a pass open, a hazard region spreading. Edge changes alter the supply graph, which is what makes the strategy feel new.
- **Pitfall:** Variants that only change cosmetics (props, weather) are not noticed as variety `[INFERENCE]`.

**3.6 They Are Billions: themed procedural survival maps**
- **Mechanic:** Survival maps are procedurally generated per theme ([TAB archive wiki](https://theyarebillions-archive.fandom.com/wiki/Maps)). Swarms spawn at the edges and path to the Command Center ([TAB wiki: Swarms](https://they-are-billions.fandom.com/wiki/Swarms)).
- **Applicability:** A PvE "enemy comes from map edges toward the core" model works with random terrain *because the direction of threat is fixed*. JEV start regions should likewise come from an authored *set of directions* per map, not any region.
- **Pitfall:** Fully procedural terrain produces bad seeds (blocked or overly exposed bases), and players notice.

## 4. Organic look on a structured grid

**4.1 Stålberg: Bad North (WFC) and Townscaper (irregular quads)**
- **Mechanic:**
  - Bad North builds islands with a bespoke 3D Wave Function Collapse, a constraint solver over adjacency rules. Each tile also "knows its navigability", which guarantees a path from the beach to the houses ([Game Developer / AI and Games](https://www.gamedeveloper.com/game-platforms/how-townscaper-works-a-story-four-games-in-the-making)).
  - Bad North uses **square grids**. Townscaper switched to a relaxed **irregular quad grid** "to create more natural and realistic geometry". Stålberg: "You can skew the pieces quite a lot before it starts to become a problem" (same source).
- **Why it works:** Standardised modules survive procedural placement because constraints ensure valid neighbours. Irregularity in the grid hides the module seams.
- **Applicability:** If Post-Frontier goes modular, **put socket rules (open / ridge / river / road) on module edges and carry gameplay metadata (navigability, capture anchor, defend posts) in each module.** An irregular grid is a good way to make a *hidden* cell layer that reads as organic.
- **Pitfall:** WFC handles local validity only. Global goals (fairness, JEV distance, supply necks) need a separate validator, as Bad North's late-added navigability check shows.

**4.2 Red Blob: Voronoi / polygon map generation**
- **Mechanic:** Choose points (Poisson disc or Lloyd-relaxed), build Voronoi cells, and keep **two graphs**: Delaunay for adjacency/pathfinding and Voronoi for shapes/border rendering. Polygons give "distinct player-recognizable areas … territory to conquer … pathfinding waypoints, difficulty zones" ([Polygonal Map Generation](http://www-cs-students.stanford.edu/~amitp/game-programming/polygon-map-generation/)). Red Blob has also shaped Voronoi noise into a PvP strategy layout with team build areas and mountain passes ([Red Blob](https://www.redblobgames.com/x/1638-voronoi-strategy-mapgen/)).
- **Applicability:** Post-Frontier's 15 regions already form this dual structure: the supply chain is the Delaunay-like graph and the borders are the Voronoi-like graph. Relaxed Voronoi is a good **authoring tool**: seed points on designer-placed anchors, relax, then hand-edit borders onto ridges and rivers.
- **Pitfall:** Raw Voronoi borders ignore terrain. A border crossing open ground with no visual cue is the classic readability failure `[INFERENCE]`.

## 5. Spherical worlds

**5.1 Planetary Annihilation: RTS on small planets**
- **Evidence against:**
  - IGN: "the curvature of these tiny globes means that you can still only see a fraction of the playing surface before you have to spin the camera … It's a fog-of-war you can never dispel; even when you have radar coverage of an entire planet, your situational awareness is severely reduced". IGN found the picture-in-picture view helped; verdict: "low on playability thanks to the central gimmick" ([IGN, 4.8](https://www.ign.com/articles/2014/09/17/planetary-annihilation-review)).
  - PC Gamer could not work out how to rotate the camera ([PC Gamer](https://www.pcgamer.com/planetary-annihilation-review/)).
- **Evidence for:** GameSpot: globes add "a new tactical dimension", but "all opponents must do to flank is to simply run around the globe", which forced defences "all the way around" ([GameSpot, 7](https://www.gamespot.com/reviews/planetary-annihilation-review/1900-6415935/)). RPS: planets take "a few minutes to circumnavigate", and raiders "circle your HQ" ([RPS](https://www.rockpapershotgun.com/planetary-annihilation-review)).
- **Developer commentary:** None found on the readability trade-off; the public material covers planet generation and flow-field pathfinding ([PC Gamer](https://www.pcgamer.com/planetary-annihilation-devs-show-planet-creation-tech-clever-unit-pathfinding)).
- **Applicability:** A sphere maximises frontage. In a no-micro game where Hold means "defend this whole region", a front with no edges multiplies the regions under threat. That clashes with the core loop, and it hurts solo players most `[INFERENCE]`.

**5.2 Populous: The Beginning: a fake sphere**
- **Mechanic:** "While the terrain's topology is a torus, the map is locally projected onto a sphere to give the illusion of a planet." The camera rotates 360° ([Wikipedia](https://en.wikipedia.org/wiki/Populous:_The_Beginning)).
- **Applicability:** You can get the *look* of a planet (horizon curvature) on a flat or wrapped grid with no pentagons. A curved-horizon shader on the flat battlefield is a cheap aesthetic nod `[INFERENCE]`.

**5.3 Hexasphere / Goldberg maths**
- **Mechanic:** A Goldberg polyhedron "always has exactly 12 pentagonal faces" and has 10T + 2 faces in total ([Wikipedia](https://en.wikipedia.org/wiki/Goldberg_polyhedron)).
- **What that means at Post-Frontier's region counts** (arithmetic from that formula):

  | Total faces | Pentagons | Pentagon share |
  |---|---|---|
  | 12 | 12 | 100 % |
  | 32 | 12 | 38 % |
  | 42 | 12 | 29 % |
  | 72 | 12 | 17 % |

  At 15–40 regions the "hex sphere" is largely pentagons.
- **Camera:** Red Blob notes that sphere tiling leaves twelve pentagons, and that "you can't have a camera that's always facing north. It either needs to rotate freely or it needs to flip upside down when you pass the pole" ([Red Blob](https://www.redblobgames.com/x/1640-hexagon-tiling-of-sphere/)).
- **Shipped example:** Before We Leave, a slow, non-violent city builder, ships "hexagonal lands and planets" ([Steam](https://store.steampowered.com/app/1073910/Before_We_Leave/)). Hex spheres work for low-pressure play.
- **Pitfall:** Pentagon regions have 5 neighbours. That is a hidden asymmetry in supply and frontage that players will find.

**5.4 Sphere as the run-level map**
- **Evidence:** Into the Breach prototyped a large-scale world strategy layer and cut it as "too board gamey – too abstract", "didn't care about cities", "dull to chase enemies", "long game". It then went to "small islands" ([ITB GDC postmortem slides](https://media.gdcvault.com/gdc2019/presentations/Into%20the%20Breach%20Postmortem%20Final.pdf)). Populous: The Beginning's campaign moves planet to planet ([Wikipedia](https://en.wikipedia.org/wiki/Populous:_The_Beginning)).
- **Applicability:** A small planet as the **node map** (4–6 battles, each a site on the globe) gives the Goldberg aesthetic where pentagons, camera and frontage don't matter: you just click nodes. Keep it small and concrete, and show which act or map each node uses.
- **Pitfall:** A big, wandering strategic globe repeats ITB's failed prototype. It also costs UI time that a node graph doesn't.

## 6. Region size, shape and the "Hold didn't defend" bug

**6.1 Leash behaviour in shipped RTS**
- **Evidence:** AoE II's Defensive stance: units "will follow for a certain number of tiles before returning to their original position"; Stand Ground units do not move to attack ([AoE wiki: Unit stance](https://ageofempires.fandom.com/wiki/Unit_stance)). The leash is anchored to **the unit's own idle point**, not to an area.
- **Applicability:** Post-Frontier's bug is exactly a point-anchored leash applied to an area order. A Hold order needs **region-scoped targeting**: any hostile inside the region polygon, or any hostile damaging a friendly structure in the region, is a valid target. "Fight only inside the region" then becomes a *polygon test*, not a radius around the idle point `[INFERENCE]`.

**6.2 Sizing arithmetic** `[INFERENCE: geometry, not sourced]`
- 200 × 200 m split into 15 regions averages about **2,670 m² per region**. A compact (disc-like) region has an equivalent radius of **~29 m**; a regular hex of that area has a ~32 m circumradius.
- A 2:1 elongated region of the same area (~73 × 37 m) reaches **~41 m** from its centre and **~82 m** corner-to-corner from an off-centre anchor.
- Rule of thumb: **every buildable spot must be within (acquisition range + ~3–5 s of travel) of at least one Hold idle post.** With a single central post that means a ~35–40 m response radius. For elongated or L-shaped regions, author **2–3 defend posts** (near the anchor and near build pads), or bound compactness.
- Total War's undefended settlements ([TW Academy](https://academy.totalwar.com/the-province-system)) are the deliberate version of this tension. If it is not deliberate, it reads as a bug.

**6.3 Map-structure fixes for the bug**
- (a) Each region carries authored **defend posts** and **build pads**. Hold idles across the posts, weighted toward structures (the DoW listening-post analogue, §1.2).
- (b) Add a **compactness constraint** to the map validator: the maximum distance from any build pad to its nearest post must be ≤ R.
- (c) A "structure under attack" event pulls Hold forces in that region.
- (d) Show the response radius as a faint ring while issuing Hold.
- None of this needs modular tiles. Option A (§7) covers it.

## 7. Map-structure options for Post-Frontier

| Option | What it is | Gains | Costs / risks |
|---|---|---|---|
| **A. Handcrafted + slot shuffle + edge variants** (recommended) | Keep 3 organic maps. Per seed, shuffle deposits, traits, objectives and JEV start from constrained pools (Catan veto, AoE hop bands, TI compensation). Add 2–4 authored toggles per map that add or remove adjacency edges (RoR2 / ITB model). | Most readable; borders follow authored terrain; fairness is verifiable over ~15 nodes; fixes the Hold bug with authored posts. | Maps get memorised. Variety must come from contents, edge toggles and JEV behaviour (Spelunky lesson). |
| **B. Macro-tile assembly** | Handcrafted chunks of 2–4 regions (~60–70 m) snap into a coarse socket layout with edge sockets (Dorfromantik / WFC), plus a global validator (connectivity, JEV hops, deposit parity). | New supply graphs each run; scales content beyond 3 maps. | Unreal landscape seams, blending, lighting and navmesh rebuild per seed. Macro-composition feels less authored. Needs the validator from day one (Bad North lesson). |
| **C. Generated region partition over fixed terrain** | Keep 3 terrains. Re-partition regions per seed with relaxed Voronoi or cell groups (Humankind-style), snapping borders to ridges, rivers and roads. | New region graphs with no new art. | Borders may not match terrain cues (readability). Region sizes vary, breaking the Hold radius. Best used as an *authoring tool*, not at runtime. |
| **D. Planet meta-map + flat battlefields** | A small Goldberg/hex globe as the run's node map; battles stay flat (optional curved-horizon shader, Populous-style). | Delivers the friend's "hex sphere" look where it costs nothing in gameplay. | UI and art cost; must stay small and concrete (ITB's cut strategy layer). |
| ✗ **Sphere battlefield** | Regions on a Goldberg sphere. | Novelty; flanking dimension. | 29–38 % pentagon regions at 32–42 faces; no-north camera; partial visibility (IGN); frontage everywhere (GameSpot), which conflicts with Hold-the-region. Not recommended. |

Suggested path: **A now**, with a validator and authored defend posts. Add **D** for flavour if wanted. Keep **C** as an editor tool for drafting new region layouts. Revisit **B** only if more than 3 maps per act are needed.

## 8. Answers to the five questions
1. **Snap-together tiles: fair, readable and handcrafted?** Yes, at *macro-chunk* granularity with edge sockets and a global validator (TI4 rings and vetoes, Spelunky's template grid, Bad North's WFC with navigability). Every shipped example keeps a fixed skeleton and varies the contents, and adds compensation when geometry can't be fair (TI4).
2. **Organic vs hex vs irregular grid:**
   - Organic polygons give the best border readability (they can follow terrain) and allow variable degree, so chokepoints and supply necks are possible.
   - Hex grids give uniform degree 6: many fronts, few chokepoints, and staircase borders.
   - Irregular or cell-group regions are a good hidden layer for authoring and generation (Humankind, Townscaper).
   - Pathing is unaffected either way: units path on the navmesh and regions are a logical overlay `[INFERENCE]`.
3. **Size vs unit range:** No shipped game publishes a rule. Use the arithmetic in §6.2 (~30 m effective radius at 15 regions on 200 m), bound compactness, and author 2–3 defend posts for non-compact regions.
4. **Hex sphere battlefield?** The evidence is mostly against (IGN on visibility and camera, Red Blob on pentagons and north, GameSpot on omnidirectional flanking). It works for slow, non-combat play (Before We Leave) and as a meta-map.
5. **Keeping 3 maps fresh:** Shuffle slot contents under constraints, add edge-changing variant toggles, draw JEV start from authored directions, rotate objective pools, and keep a beginner seed (Catan, ITB, RoR2, Spelunky).

## 9. Ranked lessons
1. **Freshness comes from contents and graph edges, not new geometry** (Spelunky, ITB, RoR2).
2. **Place by supply-graph hop distance, then veto and validate across many seeds** (AoE II, Catan, Polytopia). Histogram 1,000 seeds per map.
3. **Hold must be region-scoped, not point-leashed** (AoE II stance contrast). Author defend posts and bound region compactness.
4. **One primary trait per region; border style shows state** (Northgard, Humankind).
5. **Use non-capturable or hazard regions and edge toggles to make supply necks** (Dune: Spice Wars, CoH). Without necks the supply chain doesn't matter.
6. **If modular, attach gameplay metadata and sockets to modules and validate globally** (Bad North).
7. **Keep spheres for presentation or the meta-map; keep the battlefield flat** (IGN/GameSpot on PA; Populous's fake sphere; Goldberg pentagon share).
8. **Compensate instead of chasing perfect symmetry** (TI4). In PvE, budget threat per seed.

## 10. Traps
- Voronoi or cell borders that cross open ground with no terrain cue.
- Hex-uniform adjacency, which removes chokepoints and makes supply cuts meaningless.
- Pentagon regions on a sphere: a hidden 5-neighbour asymmetry.
- JEV starting anywhere: some seeds put it 1 hop from a player's main.
- Variant toggles that are only cosmetic.
- Long or L-shaped regions plus a single idle point: the playtest bug, repeated per seed.
- Generator bias you never measured (AoE II's south-biased hills).

## 11. Open questions
1. What are the actual unit acquisition ranges and speeds? They set R for the compactness rule (§6.2).
2. Should Hold idle positions be authored per region (defend posts) or computed (e.g. a weighted centroid of structures plus the anchor)?
3. How many edge-toggle variants per map can art and level design support, and which toggles change the supply graph most?
4. Is JEV-vs-players "fair" defined per seed (a threat budget), or is difficulty only tuned by run-level escalation?
5. Should region count scale with player count (e.g. 1–2 regions locked in solo), or stay fixed at 15?
6. Is a planet meta-map in scope for the node map, or is a 2D graph enough for 4–6 battles?
7. Should 1–2 non-capturable or hazard regions per map be a standard role in the shuffle pool?
8. Which validator metrics block a seed? Candidate list: hops to the nearest deposit, minimum JEV distance, supply-neck count, contested-edge count.
