# Battle

> Part of the [Post-Frontier design](../Design.md). Related: [build-order](build-order.md), [forces](forces.md), [jev](jev.md), [open-questions](open-questions.md).

- **Length target:** 8–12 min.
- **Start [Built]:** a planning phase, then the commanders' kits stand pre-built (see "Opening" below). Before this: an empty base, 600 Power per commander and no units.
- **Win:** complete the node objective.
- **Lose [Built]:** the friendly HQ goes offline and the attackers complete its hold ([Guarding the HQs](#guarding-the-hqs-built--decided)).

## Opening: planning phase and pre-built kit [Change] — decided

**Why.** Every battle used to start from an empty base. In all 20 harness matches the AI's opening was identical to the second: barracks finished at 14 s, first Drill Rig at 27 s, first capture at 39 s, first fight around 2:15. One build order, replayed 4–6 times a run. Research: [opening.md](../Research/opening.md).

**Planning phase (before 0:00) [Built].** Starting values, orchestrator 2026-10-04. The authoritative core and the controller's input and HUD (Enter, KIT bar, planning panel, roster chips, see [ui.md](ui.md)) are built and tested. Commands: `Commands/PlanningCommands.h`.
- It lasts up to 60 s of real time and ends early when every human is Ready. The battle clock, economy, JEV, combat, production and construction don't run; the world pauses without spending the co-op pause, and P is refused. The phase ends once, at the deadline or on the last Ready, after the navmesh is ready. The battle clock (JEV's schedule, telemetry and simulation durations) reads 0:00 at that moment.
- **Each commander:**
  - places a pre-built Barracks (normal placement rules, in own territory; free and finished) and a Drill Rig (default: the nearest free deposit in own territory);
  - picks the Barracks unit type, started at 0:00;
  - may queue a first order for that force (Move & Hold or Attack, three at most), issued at 0:00;
  - edits all of it until Ready or 0:00; Ready locks edits and un-Ready unlocks;
  - sees JEV's base and its first plans ([jev.md](jev.md)). Pings work; freehand drawing is **[Later]**.
- **At expiry,** unplaced kits auto-place at default spots (rings around the headquarters, clear of free deposits), as does any AI adjutant's kit ([run.md](run.md)).
- **Joining and leaving:** a commander who joins during planning gets a kit; one who leaves has their kit removed. If a commander's territory has no free deposit, they get the Drill Rig's cost in Power instead ([economy.md](economy.md#deposits-change)).
- **Wallet:** the Opening values in [economy.md](economy.md#resources-change--decided).
- **JEV's matching start [Built]:** one pre-built Barracks and Drill Rig per human commander, placed and finished as the humans' kits are. They follow the roster: a joiner adds a pair, a leaver removes one.
- **JEV's first plans [Built]:** published during planning, so the layout answers a real threat. JEV's kit forces exist then (its Barracks are configured as the planning kit's are, the role by JEV's producer rule), and each is planned at its squad size as if at 0:00 ([jev.md](jev.md#published-intent-built)). The world stays frozen while they are made: JEV plans when its kit stands and whenever the roster changes, not on a tick, and the plans' 25 s commitment starts at 0:00.

**At 0:00:** the placed buildings stand finished and production starts. Everything else (the Workshop, the signature building, drafted buildings) is built as normal.

**Head start balance:** starting Power drops to pay for the free buildings ([economy.md](economy.md#resources-change--decided)). JEV's matching start keeps its first release meaningful ([jev.md](jev.md)).

**[Later] With build zones and commanders:** the kit becomes each commander's own production buildings, each placed in that commander's build zone ([map.md](map.md#bases-change--decided)) with its own unit lock. Build zones arrive in step 2 and commanders in step 3 ([build-order.md](build-order.md)).

**Later variety:** a starting package per node and one carried force are both **[Later]** ([run.md](run.md)).

## No clock: JEV escalates

There is no battle timer and no fail state tied to time. Instead JEV **escalates on a visible schedule** of versions, the Releases table in [jev.md](jev.md#how-jev-plays-change--decided). The HUD timeline counts down to the next one.

- Escalation depends on elapsed time and node depth only. **It never reacts to the team's performance.** No rubber-banding.

Finite deposits add pressure from the economy side. Their reserves are halved in build step 1b, so a normal deposit runs out after 300 s instead of 600 s ([economy.md](economy.md)).

**Measured today (`Docs/Balance.md`, 20 AI-vs-AI matches on V2):**
- Median battle: **23.6 min** at the 2/s baseline. Only 1 of 9 decisive matches fell inside 12–18 min.
- First deposit runs dry at a median of **9.6 min**, so depletion barely touches an 8–12 min battle.
- The draws came from **orphan forces**: their barracks had died, they couldn't receive new orders, and they piled up in the main base.

`Balance.md` still measures against the old README target of 12–18 min. **The design target is 8–12 min.**

**Levers that end stalls**, all in build step 1b ([build-order.md](build-order.md)):
1. Orphan forces stay commandable ([forces.md](forces.md)).
2. A defined wave budget per release ([jev.md](jev.md)).
3. Halved deposit reserves, so the first deposits run dry around 5 min and force the second expansion. Decided 2026-10-03; the Power baseline stays as a floor, at 2/s or 1/s ([economy.md](economy.md)).

Measure battle length per node in the harness before and after each lever.

## Objectives

Each node shows its objective in advance.

| Objective | Win when | Pushes players to |
|---|---|---|
| Assault | The Lattice (JEV HQ) goes offline and the team wins its uplink hold (see "Guarding the HQs" below) | Commit to one push [Built] |
| Raid | **3 JEV relays** are destroyed. Relays are guarded and placed apart, so the team has to fight on several fronts | Spread out and fight on several fronts [New] |
| Sabotage | **3 marked regions** are held at the same time for **90 s**. Progress is kept when interrupted and decays slowly | Hold territory and work together [New] |

## Guarding the HQs [Built] — decided

**Why.** In the 2026-10-01 playtest, a force left on Assault walked to JEV's HQ and won at about 5 min while the player was busy in his base, without him noticing. In source, the HQ has 900 HP and one full Frontline force deals 120 DPS, so it dies in 7.5 s. The HQs are 211 m apart, about 50 s of walking, and JEV defends with only its single nearest force, which has to walk home. Even the target's 1800 HP falls in 15 s, or 3 s against five forces. AI-vs-AI simulations never rush, so their 23.6 min median hid this. Research: [pacing.md](../Research/pacing.md).

The same rules apply to **both HQs**, the Lattice and Hardline (starting values, orchestrator 2026-10-04):
- **Failover Nodes [Built]:** two per HQ, pre-built. Their stats, the damage reduction of the opening and the loss announcements are in [buildings.md](buildings.md#hq). A visible beam links them to the HQ.
- **Offline HQ [Built]:** an HQ at 0 HP goes offline instead of being destroyed, and its main becomes a hold objective for the attackers (the uplink, for the Lattice). Hold progress runs from 0 to full:
  - attackers present with no defenders: progress grows by 1/75 per second;
  - any defender present: progress pauses;
  - nobody present: progress decays at the same rate.
  - Reaching 0 after progress existed, with no attackers present, brings the HQ back online at 25% HP.
  - Full progress (75 s of it) completes the hold and loses the battle for that side.
  - The main stays controlled and connected for its owner until the hold completes.
- **Emergency wave [Built]** (the final protocol): the first time a side's HQ goes offline (once per battle per side), an emergency force spawns at that HQ, announced globally.
  - Humans: one free force per commander, of that commander's Barracks unit type, at full squad.
  - JEV: one wave at the current release's wave budget ([jev.md](jev.md#how-jev-plays-change--decided)).
  - Neither emergency force is strong enough to win on its own.
- **Outcome [Built]:** in step 1b, which has the Assault objective only, a battle ends only on a completed hold. Raid and Sabotage keep their own win conditions when they arrive (step 4). The harness counts completed holds.
- **Earliest win:** the nodes' reduced damage lasts until the `v1.2` release; then a node and the HQ must break and the hold complete, so roughly 6–7 min against an unattended rush.

## Two-commander threat [New]

**Split-Brain Cut [Built]**, in co-op only (starting values, orchestrator 2026-10-04):
- **The threat:** at the `v2.0` release (6:00), JEV sends two extra free assault forces at the same moment to two authored, non-adjacent human supply-neck regions ([map.md](map.md#region-traits-new--decided)), on top of the normal `v2.0` wave. Each force is funded with half the `v2.0` budget ([jev.md](jev.md#how-jev-plays-change--decided)): 260 Power-equivalent for two commanders, 200 alone. Both plans are published 30 s ahead, with an alert row and a voiced line ("Split-Brain Cut in thirty seconds. Hold your supply necks."); the row names every target.
- **Targets:** [Built] the map authors the eligible pairs; on Habitable Zone v2 they are Skyhook with Reactor Yard and West Cut with Reactor Yard. A neck is a non-main region nearer the humans' main than JEV's whose loss lengthens or cuts the humans' shortest hop path to some other region; the map audit checks that every pair is two distinct, non-adjacent necks. JEV takes the pair the humans hold most of, else the pair nearest their main, and skips the threat with a logged reason when every pair is malformed or holds a region JEV controls.
- **Composition [Built]:** a cut force is one full Lancer squad (three) with one Brawler as escort, four units, bought in that order from the force's budget. The budget is a ceiling and a rich budget never grows the force, so two or more commanders do not make it stronger. The force keeps its Attack order until it has arrived and fights to the end.
- **Tuning [Built]:** one holding force loses its region without Fortify and keeps it with Fortify ([commanders.md](commanders.md)). The world test runs the real threat from the real `v2.0` schedule against six Brawlers (a full tier-1 squad) on Skyhook and Reactor Yard, with and without the cast (cast when the force is within 20 m of the region's anchor), and the runs are exactly repeatable: without Fortify both regions fall (the squad dies and JEV captures; the force keeps 263 of 618 and 97 of 618 durability); with it both hold (the force dies; the squad keeps 244 and 657 of 1980). The mix was chosen from this sweep, every row two commanders and six Brawlers per region:

| Cut force | Unfortified | Fortified |
|---|---|---|
| 2 Lancers | held | held |
| 3 Lancers | held (264, 630 left) | held |
| 4 Lancers | lost | one region lost, one held with 33 durability left |
| **3 Lancers and a Brawler** | **lost** | **held** |
| 3 Lancers and a Rifle | lost | held (195, 504 left) |
| 3 Lancers and a Scrambler | held | held |
| 3 Lancers and an Artillery | lost | one region lost |
| 4 Rifles | held | held |
| 5 Rifles | lost | one region lost |

  The window is one unit wide, so Fortify's ×0.75 decides the fight only in a narrow band. Other squads are outside the tuning: a Rifle squad beats the force without help, and a three-unit Lancer or Scrambler squad loses to it fortified or not.
- **Solo [Built]:** one target only: the region of the chosen pair the human holds, else the one nearer their main.
- **Failure:** costs the regions (real supply cuts through the normal capture), with no special fail state.
