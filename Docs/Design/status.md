# Current build vs target

> Part of the [Post-Frontier design](../Design.md). Related: [battle](battle.md), [buildings](buildings.md), [cards](cards.md), [commanders](commanders.md), [economy](economy.md), [forces](forces.md), [jev](jev.md), [map](map.md), [meta](meta.md), [run](run.md), [ui](ui.md), [units](units.md).

Current-build values come from a source audit on 2026-10-01. README.md and World.md were brought in line with source on 2026-10-02.

| Area | Current build | Target |
|---|---|---|
| Structure | Single match; choose a map; no saves | Node-map run in 2 or 3 acts, lives, drafts, auto-save ([run.md](run.md)) |
| Battle end | Kill the enemy HQ (900 HP, 7.5 s for one full Frontline force); no time limit | Assault, Raid or Sabotage objectives plus JEV escalation every 2 min; Failover Nodes, a fortified opening and a 75 s hold guard both HQs ([battle.md](battle.md)) |
| Opening | Empty base, 600 Power; everything is built after 0:00 | Planning phase (ready-up, 60 s cap, simulation paused); kit pre-built; 200 Power; JEV starts with a matching base ([battle.md](battle.md)) |
| Awareness | A small enemy-HQ bar; JEV's HQ alarm plays only at its HQ | Team announcer for objective events including your own successes, objective strip, Space for the latest alert, solo slow-down option ([ui.md](ui.md)) |
| Resources | Power only | Power + Data ([economy.md](economy.md)) |
| Income | Extractor pays only its builder, even after its region is lost | Team pool split evenly; connected chain required; reward regions pay Data ([economy.md](economy.md)) |
| Gifting | Not possible | Free, unlimited, logged ([economy.md](economy.md)) |
| Bases | One shared main, free placement | Shared HQ with a build zone per commander ([map.md](map.md)) |
| Map | Fixed layouts, no traits or neutrals; one capture point per region; authored defend posts with checked coverage and local-team ground markers ([map.md](map.md)) | Randomized variants under fairness rules, link toggles, region traits, neutrals ([map.md](map.md)); map events later |
| Units | [Built] Three retagged types with shared faction stats, armor and definition-driven class speeds; stats authored as text ([units.md](units.md)) | Nine niches, Shielded units, splash and faction twists ([units.md](units.md)) |
| Production | One Barracks type for every unit | Barracks, Factory and Lab by unit group ([buildings.md](buildings.md)) |
| Upgrades | None | Tier 2 branch, tier 3 mastery, perk slots, per barracks ([forces.md](forces.md)) |
| Steering | Select a building; one goal per barracks (Hold/Expand/Assault/Fall Back); right-click only cancels a mode; clicking a unit selects its barracks; Hold reacts only within 10.5 m of the capture point and ignores attacks on buildings; recruits walk out alone; orphan forces can't be ordered | Select forces by badge, card or unit; box/Shift select; 1–4 by force number (solo 1–5); smart right-click with A and R order keys; 3 verbs with distinct rules; Hold defends the whole region (alarm, defend posts); route preview; order queue; reinforcements along the supply chain; orphans stay commandable; force bar with production state ([forces.md](forces.md), [ui.md](ui.md)) |
| Force settings | Fixed 40% retreat on Assault; [Built] automatic counter targeting ([forces.md](forces.md)) | Retreat threshold per force, Attack only; targeting explanation on the force card ([forces.md](forces.md)) |
| Buildings | Barracks, Extractor, Workshop; no building cap; no recycling | 10 buildings in 7 categories at launch, recycle for 50%; production buildings capped at 4 per commander (solo 5); Overclocker and Barricade later ([buildings.md](buildings.md), [forces.md](forces.md)) |
| HQ | Passive, 900 HP | Passive, 1800 HP; tiers and calldowns later ([buildings.md](buildings.md)) |
| Enemy scaling | Baseline income scales with the live human commander roster; fractional credits carry between payments ([jev.md](jev.md)) | Player-count scaling on wave budgets and node-depth scaling ([jev.md](jev.md)); solo relief from a secondary commander and active pause |
| Counters | [Built] Armor and HP counter bonuses composed with Workshop specializations ([units.md](units.md)); automatic targeting rule ([forces.md](forces.md)). [Built] Runtime-definition duel matrix and combat acceptance reporting; baseline pending the separate pursuit-stall fix and re-measurement ([Balance.md](../Balance.md)). Support compositions remain unbuilt | Shields/EMP, a balanced who-beats-whom matrix validated in the harness, branches and perks ([units.md](units.md)) |
| JEV | One deterministic planner, debug plan text, no difficulty settings | Committed per-force plans; the planner proposes legal plans and a deterministic chooser picks; memos from templates, no runtime LLM; hybrid economy plus free-spawn waves, 2 calldowns, personalities, bosses ([jev.md](jev.md)) |
| Commanders | None | 4 commanders, all available from run 1: passive, region ability, ultimate, kit, card pool ([commanders.md](commanders.md)) |
| Specializations | 3 at the Workshop, 150 each, one per commander | Siege Optics becomes a perk, Entrenched Frontline becomes Shield Wall, Field Repairs a Doctrine; the Workshop only gates tier 3 ([buildings.md](buildings.md), [cards.md](cards.md)) |
| Leaving | A leaver's buildings and forces are destroyed | An AI adjutant takes over ([run.md](run.md)) |
| Time | Solo menu pause; no pause in co-op | Solo active pause; 1 co-op pause of 60 s ([ui.md](ui.md)) |
| Information | Fully visible; no fog | Region visibility, blips and last-seen ghosts; shared team vision; JEV under the same fog ([map.md](map.md)) |
| Names | Menu, map data and content still use the old display names: Availability Zone, Extractor, The Bunker, The Cluster, region labels such as Uplink | Habitable Zone, Drill Rig, Hardline, The Lattice, Skyhook and the rest of the far-future set ([World.md](../World.md#names-in-code-and-assets)) |
| Meta | None (only audio volume is saved) | Unlocks only, ladder, codex, cosmetics ([meta.md](meta.md)) |
