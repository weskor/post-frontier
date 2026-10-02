# Research: Area defence, guard posts and leashes: lessons for Post-Frontier "Move & Hold"

Scope: how games make units defend a place without micro: what triggers a response, where idle defenders stand, how leashes work, how the player reads it, and how several groups share one area. Each entry follows **Mechanic → why it works (evidence) → applicability → pitfall**. `[INFERENCE]` marks my own reasoning.

---

## 0. What went wrong in the playtest

The symptom: a force held a region, a building inside that region was attacked, and the force did nothing because its idle spot was too far from the building. That is the classic failure of using **unit acquisition range as the only trigger**. Every game below that defends *areas* separates two things:
1. **the trigger**, which is area-scoped (enemy in zone, owned thing damaged, alarm), and
2. **the engagement**, which is unit-scoped (weapon range, target priority).

Scale check `[INFERENCE]`: 200 × 200 m split into 15 regions averages about 2,670 m² per region, which is an equivalent radius of about 29 m. A building on the far edge of a region can easily be 30–50 m from the 4.3 m capture anchor. SC2's default auto-target scan range is only "5 or Weapon Range, whichever is greater" in game units ([Liquipedia: Automatic Targeting](https://liquipedia.net/starcraft2/Automatic_Targeting)). Unit-range sensing covers a disc around the force, not the region polygon. Fixing the response therefore needs a **region-level alarm**, not a bigger aggro radius.

---

## 1. Age of Empires II / IV: stances and Guard

