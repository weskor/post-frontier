# Forces and orders

> Part of the [Post-Frontier design](../Design.md). Related: [battle](battle.md), [buildings](buildings.md), [cards](cards.md), [economy](economy.md), [meta](meta.md), [ui](ui.md), [units](units.md).

## Barracks [Built]

- A barracks locks permanently to one force type on its first Start, and automatically refills casualties for Power.

| Type | Capacity | Unit cost | Time per unit | One-time fee |
|---|---:|---:|---:|---:|
| Frontline | 6 | 20 | 3.33 s | 0 |
| Ranged | 4 | 30 | 4.33 s | 0 |
| Siege | 2 | 50 | 6.67 s | 180 |

- Today's three types become the Brawler, Rifle and Artillery of the roster ([units.md](units.md)). The same lock, refill and upgrade rules apply to the Factory and Lab ([buildings.md](buildings.md)).
- The lock is a deliberate commitment. Because battles are short, it never stays sunk for longer than one battle.
- **Decided:** a finished building can be recycled for 50% of its build cost, so a wrong lock can be undone at a price ([buildings.md](buildings.md)).

## Force cap [New] — decided

- Each commander fields at most **4 forces** at once, or **5 solo**, matching the solo player's extra unit type ([commanders.md](commanders.md)).
- The cap counts production buildings, since each one owns one force. A fifth needs one recycled first ([buildings.md](buildings.md)). An orphan force doesn't count, but it can't be refilled.
- Forces grow stronger through tiers and perks rather than multiplying (see "Barracks upgrades" below).
- **Why:** the designer's note after the 2026-10-01 playtest: "if you have less to control, you feel more in control". Bad North's developers make the same point about a discrete possibility space ([area-defence.md](../Research/area-defence.md)), and Tooth and Tail was built around the attention split between base and battle ([pacing.md](../Research/pacing.md)).
- JEV isn't bound by this cap; its pressure comes from its budget and player-count scaling ([jev.md](jev.md)).

## Steering forces [Change] — decided

**What's wrong today.** These five problems were reported from play on 2026-10-01:
1. You steer an army by selecting a *building*, and the orders sit next to the production buttons.
2. Every barracks needs its own order, so several forces means repeating the same order several times.
3. You can't tell what a force is doing, where it's going, or why it retreats.
4. Recruits walk alone across the map, so forces look scattered.
5. The difference between Hold, Expand and Assault is unclear.

**The redesign:**

- **You command forces, not buildings.** Select a force by its map badge, its card in the force bar, or one of its units. Buildings only produce; the building panel keeps upgrades and perks. How selection and orders are input lives in [ui.md](ui.md).
- **Several forces at once:**
  - Shift-click or box-select force badges to give one order to all of them.
  - Keys **1–4** (solo **1–5**) select a single force by its force number.
  - A mixed selection marches at its slowest member's speed.
  - **[Later]** Saved control groups (Ctrl+1–9). Cut from launch scope in the design review; box-select, Shift-select and the order queue cover multi-force orders.
- **Three verbs** replace Hold, Expand, Assault and Fall Back:

| Verb | Target | Behaviour |
|---|---|---|
| **Move & Hold** | A region | Travel there, fighting what it meets on the way and capturing as it goes. Once there it **holds the whole region**: it waits at a defend post and answers any alarm in that region (see "Holding a region" below). It strikes back at most weapon range past the border, and **never auto-retreats**. Your own main is a valid target; today's Hold already allows that. |
| **Attack** | A region or a structure (HQ, Drill Rig, relay, core) | Push to the target, fighting and capturing along the way, and chase enemies near the target. Below the retreat threshold it makes a **fighting withdrawal**, still firing, to the nearest safe region. It **resumes the Attack automatically at 80% strength**. The force card shows *Withdrawing · 3/6 → resumes at 5/6*. **When the region is taken or the structure destroyed, the Attack turns into Move & Hold** in the region it ends in. |
| **Retreat** | — | A manual order: sprint (+25% speed) to the nearest safe region **without firing**, and refill there. |

The two attack verbs have different rules on purpose, so players can tell them apart. That was today's problem #5. The force card states each rule in one line.

**Definitions:**
- **Safe region:** the nearest connected region with no hostiles inside, preferring the last region the force held.
- **Orphan forces:** a force outlives its production building. It stays selectable and commandable; it just receives no more reinforcements or refits.
  - Today an orphan keeps its last front and can't be given orders. That's the measured cause of the stalled draws ([battle.md](battle.md)).
- **Today [Built]:**
  - Auto-retreat exists for Assault only. It uses a Defend front and keeps firing, and resumes only at full strength.
  - Fall Back goes to the barracks and doesn't fire.

- **Fighting on the way:** both Move & Hold and Attack fight whatever they meet in range while travelling, which keeps them predictable.
- **Order queue:** Shift-queue up to **3 orders**, e.g. *Attack Relay → Move & Hold West Cut*. The queue shows on the force card and the path line.
- **Rally point:** each production building has one; it defaults to its own region. New and idle forces Move & Hold there automatically.
- **Routing:** the shortest path is the default and is **previewed before you confirm**. **[Later]** Alt-click waypoints, cut from launch scope.
- **Reinforcements travel along the supply chain:**
  - A force standing in a **connected** region receives its replacements after a travel delay. They arrive *at the force*; nobody trickles across the map on their own.
  - A force in a cut-off region gets nothing until it's reconnected or retreats.
  - This ties steering to the connectivity rule ([economy.md](economy.md)).
