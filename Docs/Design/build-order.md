# Build order

> Part of the [Post-Frontier design](../Design.md). Related: [economy](economy.md), [jev](jev.md), [open-questions](open-questions.md), [run](run.md).

The battle has to be fun before a run layer can help it.

1. **Make one battle good.** Split in two, each with its own playtest, so the gate isn't hiding ten features.
   - **1a: steering and readability**
     - **Hold defends its region:** the region alarm, the capped response, the border rule, and defend posts authored on Habitable Zone v2 ([forces.md](forces.md), [map.md](map.md)). This fixes the 2026-10-01 playtest bug and can ship before the rest of the redesign.
     - **The steering redesign:** select forces rather than buildings; the force cap of 4 (solo 5); smart right-click with A and R order keys and the cursor preview; the 3 verbs with distinct rules; orphan forces stay commandable; the route preview; the order queue; the force bar with production state; the build bar with grid hotkeys; intent arrows and pings. Fix today's feedback gaps from the input audit ([ui.md](ui.md)).
     - **Awareness:** the team announcer, the objective strip, and Space for the latest alert ([ui.md](ui.md)). The 2026-10-01 playtest was won without the player noticing.
     - **JEV planner port:** move `AEnemyCommander` from Hold/Expand/Assault/Fall Back to the 3 verbs. Add committed per-force plans with hold windows, published on the timeline. The planner proposes legal candidate plans and a deterministic chooser picks one; memos come from templates ([jev.md](jev.md)).
     - JEV player-count scaling.
     - Re-tag today's three units with armor classes, speed by class and the targeting rule. **Build the harness duel matrix** and validate the re-tag before adding units.
     - The co-op pause and solo active pause.
     - **Gate:** playtest with 2–3 friends: steering feels clear, and JEV's plans are readable. **Not run as a separate playtest (owner, 2026-10-04).** On a flat map with 3 units and no active choices, 1a isn't interesting enough to share. Its questions join the 1b playtest.
   - **1b: economy and pressure**
     - Connectivity (definition in [economy.md](economy.md)), shared income with accumulators, reinforcements along the supply chain, and gifting.
     - **Deposit pressure:** halved deposit reserves, and JEV's baseline split from the human floor; the harness compares a 2/s and a 1/s human baseline ([economy.md](economy.md)).
     - **JEV planner, chain-aware:** expansion scoring gets a supply-chain term, so JEV doesn't build disconnected Drill Rigs and starve.
     - Lancer and Scrambler, so the Shielded triangle exists.
     - Version escalation and free-spawn waves from the budget table ([jev.md](jev.md)).
     - **Data with something to buy:** reward regions and structure kills, plus tier-2 branches as the first purchase.
     - **Pre-built start and planning phase,** with a generic kit (Barracks plus a Drill Rig) until commanders exist; lower starting Power; JEV's matching start ([battle.md](battle.md)). It shortens every battle, so it belongs before the battle-length gate.
     - **Guarding the HQs:** Failover Nodes with reduced damage early on, the hold and the emergency wave for both HQs ([battle.md](battle.md)).
     - **A two-commander threat:** Split-Brain Cut ([battle.md](battle.md#two-commander-threat-new)). A second design stays open ([open-questions.md](open-questions.md)).
     - **Where and how to fight (moved from step 4 and step 3, owner decision 2026-10-04):**
       - **Region traits** on fixed regions of Habitable Zone v2. Each trait's terrain matches it: high ground sits on raised plateaus with ramps, and cover has visible cover. The map also gets narrow necks and 2–3 real routes between fronts that cost different things, so routes stop being straight lines ([map.md](map.md)).
       - **One region ability, Fortify, for every commander** until commanders exist. It is paid with Data plus a cooldown ([commanders.md](commanders.md)).
     - **Gate:** the harness gate in [Balance.md](../Balance.md#step-1b-gate-new) (median battle length and a scripted rush); the playtest shows the decision budget is met and no unit type dominates.
2. **Depth inside a battle:**
   - Tier 3, perks, Factory and Lab, Workshop as the tier-3 gate.
   - Turret, Repair Bay, Shield Generator, Relay Tower, Conduit.
   - Build zones, recycling.
   - **Fog of war:** region visibility, blips and ghosts for players, plus JEV's perceived-state layer. Relay Tower and Watchtower get their full jobs.
3. **Run layer:**
   - Node map drawn as the frontier moon, lives, drafts and cards, Salvage.
   - Repair, Depot and Event nodes; save and resume; the AI adjutant (stub first, [run.md](run.md)).
   - One act map, one boss.
   - Two commanders with abilities and ultimates.
4. **Variety:**
   - The remaining units.
   - JEV personalities and calldowns; Raid and Sabotage.
   - Neutrals; traits in the seed shuffle with its fairness rules, the seed checker, and link toggles.
   - **The other two commanders.** All four must exist before release, because all four are available from run 1.
   - The remaining bosses and maps.
5. **Meta:** unlocks, the Terms of Service ladder, codex, cosmetics.
6. **Later (cut from launch scope in the design review):** HQ tiers and calldowns, Workshop techs, per-force target priority, saved control groups and waypoints, Overclocker, Barricade, rogue drones, map events, 3 more JEV calldowns, the Echo Chamber boss, weekly operations, controller support with the target-first menu, the starting package per node, the Cryosleep Protocol, and the LLM chooser experiment.

Ship telemetry from step 1: card pick rate and win rate, unit type pick rate and win rate, battle length, decisions per player per minute, and where battles end.
