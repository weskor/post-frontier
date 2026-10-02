# Post-Frontier — Input research: interacting with buildings and forces

Scope: how comparable games handle selecting groups, giving orders, running production and supporting controllers, and what that means for Post-Frontier's force/region model.
Rules: every claim about a game links a source. My own conclusions are tagged **[INFERENCE]**. Entry format: **Mechanic → why it works (evidence) → applicability → pitfall**.

---

## 1. Standard RTS controls players already know

**1.1 Smart right-click (StarCraft II).** Right-click on open ground moves; right-click on an enemy attacks it. Attack-move is a separate verb (`A` then click).
- Why it works: right-click covers the most common case. The SC2 guide also explains why the second verb exists: units on Move ignore ambushes, and attack-move is "by far the safest way to move units". Units ordered to attack one target stop once it dies ([Blizzard: Basic Unit Controls](https://news.blizzard.com/en-us/article/4552956/game-guide-basic-unit-controls)).
- Applicability: Post-Frontier has three verbs. Two of them are cleanly decided by the target type: region → Move & Hold, hostile structure → Attack. Attack-a-region and Retreat are the cases a smart click cannot decide.
- Pitfall: the SC2 guide warns that missing a target with a right-click quietly turns it into a Move ([same](https://news.blizzard.com/en-us/article/4552956/game-guide-basic-unit-controls)). A misclick changes the verb, not just the location. **[INFERENCE]** Large targets that snap the cursor (whole regions, structure footprints) reduce this.

**1.2 Mouse-button convention is muscle memory.** AoE2: DE once defaulted to left-click orders. Players looked for the "Two Button Mouse" option to get right-click orders back ([AoE forum](https://forums.ageofempires.com/t/left-click-unit-movement/69993)).
- Applicability: left-click selects, right-click orders. Post-Frontier should not invent its own mapping, and should allow remapping. AoE IV ships with every key remappable ([AoE IV shortcuts](https://www.ageofempires.com/news/aoeiv-shortcuts-revealed)).
- Pitfall: a verb-first-only scheme, where right-click does nothing, will feel broken to RTS players. **[INFERENCE]**

**1.3 Control groups, double-tap to jump the camera, select-all-of-type.** In SC2, Ctrl+number assigns a group and pressing the number twice centres the camera on it. Ctrl-click or double-click selects every unit of that type on screen. Shift-click adds or removes units ([Blizzard: Special Control](https://news.blizzard.com/en-us/article/4552955/game-guide-special-control)). Halo Wars DE uses the same pattern: press once to select, double-press to focus the camera ([Halopedia](https://www.halopedia.org/HW:Control_schemes)).
- Applicability: Post-Frontier's keys 1–9 per force are fixed control groups that the player never has to create. Use the same rule: press once to select, press twice to jump the camera.
- Pitfall: manual grouping confuses players. A Total War: Warhammer II player could not regroup or ungroup units with Ctrl+number and asked "why is this so complicated" ([Steam thread](https://steamcommunity.com/app/594570/discussions/0/1751268668283178163)). Giving each force a fixed identity avoids this. **[INFERENCE]**

**1.4 Selecting must not move the camera.** AoE IV's "select all military buildings" key originally jumped the camera, which made it "unusable" for players who wanted to queue units while watching a fight. Players asked for: tap to select, double-tap to centre. An option for "Select Only" was added ([AoE forum](https://forums.ageofempires.com/t/select-all-x-building-type-hotkeys-should-not-re-focus-your-camera/179434)).
- Applicability: clicking a force card or pressing 1–9 should select only. Moving the camera should need a second, deliberate action (double-tap, or a "go to" button on the card).

**1.5 Grid hotkeys and building groups.** AoE IV puts building construction on a spatial QWE/ASD/ZXC grid. F1 selects all military buildings and Tab cycles between types ([AoE IV shortcuts](https://www.ageofempires.com/news/aoeiv-shortcuts-revealed)). SC2 players bind all production buildings to one group, use Tab to switch sub-types, and set one rally point for all of them ([Blizzard: Special Control](https://news.blizzard.com/en-us/article/4552955/game-guide-special-control)).
- Applicability: these keys only make sense when there are many buildings. Post-Frontier has one building per force, so the "production group" is the force bar itself. **[INFERENCE]**

## 2. Selecting groups instead of units

**2.1 Unit cards as the main selector (Total War).** Players select units by clicking them, box-selecting, Ctrl-clicking, or clicking the unit cards along the bottom of the screen. Right-click moves; right-click-drag sets a formation; Ctrl+A selects the whole army ([Total War Academy](https://academy.totalwar.com/battle-keyboard-and-mouse-controls)).
- Why it works: a card is always on screen and always clickable, even when the unit is off-camera or hard to see.
- Applicability: this matches Post-Frontier's bottom force bar directly. A card should show at a glance: what the force is doing (holding / moving / attacking / retreating), its strength, and its production status.
- Pitfall: Total War players report that card order does not set formation order ([r/totalwar](https://www.reddit.com/r/totalwarhammer/comments/vo8wis/trouble_with_unit_cards_and_formations)). Bar order should mean force number and nothing else. **[INFERENCE]**

**2.2 Squad badges (Company of Heroes).** In CoH2 you select a squad by clicking the shield icon above its soldiers or the matching icon in the top-right of the screen ([IGN CoH2 guide](https://www.ign.com/wikis/company-of-heroes-2/Starter_Guide), seen in a search snippet; the page itself returned 403).
- Applicability: this is Post-Frontier's map badge plus card. One click on either selects the whole force; individual units are never selectable.

**2.3 Zone-based combat (Northgard).** Units fight enemies in their own zone and ignore units across the zone border. "Put your warband in the same zone as an enemy and they will fight" ([Steam thread](https://steamcommunity.com/app/466560/discussions/1/595137983640786305)). `E` selects the whole warband. Double-clicking a unit selects all units of that type in the same tile ([Steam guide](https://steamcommunity.com/sharedfiles/filedetails?id=2014417078)).
- Applicability: this is the closest commercial precedent for "Move & Hold a whole region". The region is both the order target and the scope of combat.
- Pitfall: the same guide says control groups are "not mentioned anywhere in the game interface" ([guide](https://steamcommunity.com/sharedfiles/filedetails?id=2014417078)). Every key needs an on-screen prompt.

**2.4 Few squads, one click to send (Bad North).** The player has up to four units of about eight soldiers. The game is about moving them to the right beach and sending them to houses to replenish. The reviewer said moving troops "feels like playing with a small box of toys" ([PC Gamer](https://www.pcgamer.com/bad-north-review/)).
- Applicability: with 2–6 units per force and a handful of forces, Post-Frontier is at the same scale. Select a force, click a destination. The game should handle formation and targeting itself.

## 3. Production, upgrades and per-group purchases

**3.1 Production at the building (SC2, AoE).** You select a building to use its production card. AoE2 on Xbox: select a building with A, then hold RT to train units or research ([AoE Xbox guide](https://www.ageofempires.com/learn-to-play/controlling-your-empire-gathering-resources-xbox)). The AoE IV Xbox radial gives access to "just about every action": constructing, training, formations, upgrades ([Richardson, AoE IV Xbox UI](https://craig-richardson.net/2023/11/20/age-of-empires-iv-creating-the-xbox-ui/)).
- Applicability: tier upgrades and perks are rare, spatial decisions. They belong in the building inspector.
- Pitfall: to produce during a fight, the player has to leave the fight. This is the reason for the camera-jump complaint in 1.4.

**3.2 Build from anywhere (Command & Conquer sidebar).** You only need the factory to exist; you never select it. Queues are split into tabs (bound to Q/W/E/R since Red Alert 2). With several factories, units come out of the one marked "primary" ([C&C Wiki: Sidebar](https://cnc.fandom.com/wiki/Sidebar)). Battle Aces cuts further: buildings and bases are "a button tap away" so players can focus on combat decisions ([CNET](https://www.cnet.com/tech/gaming/battle-aces-is-streamlined-starcraft-for-fast-fun-pvp-matches/)). Its director asked why players should have to master basic execution "before they can experience the core fun" ([PC Gamer](https://www.pcgamer.com/games/strategy/blizzard-veteran-david-kims-strategy-comeback-with-battle-aces-is-very-personal-i-just-cant-accept-the-end-all-peak-of-rts-is-starcraft-2-and-nothing-can-ever-be-better/)).
- Applicability: in Post-Frontier, the force card can carry the force's production controls (queue, pause, reinforce) because building and force are one-to-one. That gives build-from-anywhere without a separate global sidebar. **[INFERENCE]**

**3.3 Rally point = where the army is (SC2).** If production buildings share a control group with the army, a right-click on an attack target also moves the buildings' rally point to the army ([Blizzard: Special Control](https://news.blizzard.com/en-us/article/4552955/game-guide-special-control)). Supreme Commander can send a factory's output along a transport ferry route automatically ([Wikipedia](https://en.wikipedia.org/wiki/Supreme_Commander_(video_game))).
- Applicability: in Post-Frontier, new units should join their force wherever it is. No rally points to manage. **[INFERENCE]** Open question: do reinforcements walk there, and can they be intercepted? (Q3)

**3.4 Fixed building slots (Thronefall, Halo Wars).** In Thronefall, the designers removed the choice of what to build and where. That made level design easier and capped unit counts without a supply system. They also note that players who are told "strategy game" expect free building, so expectations must be managed ([Game Developer](https://www.gamedeveloper.com/design/mastering-minimalism-and-layering-complexity-with-strategy-game-thronefall)). Halo Wars moved from free placement to locked base slots (7 per base) ([Wayward Strategy](https://waywardstrategy.com/2020/03/23/halo-wars-the-ultimate-design-for-console-rts/)).
- Applicability: Post-Frontier keeps a free grid inside controlled regions. Placement is the one interaction that needs precise cursor control. On controller, snap placement to grid cells. **[INFERENCE]**

## 4. Games built for or ported to controller

**4.1 Halo Wars (Ensemble).** A selects; holding A paints a selection brush; X gives the contextual move/attack; Y uses the special ability. RB selects all local units, LB all units. The D-pad cycles armies, bases and the last alert ([Halopedia: control scheme](https://www.halopedia.org/HW:Control_schemes)). Double-tapping A selects all of a type ([Wayward Strategy](https://waywardstrategy.com/2020/03/23/halo-wars-the-ultimate-design-for-console-rts/)). The PC port maps the eight radial slots to keys laid out in the same shape: Q W E / A · D / Z X C ([Halopedia](https://www.halopedia.org/HW:Control_schemes)).
- Why it works: select-local and select-all replace most box-selecting. One contextual order button replaces a verb menu.
- Applicability: high. Map Post-Frontier's verbs to radial slots and use the same spatial key layout on PC, so both input methods share one mental map.
- Pitfall: Halo Wars 1 had no custom groups and no hold position; Halo Wars 2 added both ([Wayward](https://waywardstrategy.com/2020/03/23/halo-wars-the-ultimate-design-for-console-rts/)). Forum players called its controls "oversimplified to a fault" ([GameFAQs](https://gamefaqs.gamespot.com/boards/935835-halo-wars/48250198)). Post-Frontier's fixed forces cover groups, and Move & Hold covers hold position.

**4.2 Age of Empires II/IV on Xbox.** A is contextual: move on terrain, attack on an enemy. With a building selected, A sets the gather point. Holding LT while training queues five units at once. The D-pad jumps straight to idle villagers, the Town Center, military units or support units ([AoE Xbox guide](https://www.ageofempires.com/learn-to-play/controlling-your-empire-gathering-resources-xbox)). Control groups are slots on an LB radial; Y on a radial slot moves the camera there. Y is also a player-assignable "most common action" button. **Site-based commands:** holding RT with nothing selected opens a radial for whatever is under the cursor. On terrain it can send military or siege there; on an enemy it can order an attack ([AoE Xbox advanced](https://www.ageofempires.com/learn-to-play/match-goals-advanced-controls-xbox)). AoE IV also added a Villager Priority System so the controller player sets economy priorities instead of micromanaging villagers ([Can I Play That](https://caniplaythat.com/2023/08/25/age-of-empires-iv-on-console-adds-accessibility-with-new-controls/)).
- Applicability: site-based commands are a ready-made model for picking the target first and then the force or verb (Option C below).
- Pitfall: AoE IV radial slots move when units are upgraded, and the order is inconsistent between civilizations. Players ask for fixed slots so they can build muscle memory ([AoE forum](https://forums.ageofempires.com/t/xbox-radial-menu-overhaul/284721)). The developers paged the radial and gave designers per-slot placement metadata ([Richardson](https://craig-richardson.net/2023/11/20/age-of-empires-iv-creating-the-xbox-ui/)).

**4.3 Northgard on Switch.** Most menus go on one stick-driven wheel. The review praised the mapping but noted that hover tooltips are lost ([Nintendo World Report](http://www.nintendoworldreport.com/review/51799/northgard-switch-review)).
- Pitfall: if information appears only on mouse hover, it is missing on controller. Put it on the focused card or in an inspector instead. **[INFERENCE]**

**4.4 Company of Heroes 3 console edition.** Clicking L3 enters tactical pause; dotted lines show each unit's queued path. The reviewer called pause "the highlight". The same reviewer cited the depth of the control wheels as "an overload of options" ([Jump Dash Roll](https://www.jumpdashroll.com/article/company-of-heroes-3-console-edition-review)).
- Pitfall: nested ability wheels. Post-Frontier's three verbs fit in one ring.

**4.5 They Are Billions on Xbox (negative example).** This port used a virtual mouse cursor with acceleration that made precise selection hard, could not be remapped and had no tutorial. LB stood in for Shift-queue. The review: "abysmal" gamepad support, despite pause being available ([Windows Central](https://www.windowscentral.com/they-are-billions-xbox-review)).
- Lesson: pausing does not make a mouse UI driven by a free cursor usable on a controller. Targets need snapping. **[INFERENCE]**

**4.6 Pikmin: whistle and throw.** Holding the whistle grows a circle around the cursor, and every Pikmin inside it joins your group. In Hey! Pikmin, one tap calls every Pikmin on screen. Pikmin 3 Deluxe made a short whistle pause busy workers instead of pulling them off their task ([Pikipedia: Whistle](https://www.pikminwiki.com/Whistle)). The series is built around "Dandori": planning tasks and assigning groups efficiently ([Nintendo, Ask the Developer](https://www.nintendo.com/en-ca/whatsnew/ask-the-developer-vol-10-pikmin-4-part-1/)).
- Applicability: a "recall" works as a group verb that uses only position. Post-Frontier's Retreat could be a single global or local button that needs no target. **[INFERENCE]**

## 5. Strategy with few inputs

**5.1 Supreme Commander strategic zoom.** Zooming all the way out turns the view into a full-screen map of icons, and you keep giving orders. Holding Shift shows every queued order on the map, and they can be edited. A coordinated-attack order matches arrival times ([Wikipedia](https://en.wikipedia.org/wiki/Supreme_Commander_(video_game))). PC Gamer praised the zoom; Eurogamer still said the game "feels like hard work" (same page).
- Applicability: this is the war-table view. Orders given there should be the same as orders given on the ground, and queued orders should be visible and editable there.

**5.2 Tactical pause (They Are Billions, Company of Heroes 3).** CoH3 allows "everything you can do normally" while paused, but only in single-player ([PC Gamer](https://www.pcgamer.com/company-of-heroes-3s-tactical-pause-system-is-a-game-changer-for-real-time-strategy/)).
- Applicability: this works for solo play. In co-op, one player cannot freeze the others. **[INFERENCE]** Open question Q6.

**5.3 Kingdom Two Crowns: indirect control.** The monarch spends coins and subjects act on their own ([Wikipedia](https://en.wikipedia.org/wiki/Kingdom_Two_Crowns)). Critics praised the minimalism but found the "lack of direct control over the villagers annoying" (same page).
- Pitfall: for "strategy, not micro" the line should be no unit micro. Forces must still respond to orders immediately. **[INFERENCE]**

## 6. Drag-and-drop orders

**6.1 Tap or drag (Clash Royale Merge Tactics).** "Tap a card to instantly deploy your troop, or drag and drop it to choose a precise position" ([Supercell support](https://support.clashroyale.com/hc/en-us/articles/49484923863067-Merge-Tactics)). Players also discover the tap option late ([r/ClashRoyale](https://www.reddit.com/r/ClashRoyale/comments/4kx0h8/tap_to_deploy_cardyea_i_just_figured_that_out)).
- Pitfall: WCAG 2.2 SC 2.5.7 requires any drag action to have a single-pointer alternative, because some players cannot hold a button while moving the pointer ([W3C](https://www.w3.org/WAI/WCAG22/Understanding/dragging-movements.html)). On a controller, drag becomes "pick up, move cursor, put down", which is just choose-then-target in slower form. **[INFERENCE]**
- Applicability: keep card/badge drag as a mouse shortcut. Make sure every drag-only result can also be done with click-then-click.

## 7. Answers to the six questions

**Q1 — Smart click or choose a verb first?** RTS players expect a smart right-click for the common case and a verb key for the exceptions. SC2 keeps attack-move as a separate key because a plain move is unsafe ([Blizzard](https://news.blizzard.com/en-us/article/4552956/game-guide-basic-unit-controls)). Halo Wars and AoE on Xbox both make the main button contextual ([Halopedia](https://www.halopedia.org/HW:Control_schemes); [AoE Xbox](https://www.ageofempires.com/learn-to-play/controlling-your-empire-gathering-resources-xbox)). Recommendation **[INFERENCE]**: make right-click (and controller X) smart:
- own or neutral region → Move & Hold;
- hostile structure → Attack;
- hostile region → open question (Q1).

Keep Attack-region and Retreat as explicit verbs: hotkey, card button or radial slot. Verb-first stays available as the explicit path; it should not be the only path.

**Q2 — What clicks should do** **[INFERENCE]**, based on 1.3, 1.4, 2.1 and 2.2:
- Left-click a unit or badge → selects its whole force.
- Left-click a card → selects the force. Double-click the card → jump the camera to the force.
- Left-click your own building → opens the building inspector and highlights the linked force's card.
- Left-click a region with nothing selected → region info (owner, threat, forces present).
- Right-click a region or structure with a force selected → smart order.

Link building and force through shared colour or number, and show the force number on the building's nameplate.

**Q3 — Where production, upgrades and per-force purchases live.**
- Rare, spatial decisions (tier upgrade, perks, placement) → building inspector (3.1).
- Frequent actions (queue/pause production, reinforce, current queue state) → also on the force card, so the player never has to leave the fight (1.4, 3.2).
- Global panel → only a read-only summary (the C&C sidebar problem does not exist with one building per force). **[INFERENCE]**

**Q4 — Controller without drag or many hotkeys.**
- Contextual primary order button.
- Select-local / select-all on the bumpers.
- D-pad to cycle forces and alerts.
- One radial with fixed slots for the verbs.
- A site menu on the target.
- Cursor snapping to regions and structures.

Sources: 4.1, 4.2, 4.5.

**Q5 — Fewer clicks for repeated actions.**
- Multi-select, then one order.
- Shift queue, shown and editable on the map (SupCom).
- Batch training (LT + train = 5 in AoE Xbox).
- A player-assignable "favourite action" button (AoE Xbox Y).
- Production follows the force (SC2 rally trick).
- One-press recall (Pikmin).

Sources: 1.3, 3.3, 4.2, 4.6, 5.1.

**Q6 — Common complaints.**
- Camera jumps on select (1.4).
- Radial slots that move around (4.2).
- Oversimplified controls with no grouping (4.1).
- Confusing manual grouping (1.3).
- Cursor acceleration and no remapping (4.5).
- Lost hover tooltips (4.3).
- Wheel overload (4.4).
- Attack-move units distracted by low-value buildings ([AoE forum](https://forums.ageofempires.com/t/patrol-attack-move-modifications/204173)).
- Lack of direct control (5.3).
- Hidden, undocumented keys (2.3).

## 8. Lessons, ranked by relevance to Post-Frontier

1. The force is the thing you select; the region is the thing you target. Both should be large, snappable click targets (2.2, 2.3, 4.5).
2. Make the common order a single right-click (or X). Give the ambiguous verbs a fixed key or radial slot (1.1, 4.1).
3. Selecting never moves the camera; double-tap does (1.3, 1.4).
4. Put the force's production state and its "reinforce" control on the force card. Put tiers and perks in the building inspector (3.1, 3.2).
5. One radial ring of three or four verbs in fixed positions. The same layout on PC keys (4.1, 4.2).
6. A site menu on a region or structure lets the player pick the target first, which suits the war-table view and controller (4.2, 5.1).
7. Queued orders are always visible and editable on the ground and on the war table (5.1, 4.4).
8. Drag is an optional mouse shortcut. Every result also has a click-then-click path (6.1).
9. Every key gets an on-screen prompt. Nothing is shown only on hover (2.3, 4.3).

## 9. Traps

- **Verb-first as the only path.** It costs an extra input on every order and feels broken to RTS players expecting right-click (1.2). **[INFERENCE]**
- **Region Attack vs Move & Hold decided by the game.** If the player cannot tell in advance which one a smart click will give, misclicks change the verb (1.1).
- **Order mode that stays on after an order.** A verb chosen and then forgotten fires on the next click. **[INFERENCE]** Cancel the mode after one order unless Shift is held.
- **Radial slots built from whatever is available.** They shift and break muscle memory (4.2).
- **Hover-only information.** It disappears on controller (4.3).
- **A free virtual cursor with no snapping on controller** (4.5).
- **Pause as the answer to difficulty in co-op** (5.2).
- **Too little control.** Players resent forces that do not react to orders (5.3).
- **Attack orders distracted by low-value targets.** AoE players complain that attack-moving units stop to hit houses and farms ([AoE forum](https://forums.ageofempires.com/t/patrol-attack-move-modifications/204173)). Attack should target defenders first. **[INFERENCE]**

## 10. Interaction-model options

**Option A — RTS-native smart click with verb overrides.** Select a force by card, badge, 1–9 or box. Right-click is the smart order: region → Move & Hold, hostile structure → Attack. Verb keys arranged in a spatial layout (e.g. `A` Attack, `R` Retreat) override it for one order, then the mode ends. Shift queues up to 3.
- Pros: fastest; matches SC2 and AoE habits.
- Cons: Attack-region needs a key; new players may never discover it.
- Mouse + keyboard: excellent.
- Controller: X = smart order on the snapped target; Y = Retreat; LB-hold radial for Attack-region and other verbs. Fine.

**Option B — Verb first, then target (current plan).** Choose a verb from the card or a hotkey, then click a region or structure. Drag a card or badge as a shortcut.
- Pros: always explicit; easy to teach; maps 1:1 to a radial.
- Cons: two inputs per order; RTS players will try to right-click and get nothing; drag is mouse-only.
- Mouse + keyboard: OK, best with Option A's right-click added on top.
- Controller: radial → stick-snap to target → A. Good, but slower.

**Option C — Target first, through a site menu.** Click or hover a region or hostile structure, then a contextual menu lists "Hold here with…", "Attack with…", "Retreat to here" plus your forces with arrival times. Like AoE's site-based commands (4.2).
- Pros: fits the region/war-table pillar; good for co-op ("who can reach here?"); the most natural on controller.
- Cons: issuing orders to several forces needs multi-select in the menu; slower for one quick order.
- Mouse + keyboard: good as right-click-hold or middle-click.
- Controller: excellent (RT on the target).

**Option D (recommended hybrid, A + C).** Smart right-click and X for the common case. Verb keys and a fixed radial for Attack-region and Retreat. A site menu on the target for target-first play and the war table. Drag only as a mouse shortcut. Production state and reinforce on the card; tiers and perks in the inspector.
- Mouse + keyboard: one click for common orders, explicit keys for edge cases.
- Controller: every verb reachable with one button plus a stick direction.
- Cost: three ways to issue an order, which must be taught in stages (Thronefall-style layering, [Game Developer](https://www.gamedeveloper.com/design/mastering-minimalism-and-layering-complexity-with-strategy-game-thronefall)). **[INFERENCE]**

## 11. Open questions

1. When a force is told to go to a hostile region, is that Move & Hold or Attack, and how does the player see which one before clicking?
2. Does Attack on a region mean "clear it, then hold" or "clear it, then return"? This decides whether Attack-region needs its own verb at all.
3. Do reinforcements walk to their force (and can they be intercepted), or appear at it?
4. Retreat to where: the nearest friendly region, the force's home building, or a target the player picks?
5. Can a force card be selected and given orders from the war table without moving the camera on the ground?
6. Is there pause or slow-motion in solo play, and what happens in co-op?
7. In co-op, can a player select another player's force (for gifting, or to view its orders)? What does a click on an ally's force do?
8. What do controller-only players do for box select: Halo Wars' paint brush, or select-local plus cycling?
