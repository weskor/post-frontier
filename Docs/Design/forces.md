# Forces and orders

> Part of the [Post-Frontier design](../Design.md). Related: [battle](battle.md), [buildings](buildings.md), [cards](cards.md), [economy](economy.md), [meta](meta.md), [ui](ui.md), [units](units.md).

## Barracks [Built]

- A barracks locks permanently to one force type on its first Start, and automatically refills casualties for Power.

| Type | Capacity | Unit cost | Time per unit | One-time fee |
|---|---:|---:|---:|---:|
| Brawler | 6 | 20 | 3.33 s | 0 |
| Rifle | 5 | 24 | 4 s | 0 |
| Artillery | 3 | 40 | 6.67 s | 180 |

- Combat profiles and tuning live in [units.md](units.md). The same lock, refill and upgrade rules apply to the Factory and Lab ([buildings.md](buildings.md)).
- The lock is a deliberate commitment. Because battles are short, it never stays sunk for longer than one battle.
- **Decided:** a finished building can be recycled for 50% of its build cost, so a wrong lock can be undone at a price ([buildings.md](buildings.md)).

## Force cap [Built]

- Each human commander owns at most **4 production buildings** at once, or **5 solo**, matching the solo player's extra unit type ([commanders.md](commanders.md)). For both this cap and the force-number keys, **solo means exactly one human commander in the match roster**, regardless of network mode.
- Living production buildings count from placement, including construction and unconfigured barracks. Other commanders' buildings, non-producers and orphan forces don't count. An orphan can't be refilled.
- At the cap, authority rejects another producer without spending Power; the build bar greys its button, shows the current count/cap and explains blocked placement when clicked. Destroying a producer frees its slot. **[New]** Recycling will provide another way to free a slot ([buildings.md](buildings.md)).
- **[New]** Forces grow stronger through tiers and perks rather than multiplying (see "Barracks upgrades" below).
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