**Mechanic.** AoE2 has four stances ([AoE wiki: Unit stance](https://ageofempires.fandom.com/wiki/Unit_stance)). **Aggressive** chases targets in line of sight "until they are killed". **Defensive** attacks anything in range and follows "for a certain number of tiles before returning to their original position". **Stand Ground** never moves but fires at anything in range. **No Attack** only fights when ordered. AoE4 collapses these to a default between Aggressive and Defensive: units "pursue … but return to their original position if they lose the enemy". It adds **Stand Ground**, which makes the units impassable and gives them a visible "battle-ready pose" (same page). Patrol, Guard and Follow are separate movement orders ([AoK strategy guide, GameFAQs](https://gamefaqs.gamespot.com/pc/63605-age-of-empires-ii-the-age-of-kings/faqs/38481)). The **Town Bell** is a one-button alarm that sends every villager to garrison at once ([AoE wiki: Town Bell](https://ageofempires.fandom.com/wiki/Town_Bell)).

**Why it works.** Stances are a single dial for *how far a unit may leave its post*, and the post is wherever it was last ordered. Defensive's "follow N tiles, then return" is the archetypal leash. AoE4 made the pose visible, so the player can read the stance on the battlefield without opening the UI.

**Applicability.** Move & Hold is effectively "Defensive stance, but the leash is the region polygon, not N tiles". The Town Bell is evidence that a *broadcast* (one event, many responders) is an accepted RTS idiom.

**Pitfall.** Players report that Defensive units in AoE2 DE "tend to let themselves get killed" ([Steam thread](https://steamcommunity.com/app/813780/discussions/0/2441462402302394257)). `[INFERENCE]` The likely cause is a leash that snaps back while the unit is still being hit, which disengages it mid-fight and produces ping-pong.

## 2. StarCraft II: Hold Position, Patrol, Attack-Move, auto-targeting

**Mechanic** ([Blizzard: Basic Unit Controls](https://news.blizzard.com/en-us/article/4552956/game-guide-basic-unit-controls)). **Stop**/idle units "engage and chase enemies attacking them". **Hold Position** never moves, "even if it is being attacked by ranged fire"; Blizzard warns that "a single enemy unit with superior range can pick off your troops one by one." **Patrol** is an attack-move loop that "will resume its normal Patrol route once it has dealt with the enemy". **Attack-Move** engages anything that comes into range.

Auto-targeting runs only under Stop/Hold/Patrol/Attack-Move. It considers targets inside the scan range and ranks them by threat (can it shoot back?), then Attack Target Priority (combat units 20, most buildings 11), then distance. Once a unit picks a target it keeps it until the target becomes invalid or leaves range ([Liquipedia](https://liquipedia.net/starcraft2/Automatic_Targeting)).

**Why it works.** Every order is a rule for **when to stop obeying the move and fight**, and the rules are learnable. Target stickiness avoids jitter. The Thor/Ultralisk "closest angle" rule exists specifically to stop units spinning between equidistant targets (same page).

**Applicability.** Use "threat first, then priority, then distance" for target choice once the force is inside its region, and keep the current target until it dies or leaves the leash.

**Pitfall.** Blizzard's own guide shows both failure modes: **Hold** gets out-ranged, and **Stop/Aggressive** gets baited ("if your enemy baits your units with a single unit to draw them out … they follow of their own accord"). Post-Frontier's region rule has to handle both: JEV attackers firing from just outside the border, and JEV baiting a force across it.

## 3. Warcraft III: creep camps, guard distance, call for help

**Mechanic.**
- **Group alarm.** Creep camps "act as one unit. When one attacks, they all attack … All Creeps will rejoin an attack if any one of them is hit." ([Blizzard: Creep Basics](http://classic.battle.net/war3/neutral/creepbasics.shtml))
- **Leash.** Creeps "will only follow you a certain distance away from their camp … Creeps cannot be dragged via constant attacks; they eventually give up and return to their start location" (same).
- **Tunable constants.** The editor exposes *Creeps – Guard Distance*, *Guard Return Distance* and *Guard Return Time* ([Hive Workshop](https://www.hiveworkshop.com/threads/modify-aggro-range.33709)). It also has *Combat – Call for help Range*: the "maximum distance when a friendly unit will come to help a unit that's attacked" ([WE tutorial](https://world-editor-tutorials.thehelper.net/cat_usersubmit.php?view=21216)).
- **Building triggers.** Creeps attack buildings under construction near them, but "will not seek out and destroy completed buildings" (Creep Basics).

**Why it works.** There are three separate numbers: an engage radius, a leash radius *from home*, and a **return timer**. The timer is what defeats dragging: even if the attacker stays in range, the creeps give up and go home. Call-for-help is the canonical "nearby ally hit → I respond" rule.

**Applicability.** This is the best direct template for Hold. **Call-for-help scope = the region**: any owned structure or force in the region taking damage alerts every force holding that region. **Guard Return Distance = region border + margin.** **Guard Return Time = maximum chase time outside the anchor area.**

**Pitfall.** Creep leashes are *meant* to be exploitable: running away is a designed escape hatch. For player defenders, a hard return timer lets JEV kite indefinitely. Reset the timer only while the force is actually landing damage `[INFERENCE]`.

## 4. Supreme Commander (and Beyond All Reason): Guard/Assist, Patrol, fire state

**Mechanic.** SupCom's **Guard/Assist** makes mobile units "follow and fire on attackers of unit", and engineers keep the target repaired. **Patrol** works as in SC2. A **fire-state toggle** switches between "Attack&Chase" and "just return fire". The **Defense overlay** (Ctrl-E) shows the "area reachable by firepower" ([SupCom wiki: Controls](https://supcom.fandom.com/wiki/Controls)). In BAR, Guard "instructs a unit to persistently follow another unit" and is the default right-click on a friendly unit; Guard orders "only end when the guarded units are destroyed" ([BAR: Guard](https://www.beyondallreason.info/commands/guard)).

**Why it works.** Guarding a *thing* rather than a point means the trigger is "attacks on X", which is exactly the case that failed in the playtest. The coverage overlay makes defensive reach visible at a glance.

**Applicability.** The overlay idea transfers directly: draw each holding force's **response coverage**, showing which owned buildings it can reach within N seconds. "Guard building" could be a variant of Hold aimed at a structure instead of a region (Option D below).

**Pitfall.** Guard is per-target and never expires. Players lose track of which units are tied up guarding what. In a no-micro game, permanent hidden assignments are a readability tax `[INFERENCE]`.

## 5. Company of Heroes and Total War: hold fire, guard mode, retreat points

**Mechanic.**
- **CoH2.** Retreat (T) makes a squad "run back to your headquarters … unable to control it … much less vulnerable"; pinned squads can only retreat ([CoH2 manual, Feral](https://www.feralinteractive.com/en/manual/companyofheroes2/latest/steam)). **Hold Fire** keeps camouflaged units hidden. Territory points tint the minimap blue/red, and upgrades only work in your own territory (same).
- **Total War: Shogun 2** ([Battle interface](https://shogun2-encyclopedia.com/how_to_play/049_enc_manual_battle_conflict_controls.html)). **Guard mode** units "maintain their formation when attacked and don't pursue the enemy if they run away". **Fire-at-will** lets missile units choose targets, **Skirmish** keeps them out of melee, and **Withdraw** walks them off the field. Rome players use Guard mode so archers "stop shooting and stand still" rather than follow a target into a tower ([TW Heaven forum](https://rtw.heavengames.com/cgi-bin/forums/display.cgi?action=st&fn=1&tn=9216)).

**Why it works.** Retreat goes to a **fixed, known destination**, and that predictability is the whole value. Guard mode is a *don't-pursue* flag, separate from *may-I-shoot*.

**Applicability.** Post-Frontier's Retreat should go to a fixed, visible rally region; it should not be computed cleverly. Within Hold, split two concepts: **response radius** (move to threats) and **pursuit** (chase fleeing enemies). Default pursuit to off at the border.

**Pitfall.** Players complain when Total War units auto-switch to the nearest fighting enemy after a rout ([Steam thread](https://steamcommunity.com/app/1142710/discussions/0/6118730946113582516)). Auto-retargeting that drags a force far from its intent reads as disobedience.

## 6. Northgard: the zone is the order

**Mechanic.** Military orders are zone-level: "select your military units and tap on the tile you want them to fight" ([Northgard Help Center](https://playdigious.helpshift.com/hc/en/4-northgard/faq/84-how-do-i-attack-enemies)). The **Watch Tower** "protects this zone from enemies and oversees adjacent zones", and only one may be built per area ([Northgard wiki](https://northgard.fandom.com/wiki/Watch_Tower)). Some bonuses count units "in the same zone" ([Warchief](https://northgard.wiki/Warchief), [Varangian Guard](https://northgard.wiki/Varangian_Guard)).

**Why it works.** The zone is both the order target and the scope of defence and bonuses, so there is one spatial unit to learn. `[INFERENCE]` That only works because zones are small enough that "in the zone" ≈ "in the fight".

**Applicability.** Post-Frontier already shares this model (regions drive capture, supply, fog and orders). The playtest shows our regions are *larger relative to unit sensing* than the rule assumes. Either shrink the effective defence area (posts) or make the region itself the sensor.

**Pitfall.** Zone-scoped rules invite **border play**: fighting on the line between two zones, where neither zone's defence fully applies `[INFERENCE]`.

## 7. Thronefall and Kingdom Two Crowns: posts, frontier-facing idles, retreat lines

**Mechanic.**
- **Thronefall.** Holding Ctrl places units and draws "white circles on the ground for each unit", marking their hold positions. An option resets units to the same posts each morning ([Steam thread, dev reply](https://steamcommunity.com/app/2239150/discussions/0/7410182856433786146)).
- **Kingdom, knights.** Idle knights stand "near the Kingdom's defensive line at the outer wall"; at night they pull "just inside the outer wall". If the wall is about to fall, they "retreat with their companies to the next wall" ([Kingdom wiki: Knight](https://kingdomthegame.fandom.com/wiki/Knight)).
- **Kingdom, archers.** At sunset archers run to the nearest outer wall, and "if one side … has more archers than the other side, then some archers might walk to the other side to even out the numbers" ([Archer](https://kingdomthegame.fandom.com/wiki/Archer)).

**Why it works.** Idle defenders stand **at the frontier facing the threat**, not at the town centre, and the fallback is the next fortified line. Side balancing is a crude but readable load-balancer.

**Applicability.** This is the strongest evidence for Q2: idle posts should sit **between the enemy approach and the assets**. Retreat-to-next-line maps onto Retreat-to-adjacent-friendly-region.

**Pitfall.** Both games shipped fixes for exactly our failure modes. Thronefall players reported archers that "Leeroy Jenkins into battle out in the open", and units that "run from their set position towards distant enemies" ([Elliot Smith review](https://www.elliotcsmith.com/thronefall/)). Kingdom patch notes fixed archer gaps at crowded walls ([patch notes](https://kingdomthegame.fandom.com/wiki/Patch_notes_for_Two_Crowns)). The Kingdom wiki notes that archers migrating to a new wall "will not stop migrating, making them run straight into the Greed" (Archer). **Never move a post while its owners are under threat.**

## 8. Bad North: place on a tile, units decide the rest

**Mechanic.** "Players will be simply positioning their squads on a grid and then each of the units in that squad decide how/when to attack from there" ([Plausible Concept interview, Nintendo UK](https://www.nintendo.com/en-gb/News/2018/April/Interview-Taking-on-hordes-of-invading-Vikings-in-Bad-North-1368315.html)).

**Why it works.** In the developers' words: "the simulation … is hidden away from the player … so everything that happens in the combat needs to be visible", and "it's much easier to understand and predict the outcome of your positioning if you have a discrete possibility space".

**Applicability.** Bad North is the purest "place force, AI fights" model. Its lesson is that **discrete, visible posts** make the AI predictable. Post-Frontier's continuous regions would benefit from a small set of visible posts per region.

**Pitfall.** Bad North islands are tiny, so each squad's local behaviour covers the whole island. On a 30 m-radius region, the same local behaviour leaves coverage gaps, which is our playtest bug `[INFERENCE]`.

## 9. Hearts of Iron IV: garrison areas and front lines

**Mechanic.** **Garrison Area:** "Divisions will spread out to guard the most important provinces … or take back provinces as long as it's pretty safe." **Fallback lines:** divisions "instantly rush back and hold that point". **Design rule:** "The system is specifically not allowed to be clever, that is the player's job", with "control and feedback … so you would not be surprised by the system doing things you didn't tell it" ([HoI4 Dev Diary 45](https://store.steampowered.com/news/posts?appids=394360&enddate=1456486596)).

**Why it works.** Automation is scoped to "areas you don't care so much" about, it is predictable, and the player can override it.

**Applicability.** HoI4's "spread to guard the most important provinces" ≈ "station near the most valuable buildings in the region". The "not clever" rule argues for deterministic post selection.

**Pitfall.** Reallocation that is driven by threat can stack. Players report AI fronts that "death stacked 40+ divisions on a single tile and left the entire French-Italian border undefended" ([Steam thread](https://steamcommunity.com/app/394360/discussions/0/1698293255129415535)). Cap how many forces a single alarm can pull.

## 10. Halo 2/3 AI: areas, firing points, prioritised tasks with capacity

**Mechanic.**
- **Halo 2 zones and areas.** Halo 2 zones contain **areas**, which are sets of designer-placed firing points with a "margin of error" radius. An area can define an "allowable region", for example "defend this region and under no circumstances can you leave it".
- **Halo 2 orders.** Orders pair a location with a style (idle/defend/attack). They have ending conditions such as "if enemy sighted" or "**if alerted by squad X**" ([Halo 2 AI Engineering Outline, Microsoft](https://learn.microsoft.com/en-us/halo-master-chief-collection/h2/ai/aiengineeringoutline)).
- **Halo 3 Objectives.** Designers declare a tree of tasks with **priority** and **capacity** (e.g. "guard the door, but if you can, also guard the hallway"). Squads are "poured in at the top" and filter down, get pulled UP when a higher-priority task activates, and pushed DOWN when theirs deactivates. Tasks can **latch** on/off to avoid flapping ([Isla, GDC 2008 slides](https://web.cs.wpi.edu/~rich/courses/imgd4000-b12/lectures/halo3.pdf)).

**Why it works.** An area is discrete posts plus tolerance, which is the Bad North lesson in continuous space. The declarative task tree answers "several groups, one area" without designers writing O(n²) transitions (Isla's "Problems with the Imperative Method").

**Applicability.** This is the best model for Q5. A held region exposes tasks (*anchor*, *each high-value building*, *each hostile-facing border*), and holding forces fill them by priority and capacity. An alarm activates a "respond at X" task that pulls the nearest force up, and latching gives hysteresis.

**Pitfall.** Isla lists "requires designer training" and notes "the squad wasn't always the best level at which to do the bucketing". Keep the task set tiny and generated from region data.

## 11. RTS bot base defence (UAlbertaBot) and terrain analysis

**Mechanic.** UAlbertaBot's `updateDefenseSquads` ([source](https://github.com/davechurchill/ualbertabot/blob/master/UAlbertaBot/Source/CombatCommander.cpp)) collects, for each owned base region, the enemy units **whose position is inside the region**, ignoring the first worker (a scout). It then creates a "Defend Region!" squad with a fixed radius, requesting **2 defenders per enemy** split air/ground, and disbands the squad when no enemy is within that radius.

Region and choke decomposition (Perkins; the BWTA library) is the standard terrain abstraction for this ([Churchill et al., RTS AI: Problems and Techniques](https://davechurchill.ca/publications/pdf/ecgg15_chapter-rts_ai.pdf)).

**Why it works.** The trigger is region membership, not unit sight. The response is proportional, so you don't send everything, and disbanding is explicit.

**Applicability.** Post-Frontier's JEV needs the same logic for its own defence. For player forces, the "inside region → alarm" rule is exactly the fix. Proportional allocation is how to answer two simultaneous threats.

**Pitfall.** Pure position-in-region misses attackers **outside the border shooting in**. Add "anything damaging an asset in the region" as a second trigger `[INFERENCE]`.

---

## Answers to the five questions

**1. What triggers a defensive response?** Layered triggers, cheapest first: **enemy in weapon/scan range** (universal: SC2, AoE; necessary but not sufficient, and the only trigger in our playtest); **enemy inside the zone** (UAlbertaBot, Northgard Watch Tower); **a friendly unit or structure taking damage** (SupCom Guard "fire on attackers of unit"; WC3 call-for-help range); **group broadcast** (WC3 camp "all rejoin if any one is hit"; Town Bell; Halo "alerted by squad X"). For Post-Frontier, use **region membership + damage-to-owned-in-region**, broadcast to every force holding that region.

**2. Where should idle defenders stand?** Evidence favours the **threat-facing frontier, between the enemy and the assets**: Kingdom knights and archers at the outer wall, Thronefall chokepoint placement, Halo firing points, HoI4 "most important provinces". The region centre or capture point is right only when the region is small relative to response range (Northgard, Bad North). With our region size (≈29 m equivalent radius) `[INFERENCE]`, the anchor alone is the wrong idle spot when buildings sit at the region edge.

**3. Leash rules and anti-exploit.** Use three separate parameters (WC3): engage radius, **return distance** (from home or the zone) and **return time**. Against exploits:
- **Bait-pulling:** stop pursuit at the region border plus a small margin (TW Guard mode, AoE Defensive).
- **Out-range sniping from outside the border:** allow an "edge sortie" up to weapon range beyond the border while the attacker is damaging region assets (SC2 Hold's failure mode).
- **Ping-pong:** sticky targets (SC2), latched tasks (Halo 3), a minimum commit time, and a cap on forces per alarm (the HoI4 stacking pitfall).

**4. Readability without micro.** Show posts on the ground (Thronefall circles, Bad North tiles). Show coverage (SupCom Ctrl-E). Mark territory on the minimap (CoH, WC3 camp colours). Give stances a visible pose (AoE4). Preview the plan before it runs (HoI4 arrow preview). Never surprise the player (HoI4 DD45).

**5. Several forces in one zone.** Uncoordinated stacking is the default, and it fails (Kingdom archer gaps, HoI4 death-stacks). Working coordination is **prioritised slots with capacity** (Halo 3), **side balancing** (Kingdom archers), or **proportional allocation** (UAlbertaBot 2:1).

---

## Concrete "Move & Hold" behaviour options

All four options share the same base: the trigger is region-scoped (enemy inside the region polygon, OR any owned structure/force in the region damaged) and broadcast to all forces holding the region. Inside the fight, the force uses existing unit targeting (threat → priority → distance, sticky).

**Option A — Anchor + region alarm (smallest change).**
- *Idle:* at the capture anchor.
- *Response:* on alarm, the force attack-moves to the threatened asset.
- *Leash:* region border + 5 m. Pursuit ends at the border. Edge sortie is allowed while the attacker damages region assets. Return to the anchor after 6 s with no alarm.
- *Pros:* trivial to explain ("they defend the whole region"), cheap, fixes the playtest bug outright.
- *Cons:* response travel time. At ~4 m/s `[INFERENCE]`, a 40 m dash is ~10 s of free damage on edge buildings. Two threats on opposite edges cause visible ping-pong.

**Option B — Frontier post (Kingdom/Halo firing-point model).**
- *Idle:* a computed post = value-weighted centroid of owned structures in the region, pulled toward borders shared with hostile or unscouted regions (region graph + fog data already exist). Designers may author override posts per region in each handcrafted map.
- *Rules:* recompute only when the region graph changes and no alarm is active (Kingdom migration pitfall). Show the post as a ground marker. Response and leash as in A.
- *Pros:* idle position already sits between JEV and the assets, so most responses are short. It reads as "they're guarding the front".
- *Cons:* less obvious than "at the flag". Requires good post computation for reshuffled region roles. Rear raids, if JEV gets fog blips, still need the alarm.

**Option C — Slots with capacity (Halo 3 tasks / HoI4 garrison).**
- *Slots:* each held region generates a small slot list, filled in order: (1) hostile-border post (priority 3, capacity 1 force); (2) top-value structure (priority 2, capacity 1); (3) anchor (priority 1, unlimited).
- *Alarm:* an alarm adds a temporary "respond at X" slot (priority 4). The nearest force is pulled up and latched for ≥8 s or until the threat clears. At most ⌈threat strength / force strength⌉ forces are pulled, which avoids death-stacks.
- *Pros:* answers Q5 cleanly; two or three co-op players' forces in one region spread automatically and visibly.
- *Cons:* most AI work. A single force behaves exactly like B, so the payoff only appears with ≥2 forces. Slot churn must be latched or it flaps.

**Option D — Hold on a structure (SupCom/BAR Guard).**
- *Order:* clicking an owned building with Move & Hold makes the force idle adjacent to it. The trigger becomes "this building or anything within R of it". Leash: R + 10 m.
- *Pros:* exact, readable, zero ambiguity about what is protected.
- *Cons:* adds an order target type, which nudges toward micro. It conflicts with "regions drive everything" (capture, supply), and the player must re-issue the order when the threat moves elsewhere in the region.

**Recommendation `[INFERENCE]`.** Ship **A's region alarm now**, because it fixes the bug on its own. Pick idle positions with **B**, and use **C's slot rules only when ≥2 forces hold the same region**. Skip D unless playtests show players want to protect one key building.

---

## Traps

1. **Range-only triggers.** Sensing by unit scan range on a region far larger than that range (the playtest bug).
2. **Leash = acquisition radius.** The response area and the chase area are different numbers (WC3).
3. **Hard return timers.** They let JEV kite forever; reset the timer only while the force is dealing damage.
4. **Border sniping.** Out-ranged attackers just outside the polygon (the SC2 Hold failure).
5. **Pursuit across the border.** Bait pulls the force out (SC2/Thronefall "Leeroy Jenkins").
6. **Moving posts mid-threat.** Kingdom archers walking into the Greed.
7. **Every force answers every alarm.** Leaves other regions empty (HoI4 death-stack).
8. **Flapping between equidistant threats.** Needs stickiness, latching and a minimum commit time.
9. **Crowding at one post.** Kingdom archer gaps; give each force a distinct slot.
10. **A "clever" executor.** Surprising the player breaks trust (HoI4 DD45).

## Ranked lessons

1. **Separate trigger from engagement:** region-scoped alarm, unit-scoped fighting (WC3, UAlbertaBot, SupCom Guard).
2. **Broadcast:** one hit alerts every holder of the region (WC3 camps, Town Bell, Halo "alerted by squad").
3. **Idle between threat and assets,** not at the centre (Kingdom, Halo firing points, HoI4).
4. **Three leash numbers** (engage, return distance, return time) with the region border as the return distance (WC3, AoE Defensive).
5. **Hysteresis everywhere:** sticky targets, latched tasks, minimum commit (SC2, Halo 3).
6. **Proportional, capped response** to multiple threats (UAlbertaBot, the HoI4 pitfall).
7. **Visible posts and coverage** (Thronefall circles, SupCom overlay, Bad North tiles, AoE4 pose).
8. **Predictable over clever;** Retreat goes to a fixed known place (HoI4 DD45, CoH).

## Open questions

1. What are actual unit move speeds and weapon ranges? These set whether A's travel time (~10 s `[INFERENCE]`) is acceptable or B/C are required.
2. Should a holding force sortie beyond the border to hit attackers shooting in, and how far: weapon range, or a fixed margin?
3. Is "Hold" one behaviour, or do we expose a single Firm/Defensive toggle (AoE/TW precedent), at the cost of one more decision?
4. With 2–3 co-op players holding the same region, do forces of *different owners* share slots (C), or does each player's force act independently?
5. Should JEV be allowed to read player posts? Visible posts invite deliberate rear raids. Is that good counterplay or a frustration source?
6. Do region traits change post selection (e.g. prefer the high-ground or cover sub-area as the post)?
7. Do structures themselves raise the alarm (a cheap "bell" building), or is the alarm always automatic?
8. What does the player see when a force is responding: a line from force to threatened building, a minimap ping, or both?
