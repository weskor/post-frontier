# Current build vs target

> Part of the [Post-Frontier design](../Design.md). Related: [battle](battle.md), [buildings](buildings.md), [cards](cards.md), [commanders](commanders.md), [economy](economy.md), [forces](forces.md), [jev](jev.md), [map](map.md), [meta](meta.md), [run](run.md), [ui](ui.md), [units](units.md).

Current-build values come from a source audit on 2026-10-01, with existing-unit tuning and acceptance updated on 2026-10-03. README.md and World.md were brought in line with source on 2026-10-02.

| Area | Current build | Target |
|---|---|---|
| Structure | Single match; choose a map; no saves | Node-map run in 2 or 3 acts, lives, drafts, auto-save ([run.md](run.md)) |
| Battle end | Kill the enemy HQ; no time limit | Assault, Raid or Sabotage objectives plus JEV escalation every 2 min; Failover Nodes, a fortified opening and a 75 s hold guard both HQs ([battle.md](battle.md)) |
| Opening | Empty base, 600 Power; everything is built after 0:00 | Planning phase (ready-up, 60 s cap, simulation paused); kit pre-built; 200 Power; JEV starts with a matching base ([battle.md](battle.md)) |
| Awareness | [Built] Team-wide attributed objective announcer/feed, both-HQ objective strip, Space objective history, G team pings and read-only teammate-force inspection ([ui.md](ui.md)); F focuses owned selection without automatic camera jumps | [New] Failover Node/exposure and hold objectives, voiced supply/JEV events, solo slow-down option ([ui.md](ui.md)) |
| Resources | Power only | Power + Data ([economy.md](economy.md)) |
| Income | Extractor pays only its builder, even after its region is lost | Team pool split evenly; connected chain required; reward regions pay Data ([economy.md](economy.md)) |
| Gifting | Not possible | Free, unlimited, logged ([economy.md](economy.md)) |
| Bases | One shared main, free placement | Shared HQ with a build zone per commander ([map.md](map.md)) |
| Map | Fixed layouts, no traits or neutrals; one capture point per region; authored defend posts with checked coverage and local-team ground markers ([map.md](map.md)) | Randomized variants under fairness rules, link toggles, region traits, neutrals ([map.md](map.md)); map events later |
| Units | [Built] Three types with shared faction stats, armor, definition-driven class speeds and enemy-only Artillery splash; accepted tuned profiles authored as text ([units.md](units.md)) | Nine niches, Shielded units and faction twists ([units.md](units.md)) |
| Production | One Barracks type for every unit | Barracks, Factory and Lab by unit group ([buildings.md](buildings.md)) |
| Upgrades | None | Tier 2 branch, tier 3 mastery, perk slots, per barracks ([forces.md](forces.md)) |
| Steering | [Built] Owned force-set selection, orphan selection, numbered map badges, teammate read-only inspection and explicit camera focus ([ui.md](ui.md)). Buildings remain independently selectable; one goal per barracks (Hold/Expand/Assault/Fall Back); right-click only cancels a mode; [Built] whole-region Hold with shared defend posts, proportional team response, border leash, sticky commitment/quiet timers and attributed response alerts ([forces.md](forces.md)); recruits walk out alone; orphan forces still can't receive new orders | Force-bar selection; smart right-click with A and R order keys; 3 verbs with distinct rules; route preview; order queue; reinforcements along the supply chain; orphans stay commandable; force bar with production/Responding state ([forces.md](forces.md), [ui.md](ui.md)) |
| Force settings | Fixed 40% retreat on Assault; [Built] automatic counter targeting ([forces.md](forces.md)) | Retreat threshold per force, Attack only; targeting explanation on the force card ([forces.md](forces.md)) |
| Buildings | [Built] Barracks, Extractor, Workshop; categorized always-visible build bar, grid hotkeys and placement feedback ([ui.md](ui.md)); per-commander production cap with construction counted and orphan exclusion ([forces.md](forces.md)); no recycling | [New] Build-bar cap count/block feedback; 10 buildings in 7 categories at launch, recycle for 50%; Overclocker and Barricade later ([buildings.md](buildings.md), [forces.md](forces.md)) |
| HQ | Passive, 900 HP | Passive, 1800 HP; tiers and calldowns later ([buildings.md](buildings.md)) |
| Enemy scaling | Baseline income scales with the live human commander roster; fractional credits carry between payments ([jev.md](jev.md)) | Player-count scaling on wave budgets and node-depth scaling ([jev.md](jev.md)); solo relief from a secondary commander and active pause |
| Counters | [Built] Armor and HP counter bonuses composed with Workshop specializations ([units.md](units.md)); automatic targeting rule ([forces.md](forces.md)). [Built] Runtime-definition duel matrix and combat acceptance reporting; the tuned existing roster passes all rules over 40 seeds ([Balance.md](../Balance.md)). Support compositions remain unbuilt | Shields/EMP, expanded who-beats-whom roster validated in the harness, branches and perks ([units.md](units.md)) |
| JEV | One deterministic planner, debug plan text, no difficulty settings | Committed per-force plans; the planner proposes legal plans and a deterministic chooser picks; memos from templates, no runtime LLM; hybrid economy plus free-spawn waves, 2 calldowns, personalities, bosses ([jev.md](jev.md)) |
| Commanders | None | 4 commanders, all available from run 1: passive, region ability, ultimate, kit, card pool ([commanders.md](commanders.md)) |
| Specializations | 3 at the Workshop, 150 each, one per commander | Siege Optics becomes a perk, Entrenched Frontline becomes Shield Wall, Field Repairs a Doctrine; the Workshop only gates tier 3 ([buildings.md](buildings.md), [cards.md](cards.md)) |
| Leaving | A leaver's buildings and forces are destroyed | An AI adjutant takes over ([run.md](run.md)) |
| Time | Solo active pause; shared co-op pause and countdown ([ui.md](ui.md)) | Solo opt-in imminent-win/loss slow-down ([ui.md](ui.md)) |
| Information | Fully visible; no fog | Region visibility, blips and last-seen ghosts; shared team vision; JEV under the same fog ([map.md](map.md)) |
| Names | Menu, map data and content still use the old display names: Availability Zone, Extractor, The Bunker, The Cluster, region labels such as Uplink | Habitable Zone, Drill Rig, Hardline, The Lattice, Skyhook and the rest of the far-future set ([World.md](../World.md#names-in-code-and-assets)) |
| Meta | None (only audio volume is saved) | Unlocks only, ladder, codex, cosmetics ([meta.md](meta.md)) |