- **[Built] You select forces, not buildings.** The selection model includes living orphan forces, numbered map badges and teammate read-only inspection; input and camera behaviour live in [ui.md](ui.md#selecting-and-giving-orders-change--decided). **[Change]** Force-bar cards and selected-force order input replace the temporary building-inspector order controls; buildings then only produce, upgrade and refit.
- **[Built] Keys 1–4 (solo 1–5)** select a single owned force by its number, including a living orphan. Camera focus and multi-selection inputs are specified in [ui.md](ui.md#selecting-and-giving-orders-change--decided).
- **Several forces at once:**
  - **[Built]** Several owned forces can be selected together; see [ui.md](ui.md#selecting-and-giving-orders-change--decided). Commands accept several owned forces atomically.
  - **[Built]** Each multi-force command samples a selection speed cap once from the slowest living member's authored speed; an empty producer-backed force uses its authored production type. Zero-speed empty orphans are excluded from the sample. The cap belongs to that issued order, is not re-sampled after casualties, and resets when the order completes; Retreat adds its sprint bonus to that capped speed.
  - **[Later]** Saved control groups (Ctrl+1–9). Cut from launch scope in the design review; box-select, Shift-select and the order queue cover multi-force orders.
- **[Built] Three verbs** replace Hold, Expand, Assault and Fall Back in the command layer, executor, JEV, simulation and verification. Until selected-force order input lands, the building inspector offers Move & Hold and Attack with region picking, and immediate Retreat for that building's force.

| Verb | Target | Behaviour |
|---|---|---|
| **Move & Hold** | A region | **[Built]** Travel there, fighting what it meets in range and capturing as it goes; then hold the whole region at shared defend posts, responding to alarms under the rules below. It **never auto-retreats**; either main is a valid reachable target. Authored posts and ground markers are built ([map.md](map.md#region-size-and-defend-posts-built-unless-noted)). |
| **Attack** | A region or a hostile structure | **[Built]** Push to the target, fighting and capturing along the way, and chase enemies near the target. Below the retreat threshold it makes a **fighting withdrawal**, still firing, to the nearest safe region. With a living producer it **resumes the retained Attack automatically at 80% joined capacity**, rounded upward; travelling recruits do not satisfy this count. An orphan's withdrawal ends on safe arrival, advancing any queued order or becoming Move & Hold there. **When the force has arrived and the region is controlled without hostile units, or the structure is destroyed, the Attack turns into Move & Hold** where it ends unless another order is queued. A target completed during withdrawal does not interrupt the trip to safety; completion is applied on safe arrival. **[New]** The force card shows *Withdrawing · 3/6 → resumes at 5/6*, inferred from the retained Attack verb, Refilling status and withdrawal resume count. |
| **Retreat** | — | **[Built]** A manual order: sprint (+25% speed) to the nearest safe region **without firing while Retreating**, then refill there with weapons enabled. Arrival ends the sprint. Completing refill yields to the next explicit order; with none queued, Move & Hold defaults to the living producer's rally, or stays at the arrival region for an orphan. |

Move & Hold and Attack have different combat rules on purpose, so players can tell them apart. That was the reported problem #5. **[New]** The force card states each rule in one line.

**Definitions:**
- **[Built] Safe region:** the nearest HQ-connected controlled region with no hostile units inside, preferring the last region the force held when it is eligible. If none exists, manual Retreat and an Attack's withdrawal go to the team's main region (the HQ's region), the last line of defence.
- **[Built] Orphan forces:** a force outlives its production building, stays selectable and accepts owned force commands; it receives no more reinforcements or refits. Selection behaviour lives in [ui.md](ui.md#selecting-and-giving-orders-change--decided).

- **[Built] Fighting on the way:** travelling Move & Hold and Attack acquire automatic targets only within their weapon range, rather than the old 800 cm/enemy-ahead acquisition rule, and do not leave the march route to chase. Near-target Attack-phase combat, including a Move & Hold still securing its capture anchor, can pursue within **10.5 m** of the current waypoint destination (`AArmyGroup::PursuitRadius`). Once the region is secured, Move & Hold uses the whole-region [holding rules](#holding-a-region-built), not that local pursuit circle.
- **[Built] Intermediate capture:** a march waits for uncontested intermediate ground to become controlled. If a hostile contests its capture point, the force continues after physically reaching that waypoint rather than leaving its route to hunt the blocker. The final target still uses the verb's completion rules.
- **[Built] Order queue:** commands queue up to **3 orders in total, including the active order**, e.g. *Attack Relay → Move & Hold West Cut*. **[New]** Shift-queue input and display on the force card and path line.
  - **[Built]** An order yields to the next queued order when it completes. **Move & Hold** completes once the force has arrived, there are no hostile units inside the region and, if capturable, the team controls it. With nothing queued it keeps holding.
  - **[Built] Attack** completes after physical arrival at a controlled, hostile-free target region, or when its target structure is destroyed. It advances to a queued order instead of turning into Move & Hold. A withdrawal already in progress finishes its safe arrival first.
  - **[Built] Retreat** completes once the force has arrived and refilled to full capacity; an orphan completes on arrival because it cannot refill.
- **[Built] Rally point:** each production building has one; it defaults to its own region. A new or idle producer-backed force with no explicit order and nothing queued Move & Holds there automatically, including after a completed manual Retreat. Changing the rally redirects an existing idle force. Existing explicit Move & Hold/Attack orders are not idle and are not redirected. An orphan with no pending order stays at its current position, or at its completed Retreat's arrival region, without assignment to a defend post. The owned rally command is available to input and verification.
- **[Built] Routing:** the shortest graph path is the default. **[New]** Preview before confirmation. **[Later]** Alt-click waypoints, cut from launch scope.
- **Region-order formation placement [Built]:** only complete formation paths are accepted. If a structure blocks a slot at the capture anchor, the centre may shift by at most 75 cm within that same region; precise point orders still reject obstructed formations. Arrival and settled waypoint reuse use the formation centre corrected by the occupied-slot offsets, not a depleted formation's biased member mean or exact rigid slot occupancy after crowd steering. Every joined member must also have gathered within the formation's occupied radius plus the arrival tolerance before the force stops; a nearby mean cannot strand a trailing member. An assembling force retains its accepted route while recruits travel.
- **[Built] Current physical reinforcements:** paid recruits walk to the force's moving formation. At a quiet defend post they target their own clipped post slot, rather than reconstructing a rigid anchor from independently positioned members. They join only after physically reaching a valid rendezvous and accepting the current formation path.
- **[Change] Reinforcements travel along the supply chain:**
  - A force standing in a **connected** region receives its replacements after a travel delay. They arrive *at the force*; nobody trickles across the map on their own.
  - A force in a cut-off region gets nothing until it's reconnected or retreats.
  - This ties steering to the connectivity rule ([economy.md](economy.md)).
- **[New] Refit after an upgrade:** in a connected region, old units are swapped for upgraded ones **one at a time**, through the same channel as reinforcements. Nobody walks back to the building.
- **[Built] The executor is dumb and obedient.** Only Attack uses the retreat threshold. Current verb, target, active-first queue, status, waypoint, march speed and withdrawal resume count replicate on the force. **[New]** These rules and ETA inputs appear on the force card.
- **[New] Intent arrows:** every commander's orders are drawn as coloured arrows on the shared map and minimap, so your orders tell your teammates your plan.

### Holding a region [Built]

Move & Hold forces with Holding status and a valid held region defend that whole region rather than a capture-point reaction circle. Once Holding, the region alarm controls destinations at posts and threats; a new explicit command clears that hold state. Idle orphans without an explicit order do not join this system. The earlier playtest gap and layout measurements are recorded in [area-defence.md](../Research/area-defence.md).

**Capture and destination ownership [Built]:** Move & Hold first reaches the region anchor and secures a capturable region before joining shared defend posts. Thereafter the holding driver owns its post or response destination; executor maintenance never reissues the anchor waypoint. Replacing the order clears the holding assignment.

**Rules:**
- **Region alarm.** An alarm fires when a hostile unit enters the region, or when anything you own in it (a building or a force) takes damage. Every force holding that region hears it. Units still fight at their normal weapon range and keep their own automatic targets independently of the force's movement/overlay threat; the alarm only decides *where they go*.
- **Who responds:** the threat is every living hostile unit inside the region plus every living hostile unit outside the border currently damaging something the team owns inside it. Its strength is their summed Power value. Holding forces of the same team, including different commanders, respond in order of distance to the nearest threat unit until their combined living-unit Power reaches 1.25× the threat's. The others keep their posts, so a feint can't pull the whole region to one side. Each responder engages the nearest threat unit within its leash, retaining that target under the no-flip-flopping rule below. When the threat grows, more holders join by the same selection rule.
- **Idle spot:** each holding force waits at one of the region's **defend posts** ([map.md](map.md)), chosen automatically: the post that best sits between the team's buildings in that region and its borders with hostile or not-friendly regions (no fog yet). Each holder takes the post with the fewest holders; ties use the best-placed post. Posts are shared when holders outnumber them, and holders sharing a post stand at distinct formation offsets inside the region. Border clipping cannot merge their assigned positions in usable region geometry; if degenerate or too-thin geometry has no room for distinct positions, the clipped boundary is reused rather than terminating the match. Post occupancy never rejects orders. A force never changes post while an alarm is active.
- **Ground markers [Built]:** see [map.md](map.md).
- **Border rule:** pursuit stops at the region border. While an attacker outside the border is damaging something inside, the force may strike back up to its weapon range past the border; then it returns.
- **No flip-flopping:** a responding force keeps its target until the target dies or leaves the leash, commits to an alarm for at least 8 s, and returns to its post 6 s after the region goes quiet. Starting values.
- **Readable response [Built]:** a line runs from each responder to its threat, and the attributed alert feed logs and voices the start of a player-owned region response episode ([ui.md](ui.md)). JEV responses do not produce that player alert.
- **Force card [Change]:** shows *Responding · Drill Rig under attack*. Replicated responding, threat-kind, threatened-asset and region state is available for the force bar.
- **JEV holds regions under the same rules.**

## Force settings [Built] — commands; presentation [New]

**[Built]** Each force has one owned retreat-threshold setting. **[New]** It is shown and changed on its force card:

| Setting | Options | Default | Applies to |
|---|---|---|---|
| Retreat threshold [Built] | Never / 25% / 40% / 60% of capacity alive | 40% | Attack only |

- **[Built] Never** allows suicide pushes and last stands, which suits a Juggernaut breaking an HQ.
- **The targeting rule replaces the target-priority setting [Built]:**
  - An eligible explicit hostile-structure Attack order keeps priority over automatic targeting.
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
