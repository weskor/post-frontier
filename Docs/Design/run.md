# Run structure [New] — decided

> Part of the [Post-Frontier design](../Design.md). Related: [jev](jev.md), [map](map.md).

A **run** is a branching node map split into **acts**. Each act has one map biome ([map.md](map.md)) and ends with a boss ([jev.md](jev.md)). Battles take about 12 min including the draft. The group picks the run length at the start:

| Length | Acts | Battles | Approx. time |
|---|---|---|---|
| Short | 2 | 4: two battles and two bosses | ~50–60 min |
| Standard | 3 | 6: three battles and three bosses | ~75–90 min |

Each act has one regular or Elite battle and a boss, plus non-battle nodes (Repair, Depot, Event) that take a minute or two. The map offers **up to 4 paths** per step.

**Run map: a frontier moon [New] — decided.** The node map is drawn as a small hex-tiled moon. Nodes sit on its tiles, and routes run between neighbouring tiles. Battlefields stay flat: as a battlefield, a sphere hides part of the map, opens flanks on every side and has no fixed north, and a hex sphere always has 12 pentagons ([map-structure.md](../Research/map-structure.md)). Keep it small and concrete, about one tile per node rather than a world to wander. Into the Breach cut its large strategic world map as too abstract.

## Node types

The map shows each node's type, objective, JEV personality, modifiers and reward before the team picks a route.

| Node | Content |
|---|---|
| Battle | Standard battle; normal reward |
| Elite | Harder JEV personality or modifiers; rare reward plus a team relic vote |
| Repair | Choose one: restore 1 life, upgrade one card, **or** remove one run scar |
| Depot | Spend Salvage: buy a card, or remove a card at a rising cost |
| Event | A Machine memo with a choice: trade-offs, joke outcomes, run scars. **1–2 per act**, offered as route options |
| Boss | JEV major version ([jev.md](jev.md)); boss reward |

The node before the boss is always a Repair.

## Route choice

The team votes on the next node. Ties go to a navigator role that rotates each node, so no single player steers the whole run.

## Lives and losing

- The team shares a pool of **3 lives**.
- A lost battle costs 1 life.
  - **Lost boss:** retry the boss with a new variant. A boss can't be skipped.
  - **Lost normal or Elite battle:** move on. The team still gets a **reduced reward based on regions held** when it ended.
- A loss also leaves **one run scar**. A loss should change the story, not only subtract a life. A scar ends at the **next won battle**, or earlier if a Repair node removes it. That prevents a death spiral where scars stack on a team that is already losing.

| Scar | Effect |
|---|---|
| Grudge node | The JEV personality that beat you returns later as an Elite, tuned against your weakest armor answer |
| Damaged HQ | The HQ starts each battle at 75% |
| Salvage seized | Salvage earned is reduced by a third |
| Ability throttled | One commander's ultimate charges 50% slower |

- **Concede** ends a battle early as a loss but keeps the reward earned so far, instead of risking the HQ for nothing.
- At 0 lives the run ends. The pool is **fixed at 3** whatever the group size; JEV scaling handles difficulty ([jev.md](jev.md)).

## Event memos — first set

| Memo | Choice |
|---|---|
| Terms Update #11,402 | Accept: +Salvage, and JEV gets +10% budget next battle. Decline: nothing. |
| Defect Report | Remove a card of your choice for 2 free rerolls |
| Demo Unit | Take a Legendary that lasts only one battle |
| Leaked Roadmap | Reveal all node rewards for the rest of the act, or take Salvage |
| Satisfaction Census | *"Rate your extinction, 1–5 stars."* Every answer gives a random Common, with a different memo reply. |

## Saving and resuming [New] — decided

- The run **auto-saves between battles**. The host holds the save, and the same group resumes another evening.
- A missing player's commander is played by an **AI adjutant**, or the player rejoins.

## Joining and leaving [New] — decided

- New players **join between battles**.
- If a player leaves mid-battle, an **AI adjutant** plays their side until the battle ends. Afterwards the team continues smaller, or a friend takes the slot.
- **Today [Built]:** a leaver's buildings and forces are destroyed. That changes.
- **The adjutant reuses the existing planner:** the simulation subsystem already runs `AEnemyCommander` on the human team for autopilot matches (`Docs/Balance.md`). At first it is a **stub**: it builds, produces and gives orders, but never uses cards, commander abilities or ultimates. Treat it as a placeholder ally, not a full teammate.

## What carries between battles

| Carries | Resets every battle |
|---|---|
| Cards (Doctrines, unlocks) and team relics (Protocols) | Units, buildings, Power, regions |
| Commander choice | Upgrades and perk purchases |
| Lives, Salvage, run scars | |

Units and Power do not carry over. That keeps one strong battle from snowballing the run. Every battle opens with a planning phase and the commanders' kits pre-built, so the opening isn't the same chore each time ([battle.md](battle.md)).

**[Later] Opening variety:**
- **Starting package per node:** before a battle, spend points set by the node's type and depth, never by past performance, on an extra pre-built building, a starter force, extra Power, or revealing JEV's second plan.
- **One carried force,** only through the *Cryosleep* Protocol ([cards.md](cards.md)). Units still don't carry over by default.