- **Refit after an upgrade:** in a connected region, old units are swapped for upgraded ones **one at a time**, through the same channel as reinforcements. Nobody walks back to the building.
- **The executor is dumb and obedient.** It never overrides the player, and its rules (retreat threshold, targeting rule, pathing) are visible on the force card.
- **Intent arrows:** every commander's orders are drawn as coloured arrows on the shared map and minimap, so your orders tell your teammates your plan.

### Holding a region [Change] — decided

**What's wrong today.** In the 2026-10-01 playtest, a force holding a region ignored an attack on a building inside that region. In source, Hold sends the force to the region's capture point (the HQ in a main), and its units only engage enemies within **10.5 m of that point** (`AArmyGroup::PursuitRadius`). A building taking damage triggers nothing. On Habitable Zone v2, the farthest point of a region is on average 48 m from its capture point, and up to 75 m. A Frontline force on Hold covers about 19% of its region, Ranged 31% and Siege 53%; 12 of the 16 deposits are out of Frontline reach. Research: [area-defence.md](../Research/area-defence.md).

**Rules:**
- **Region alarm.** An alarm fires when a hostile unit enters the region, or when anything you own in it (a building or a force) takes damage. Every force holding that region hears it. Units still fight at their normal weapon range; the alarm only decides *where they go*.
- **Who responds:** the nearest holding forces respond until their combined strength reaches 1.25× the threat's. Strength is the Power value of living units. The others keep their posts, so a feint can't pull the whole region to one side. Forces of different commanders count together.
- **Idle spot:** each holding force waits at one of the region's **defend posts** ([map.md](map.md)), chosen automatically: the post that best sits between your buildings in that region and its borders with hostile or unscouted regions. Several forces in one region take different posts. A force never changes post while an alarm is active.
- **Ground markers [Built]:** see [map.md](map.md).
- **Border rule:** pursuit stops at the region border. While an attacker outside the border is damaging something inside, the force may strike back up to its weapon range past the border; then it returns.
- **No flip-flopping:** a responding force keeps its target until the target dies or leaves the leash, commits to an alarm for at least 8 s, and returns to its post 6 s after the region goes quiet. Starting values.
- **Readable:** the force card shows *Responding · Drill Rig under attack*, a line runs from the force to the threat, and the alert feed logs it ([ui.md](ui.md)).
- **JEV holds regions under the same rules.**

## Force settings [New] — decided

Each force has **one setting**, shown and changed on its force card:

| Setting | Options | Default | Applies to |
|---|---|---|---|
| Retreat threshold | Never / 25% / 40% / 60% of capacity alive | 40% (today's value) | Attack only |

- **Never** allows suicide pushes and last stands, which suits a Juggernaut breaking an HQ.
- **The targeting rule replaces the target-priority setting [Built]:**
  - An eligible explicit AttackTarget keeps priority over automatic targeting.
  - A unit keeps its current target until it dies or leaves weapon range/the pursuit leash. A newly available counter-class or nearer enemy does not interrupt that lock.
  - The counter preference applies only when a unit acquires a new automatic target: prefer enemies of the armor class it deals bonus damage against, then the nearest eligible enemy.
  - Demolition units prefer structures on new automatic acquisition.
  - The rule is readable from the two icons and shown on the force card [New].
  - A per-force priority setting is **[Later]**.

## Barracks upgrades [New] — decided

A barracks grows through three tiers. Upgrades belong to **that barracks' force only**, so two Brawler barracks can branch differently.

| Tier | What you choose | Requirement | Perk slots |
|---|---|---|---|
| 1 | The unit type, locked on first Start [Built] | — | 1 |
| 2 | **Branch:** one of the unit's variants | Power, Data and upgrade time at the barracks | 2 |
| 3 | **Mastery:** an expensive capstone for the chosen variant | Power, Data and time, **and** a living Workshop **of your own** ([buildings.md](buildings.md)) | 3 |

- **Branches:** each unit has 2 branches at launch, and **each branch is a card** that must be drafted ([cards.md](cards.md)). A 3rd branch per unit comes later as a Rare or Legendary card, or a meta unlock ([meta.md](meta.md)).
- **Who gets the upgrade:** new recruits come out upgraded. Units already in the field refit one at a time while the force stands in a connected region (see Steering above). A cut-off force keeps its old form, so timing the upgrade is a decision.
- **Perks:** drafted cards make perks available for a unit type for the rest of the run ([cards.md](cards.md)). In battle, each barracks buys perks into its free slots for Data.
- **How it's paid:** cards decide *which* branches and perks exist in this run; Power and Data decide *when* you take them in this battle. In-battle purchases reset every battle.
- **Starting values for tuning:**
  - Tier 2: 100 Power + 50 Data, 20 s.
  - Tier 3: 150 Power + 120 Data, 30 s.
  - Production pauses while upgrading.
