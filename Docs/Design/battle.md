# Battle

> Part of the [Post-Frontier design](../Design.md). Related: [build-order](build-order.md), [forces](forces.md), [jev](jev.md), [open-questions](open-questions.md).

- **Length target:** 8–12 min.
- **Start [Change]:** a planning phase, then the commanders' kits stand pre-built (see "Opening" below). Today [Built]: an empty base, 600 Power per commander and no units.
- **Win:** complete the node objective.
- **Lose [Built]:** the friendly HQ is destroyed.

## Opening: planning phase and pre-built kit [Change] — decided

**Why.** Every battle used to start from an empty base. In all 20 harness matches the AI's opening was identical to the second: barracks finished at 14 s, first Drill Rig at 27 s, first capture at 39 s, first fight around 2:15. One build order, replayed 4–6 times a run. Research: [opening.md](../Research/opening.md).

**Planning phase (before 0:00):**
- The simulation doesn't run yet, JEV included.
- In their own build zone ([map.md](map.md)), each commander places their kit's production buildings and one Drill Rig on a main-region deposit, picks each production building's unit lock, and queues first orders for 0:00.
- JEV's first published plan is shown ([jev.md](jev.md)), so the layout answers a real threat.
- Players can draw on the shared map.
- The phase ends when every player clicks **Ready**, or after **60 s**. Anything a player hasn't placed by then, and any AI adjutant's kit ([run.md](run.md)), goes to a default layout for that commander.

**At 0:00:** the placed buildings stand finished and production starts. Everything else (the Workshop, the signature building, drafted buildings) is built as normal.

**Head start balance:** starting Power drops to **200** per commander (starting value; today 600) to pay for the free buildings ([economy.md](economy.md)). JEV also starts with a matching pre-built base, so its first release still matters ([jev.md](jev.md)).

**Later variety:** a starting package per node and one carried force are both **[Later]** ([run.md](run.md)).

## No clock: JEV escalates

There is no battle timer and no fail state tied to time. Instead JEV **escalates on a visible schedule** of versions, released **every 2 minutes**. The HUD timeline counts down to the next one.

| Release | At | Adds (proposal) |
|---|---|---|
| `v1.0` | 0:00 | Base behaviour: builds, expands, defends |
| `v1.1` | 2:00 | Raids Drill Rigs |
| `v1.2` | 4:00 | Fields counter-picks against the team's armor classes |
| `v2.0` | 6:00 | Coordinated waves from all its forces, plus its first calldown |
| `v2.1` | 8:00 | Larger waves and faster calldowns |
| `v2.1` overrun | ~10:00 | Sustained pressure designed to overrun a team that is stalling |

- Each release adds a **new behaviour** as well as more budget, and sends a scheduled wave ([jev.md](jev.md)).
- Escalation depends on elapsed time and node depth only. **It never reacts to the team's performance.** No rubber-banding.

Finite deposits **[Built]** add pressure from the economy side: a normal deposit runs out after 600 s and a rich one after 500 s.

**Measured today (`Docs/Balance.md`, 20 AI-vs-AI matches on V2):**
- Median battle: **23.6 min** at the 2/s baseline. Only 1 of 9 decisive matches fell inside 12–18 min.
- First deposit runs dry at a median of **9.6 min**, so depletion barely touches an 8–12 min battle.
- The draws came from **orphan forces**: their barracks had died, they couldn't receive new orders, and they piled up in the main base.

`Balance.md` still measures against the old README target of 12–18 min. **The design target is 8–12 min.**

**Levers that end stalls**, all in build step 1b ([build-order.md](build-order.md)):
1. Orphan forces stay commandable ([forces.md](forces.md)).
2. A defined wave budget per release ([jev.md](jev.md)).
3. A decision on deposits: either cut reserves to about half, so the first ones run dry around 5 min and force the second expansion, or drop depletion. **Open ([open-questions.md](open-questions.md)).**

Measure battle length per node in the harness before and after each lever.

## Objectives

Each node shows its objective in advance.

| Objective | Win when | Pushes players to |
|---|---|---|
| Assault | The Lattice (JEV HQ) goes offline and the team wins its uplink hold (see "Guarding the HQs" below) | Commit to one push [Change] |
| Raid | **3 JEV relays** are destroyed. Relays are guarded and placed apart, so the team has to fight on several fronts | Spread out and fight on several fronts [New] |
| Sabotage | **3 marked regions** are held at the same time for **90 s**. Progress is kept when interrupted and decays slowly | Hold territory and work together [New] |

## Guarding the HQs [Change] — decided

**Why.** In the 2026-10-01 playtest, a force left on Assault walked to JEV's HQ and won at about 5 min while the player was busy in his base, without him noticing. In source, the HQ has 900 HP and one full Frontline force deals 120 DPS, so it dies in 7.5 s. The HQs are 211 m apart, about 50 s of walking, and JEV defends with only its single nearest force, which has to walk home. Even the target's 1800 HP falls in 15 s, or 3 s against five forces. AI-vs-AI simulations never rush, so their 23.6 min median hid this. Research: [pacing.md](../Research/pacing.md).

The same rules apply to **both HQs**, the Lattice and Hardline:
- **Failover Nodes:** two nodes stand in regions next to each main, pre-built at the start. While either stands, the HQ can't be damaged. A visible beam links them to the HQ, and a node's fall is announced to everyone. Nodes can be repaired but not rebuilt. Starting value: 1000 HP each.
- **Fortified opening:** nodes take 90% less damage until JEV's `v1.2` release at 4:00, shown on the timeline.
- **The hold:** when an HQ reaches 0 HP it goes **offline**, and the attackers must hold its region for **75 s** (starting value; the uplink, for the Lattice) to win.
  - Progress pauses while the defenders contest the region, and decays while no attackers are inside.
  - If it decays to zero, the HQ comes back online at 25% HP.
- **Final protocol:** when an HQ goes offline, its side gets one capped emergency wave at the HQ, announced to everyone. JEV's wave uses the current release's wave budget ([jev.md](jev.md)). Hardline gets the same budget as colonist militia. It's never strong enough to win on its own.
- **Earliest win:** about 4:00 of fortification, plus breaking a node and the HQ, plus the 75 s hold, so roughly 6–7 min against an unattended rush.
