# Controls, UI and time [New] — decided

> Part of the [Post-Frontier design](../Design.md). Related: [forces](forces.md).

## Time

| Mode | Pause | Speed |
|---|---|---|
| Solo | [Built] **Active pause:** **P** or the on-screen **[P] Pause** button. Select, inspect and give orders while paused. Command intent and no-time changes apply immediately; movement, production, income and combat continue on resume. Esc still opens the menu; closing it preserves active pause. | [Built] Fixed 1×. [New] **Opt-in setting:** drop to half speed when a win or loss is imminent (an HQ offline, a hold three-quarters done). Never in co-op. |
| Co-op | [Built] **P** or the on-screen pause button: any player can pause; the team shares **one pause of up to 60 real-time seconds per battle**. Enough for a doorbell without draining the tension of the countdown. Any player can resume early; it resumes automatically at the deadline. Everyone sees the countdown. The spent action is grey and explains that the team's pause is used. | [Built] Fixed 1× |

**Owner decisions (2026-10-02):** P is the active-pause key. Co-op uses the single shared budget and early/automatic resume rules above. Pause and resume use the authoritative command layer and replicate to every player.

## Camera

- **[Built]** A free camera: arrow keys, screen-edge movement or middle-mouse drag pan; the wheel zooms (owner decision, 2026-10-03). WASD does not pan; A is reserved for Attack.
- **[Built]** Screen-edge pan starts within **8 viewport pixels** of an edge while the game viewport has focus and its window is foreground. It is always active at the edge, **including over HUD panels**; outside-viewport cursors and modal screens do not edge-pan. Edge and arrow pan share the existing zoom-scaled speed: **1.1 × current camera arm length per second**, initially **2,640 cm/s** at the default 2,400 cm arm. Combined/diagonal input is clamped to that speed.
- **Zoom out to a war-table view** (Supreme Commander style): regions become readable icons showing forces, JEV intent, orders, traits and supply links. Zoom in for spectacle.

## Selecting and giving orders [Change] — decided

Research: [input.md](../Research/input.md).

**What's wrong today.** A source audit on 2026-10-02 ([input-audit.md](../Research/input-audit.md)) found:
- Right-click only cancels a mode, and every order goes through the barracks: select it, pick a goal, then pick a region.
- Clicking a unit selects its barracks. Survivors of a destroyed barracks can't be selected.
- Feedback gaps: greyed-out buttons swallow clicks silently, messages never expire, a rejected region pick drops you out of the mode, and the minimap says "CLICK TO PAN" while a click assigns a region.
- Controller-hostile: tiny stacked goal buttons, region preview on hover only, and panning by held middle button.

**Input devices:** mouse and keyboard at launch. Every interaction must stay possible on a controller later: no drag-only actions, no hover-only information, large targets the cursor can snap to, and an on-screen prompt for every key.

**Selecting:**
- **[Built]** Click a force's numbered, owner-coloured map badge or any living unit to select its force. Shift-click adds or removes an owned force; box-select includes badge centres, with Shift adding to the existing selection. Number keys select by force number ([forces.md](forces.md)). **[Built]** Clicking an owned force-bar card selects that force without moving the camera.
- **[Built] Selecting never moves the camera.** Double-tap a force's number within 0.3 s, or press F, to centre on the selection; several forces focus their midpoint. Box dragging begins beyond 6 screen pixels. Space jumps to the latest alert (see "Awareness" below).
- **[Built]** Shift-clicking an owned force card adds it without toggling an existing selection; double-clicking its body focuses that force.
- **[Built] Click a production building** to open its panel and highlight its force's badge and force-bar card without selecting the force. The panel has a *Select force* button; double-clicking the building selects its force.
- **[Built] Click a teammate's force, unit or producer** to inspect its force read-only, never adding it to command selection. The same full force card shows its owner, order, joined and reinforcing members, production, ETA and rule summaries, with **Need help here [G]** and **Gift…** (opens the Team panel with its owner chosen) as its only controls. You can't command it or change its production.

**Giving orders [Built] unless noted:**
- **[Built] Right-click is the smart order** for all selected owned forces:
  - a region → **Move & Hold**, always, including regions JEV holds;
  - a hostile structure → **Attack**.
  - Ground and minimap use the same target resolver. **[New]** War-table ordering.
- **[Built] Order keys** cover the rest, for one accepted order each, then the mode ends: **A** then left-click a region = Attack that region; **R** = immediate Retreat. Esc or right-click cancels pending A without issuing a smart order. The same actions sit on the force card with their key prompts.
- **[Built] Cursor preview:** beside the cursor, show Move & Hold, Attack, rally, or not allowed with the reason. Hover and confirmation share one resolver. Pending A shows the left-click Attack preview and the right-click cancellation prompt. **[Built]** Selected-force hover previews allowed Move & Hold or Attack routes before confirmation, including pending Attack and queued routes from the preceding endpoint ([forces.md](forces.md)); rejected orders and building rally input show no force-route preview.
- **[Built] Shift queues** instead of replacing the current order; the limit is specified in [forces.md](forces.md#steering-forces-change--decided).
- **[Built] Rally input** (owner decision, 2026-10-03): with a production building selected and no forces, right-click a region on ground or minimap to set that building's rally. Rally behaviour lives in [forces.md](forces.md#steering-forces-change--decided).
- **[Built] No drag-to-order.** Dragging selects forces only.
- **[Built] Feedback rules:** a rejected order keeps the mode open and says why, including a server rejection; the mode ends on acceptance, not on sending. Rejected placements also retain their mode. A greyed-out gameplay button explains itself when clicked. Messages hold for **3 s**, then fade over **1 s** (starting values).

**Force bar [Built] — display and controls:** a bottom row of force cards, one per owned force, ordered by force number. Each shows the force number, unit type, strength (e.g. 5/6), current order and queue, status with ETA (*Marching to West Cut · 0:20*, *Withdrawing · 3/6 → resumes at 5/6*, *Responding · Drill Rig under attack*), retreat or refill state, and **production state**: refill progress and a pause/resume toggle. Travelling recruits are counted separately from joined strength; a force without a producer is labelled as an orphan without reinforcements. The Attack [A] and Retreat [R] buttons run the same selected-force order input as the keys. A card button acts on its own force, so it first narrows the selection to that force (Shift on Retreat queues it, as with the key). Threshold and targeting presentation follow [forces.md](forces.md).

**Force-bar layout [Built]:** the row is 188 virtual pixels high at the bottom edge, with 10-pixel margins and 8-pixel gaps. Cards are at most 300 pixels wide and the bar is only as wide as its cards (five cards at 1280×720 are about 245 pixels each), so only the cards are a click panel. The deck (720 wide) sits beside the cards in the same row when it fits; the minimap and build bar (at most 700 wide) stand above the row with 4-pixel clearance. Where the deck does not fit beside the cards (two or more at 1280×720, three or more at 1600×900), it starts collapsed to the mode bar above the row; **F4** opens it over the world, and selecting a building opens it too, because the building's panel lives in the deck. At 1600×900 and 1280×720 the screen centre is never HUD in the default state, and three JEV memo rows stay visible. A teammate's read-only card (360 wide) takes the deck's place above the row. One, two and five cards (with an orphan), the pinned deck, and the paused-refill, full-strength, marching, withdrawal, manual Retreat and teammate surfaces have been inspected at both resolutions.

**Force ETA [Built]:** length of the active replicated intent route, divided by the force's effective march speed ([forces.md](forces.md)). The card and world/minimap path use the same polyline builder, including accepted formation destinations and structure endpoint corrections; no shortest-path route is recomputed on the client. Queued and retained recovery legs are excluded until active. The travel estimate excludes combat, capture waits and local navigation/crowd detours; no client navigation mesh is required. It refreshes every 0.5 seconds or when an order or waypoint changes. JEV's authoritative plan ETA is not replaced by this client-side estimate.

**Force-card future badges [New]:** supply cutoff and upgrade availability arrive with their mechanics in [forces.md](forces.md); neither is shown early. Their placement and the refit badge are decided in "Step 1b surfaces" below (orchestrator 2026-10-04).

**Order-state presentation [Built]:** the replicated force status is Marching, Holding, Withdrawing, Retreating or Refilling. **Attack + Refilling + `ResumeCount`** identifies automatic withdrawal recovery, not a manual Retreat: the card keeps the withdrawal's joined strength and resume count while the force refills. Retreat + Refilling means the manual sprint has ended and weapons are enabled while refilling. Completion, orphan exceptions and rally defaults have a single specification in [forces.md](forces.md).

**Building panel:** the rare decisions stay at the building: tier upgrades, perk slots and the unit lock ([forces.md](forces.md)).

**Building:**
- **[Built]** An always-visible **build bar** grouped by category ([buildings.md](buildings.md)), replacing the deck's build cards. Every button shows cost, availability and a grid hotkey: **B**, then a letter. Letters follow bar order: **QWERT**, then **ASDFG**, then **ZXCVB**. Today's entries: **B Q** Barracks (Production), **B W** Extractor (Economy), **B E** Workshop (Tech). Pending **B** shows on the bar and ends on any non-grid key or after **2 s**.
- **[Built]** **Shift+LMB places another**; LMB without Shift ends placement after success. The new building is selected after placing. Esc or right-click cancels placement and any selection drag; another placement waits for an outstanding server result. A late result still reports whether the building was placed, including over the pause menu, without reopening placement or changing selection. Starting another build or order mode, or selecting a force by unit, badge, number or box, discards any deferred selection from the previous placement.

**On the map [Built]:** the selected forces show their path line, a target highlight and their badge. Shared commander intent and queued-route presentation are specified in [forces.md](forces.md).

**Controller [Later]:**
- X gives the smart order on the snapped target.
- One radial with fixed slots for Attack region, Retreat and the build menu.
- Shoulder buttons select nearby forces or all forces; the D-pad cycles through forces and alerts.
- The cursor snaps to regions, structures and badges.
- **Target-first menu:** select a region or structure and get *Hold here with…* / *Attack with…*, listing your forces with arrival times. It comes with controller support and also works on the war table.

**Today [Built]:** forces are selected and ordered independently of buildings, including survivors without a producer. The production inspector contains production, rally information and a Select force shortcut, not order buttons or building-force region picking. Ground/minimap smart right-click, A/R keys, queue modifiers and cursor feedback use selected forces; force-bar cards expose the same actions.

## Awareness [Built] / [New] — decided

Research: [pacing.md](../Research/pacing.md). In the 2026-10-01 playtest the only signs of a winning push were a small enemy-HQ bar and JEV's alarm, which plays at its HQ out of earshot.

- [Built] **Team announcer:** a global voiced line plus a feed entry for every player, including the team's own successes. Today's objective events are either HQ under attack, at half health, at quarter health or offline; a region captured or lost; and one of the team's Drill Rigs lost. Entries identify the causing player's force and region; captures include every participating force.
  - Each affected structure has its own attack episode, ending after 20 s without damage to it. Within an episode, "under attack" speaks only for a new attacking force, not for a damage-tier change. Every valid hit, including a suppressed repeat or a threshold announcement, extends the episode and records the force. Ending the episode clears the announced-force set.
  - Each HQ hit produces at most one line: offline takes priority over a quarter-health crossing, then a half-health crossing, then "under attack". Half/critical lines are the damage-tier announcements; crossing multiple thresholds in one hit emits only the most urgent line. State transitions bypass episode suppression. Cancellation, cleanup and destroying JEV's Drill Rigs are not team combat losses.
  - Voice and feed definitions use the same wording and event IDs; [the voice pipeline](../Audio.md) supplies each line's asset. Speech is serialized with at most 16 pending lines: overflow discards the oldest pending line, and pending lines reaching the episode's quiet-window age are discarded rather than spoken late. A line already playing is not interrupted.
- [Built] **Objective strip:** always on above the battlefield, showing both HQs' health, regions held by each side, and commander-coloured role/force badges for the latest objective event. Economy remains visible above it.
- [Built] **Alert feed:** objective rows take priority over ping rows, newest first within each history, so ping traffic cannot hide a live objective alert. Objective entries last 8 s and fade over their final 2 s. Clicking a visible entry focuses its event location without changing selection. The feed occupies the right-hand column above the build bar, without covering the footer or force cards.
- [Built] **Space jumps to the latest objective alert;** pressing it again steps back through retained objective history, clamped at the oldest entry. Pings are excluded from Space history and use their feed's click-to-focus shortcut instead. A new objective event resets the next jump to the newest. The latest 64 objective events remain navigable after their feed entries fade. Jumping is optional: the strip and announcer carry the state.
- [Built] **Team pings:** **G** pings at the cursor on the ground or minimap. A teammate's force, or its read-only inspector's ping button, sends **Need help here** at the authoritative force centre; any other spot sends **Look here**. Every connected teammate sees commander-coloured markers on the map and minimap and a feed entry naming the sender for **6 s** (starting value), plus a short UI cue. Need-help rows also name the target force's owner. Matching announcer speech plays only when speech is idle with no pending lines; busy pings drop their voice line rather than delaying or evicting objective speech, without suppressing their cue or feed entry. Receiving a ping never moves the camera; clicking its feed entry focuses the spot. The command layer delivers only to the sender's human team, never JEV. Each player can send one ping every **2 real-time seconds** (starting value); a throttled request explains the limit. Marker/feed lifetime uses synchronized battle time.
- [Built] **Failover Node events, nodes left and the hold timer:** see surface 9 in "Step 1b surfaces" below. [New] Exposure events arrive with their objectives in [battle.md](battle.md). Their strip, bar and wording are decided in "Step 1b surfaces" below (orchestrator 2026-10-04).

## JEV intent display [Built] / [New]

Everything here reads the replicated JEV plans ([jev.md](jev.md#published-intent-built)). Nothing is computed on the client that could disagree with a plan.

- **[Built] Timeline bar:** under the objective strip, left of the alert feed, so it never covers the strip, the alerts, the build bar or the deck. Up to 4 cells, soonest arrival first. Each cell shows the plan's verb (*ESCALATED* for a defense), target region, size band (`~8 units`) and a countdown. The countdown runs from the server time the planner computed the ETA, which is published with the plan; it stops at 0:00 and then reads *ARRIVED*. More plans than cells show *+N more*. **[Built]** The bar is always drawn and keeps its 40 px with no plan, and a version release joins it as its first cell (surface 8 below). **[New]** JEV calldowns join it when they exist ([battle.md](battle.md)); an entry is a kind plus a countdown, so they slot in beside plans.
- **[Built] Region badges:** every region a plan targets carries a badge above its region label on the world map and a marker with its countdown on the minimap (`ESC` while defending). The badge names the target region and the verb (`JEV  Attack  Fusion Works  0:20`); an escalated plan reads *Escalated: defending X* in place of a countdown. Two plans on one region show the sooner one and `x2`. The world region label shows the region's display name from the map data (the name the timeline and memos print) instead of `REGION n`.
- **[Built] Memo feed:** below the timeline bar, separate from the team announcer's alerts, in the Machine's voice: the Machine colours of [STYLE.md](../../Art/UI/STYLE.md) (pearl text, cyan trim, red lens) and a `JEV` tag, never the team panels' blue-grey. A plan posts its memo (the [jev.md](jev.md) template text, verbatim) when it is first published or when what the memo prints changes: its ticket, verb, target region, size band or escalation. ETA drift or a changed target structure posts nothing. Newest first, 3 rows, each held for 12 s and faded over 2 s (starting values); the feed remembers the latest 8.

## Step 1b surfaces [New] — decided (orchestrator 2026-10-04)

Step 1b adds ten player-facing surfaces. Every entry below is **(orchestrator 2026-10-04)**; mechanics, costs and durations live in the topic files they link, never here, so quoted numbers in strings are examples and real values come from data. Layout numbers are virtual pixels at HUD scale 1.0 (1600×900 and 1280×720 are both 1.0). They follow the binding rules above: no drag-only actions, no hover-only information, an on-screen prompt for every key, large snap targets (28 px minimum for any clickable target; a chip's hit area may be larger than its drawing), and state is never carried by colour alone (each state has a glyph or a word).

**Keys [New].** Checked against every existing binding (arrows, wheel, LMB, MMB, RMB, 1–5, Space, F, Esc, F4, P, G, A, R, Shift, **B** plus the grid QWERT/ASDFG/ZXCVB, and Enter, which restarts only on the Result screen and starts Play Solo on the Main Menu; neither screen overlaps planning). Esc cancels the armed mode first, then closes the Team panel **[Built]**, then opens the menu. A key that is not a grid letter ends a pending **B** and still acts, as F does today.

| Key | Action | Where it also sits on screen |
|---|---|---|
| **H** | Arm Fortify targeting | The Fortify dock button |
| **Tab** | [Built] Toggle the Team panel (roster, gifting, gift log) | The **TEAM [Tab]** button |
| **Enter** | Ready / un-ready | The **READY** button, planning only |

### 1. Data, rates and supply-cut state [Built]

- **[Built] Top bar text:** `C1  604 Power +2/s   40 Data +1/s   Forces 2   Regions 4/5`. Power stays gold. Data is white with a chip glyph and the word, because [STYLE.md](../../Art/UI/STYLE.md) has no Data colour (art may choose one). The text clips at a fixed 400 px so the chip after it has one slot at every window size.
- **[Built] Rates:** each figure shows **your own** share of the team pool ([economy.md](economy.md)), with one decimal only when it is fractional. Teammates' rates and the pool total are in the Team panel (surface 3).
- **Supply-cut state [Built]:** a red chip after Regions with a chain-break glyph: `LINE CUT ×2  −3 Power/s  −1 Data/s`. It lists only the rates that are lost, and disappears when the chain is whole. The Power figure is your share of the offline Drill Rigs' extraction (the pool splits evenly among the roster) and the Data figure is one second's worth per cut reward region; a fallen main is the end of the battle, not a cut. Its hit area is the full 32 px bar height. Clicking it focuses the first cut region; repeated clicks cycle (the controller reads `PressureFocusTarget`, which remembers the last region).
- **Why:** the top bar leaves about 540 px free at every window size, so nothing else moves, and the chip is a glyph plus words.

### 2. Supply cuts on the map and the force card [New]

- **World [Built]:** at the replicated change time the boundary cable snaps (two stubs and a spark) and each cut-off region's border flashes red three times within 1 s ([economy.md](economy.md)). A change seen under 3 s late still plays; an older one shows only the steady state: a dashed red border with a light hatch, grey dashed cables beyond the cut. The client compares each replicated mask with the last one it observed, so only the regions just cut flash (on a clock that starts when the client sees them) and only cables that were live snap; a client that joins more than 3 s after the cut sees the steady state and guesses the stub from the opponent's neighbouring region. A mask that empties (the main fell) is the end of the battle, not a cut.
- **Region and Drill Rigs:** [Built] the region's chip row (surface 7's stack) gets a **CUT OFF** chip, each offline Drill Rig carries a chain glyph, and its deposit label reads `POWER 1200 · OFFLINE` in place of the rate. **[New]** The rig mesh itself goes greyscale with the building appearance work (tier-2 slice).
- **Minimap [Built]:** the node gets a red hatched outline.
- **Feed [Built]:** `Supply cut: Fusion Works cut off · 1 Drill Rig offline` (one row per region just cut, red stripe, Drill Rig count read live); clicking it focuses the region. It is a team row (see surface 3).
- **Force card:** a **CUT OFF** chip in the header between unit type and strength, and the refill line reads `Refill: HELD · cut off · 1 recruit waiting` ([forces.md](forces.md)). A card shows at most two chips (CUT OFF, REFIT n/m, `▲ T2`), then `+N`.

### 3. Gifting and the Team panel [Built]

- **Opening:** the **TEAM [Tab]** button (96×32) sits left of the Pause button; Tab toggles. A gold dot marks a gift sent to you that you have not seen; it clears while the panel is open. The panel is 390 px wide and **replaces the alert feed column while open**; the strip, timeline and announcer keep working.
- **Rows:** teammates only (a team has at most four commanders, so up to three rows; 28 px high, the whole row is the target): colour swatch, `C2`, Power with rate, Data with rate, and a `FORTIFY ready` / `FORTIFY 0:47` chip, which is each commander's cooldown display.
- **Gift flow:** click a teammate; click the `POWER` or `DATA` toggle; click a preset (Power 50 / 100 / 200 / ALL, Data 10 / 25 / 50 / ALL) or `−` / `+` (Power ±10, Data ±5); click the full-width `SEND 100 Power to C2` button. A teammate's read-only force card gets a `Gift…` button that opens the panel with them selected.
- **Rejections** show inline under Send with the existing 3 s hold and 1 s fade, and a disabled Send still explains itself on click: `Not enough Power: you have 340`, `Pick an amount above 0`, `C2 left the team`, plus `Pick a teammate first` before anyone is chosen and `Gifting is closed: the battle is over` once the battle ends. `Gifting opens at 0:00` while the planning phase runs ([battle.md](battle.md)).
- **Log:** the team gift log ([economy.md](economy.md)) in the panel, each row timed on the battle clock, three rows with 28 px ▲ / ▼ buttons stacked beside them, so scrolling never needs the wheel. An accepted gift also posts the feed row `Commander 2 gifted 100 Power to Commander 1` (gold stripe, 8 s, click opens the panel), and the recipient's top-bar figure flashes (Power to white, Data to gold) with `+100 from C2` for 3 s (the Forces and Regions figures give way meanwhile).
- **Team rows in the feed:** gifts, Fortify casts and supply cuts rank below objective rows and above ping rows, and stay out of Space history.
- **Controller [Later]:** focus order rows → toggle → presets → stepper → Send; the D-pad moves, A activates.
- **Why:** no drag, hover or typing; three teammates fit within the 296 px column at 1280×720.

### 4. Fortify [Built]

- **Button:** the **Fortify [H]** dock, 144×42 at the left edge, 66 px above the minimap's top, directly above its legend. Two lines: the name, then the cost, `Ready in 0:47` with a drain bar, or `Need 12 more Data`. JEV memo rows clamp above it on short windows. Later commander ability and ultimate slots grow to its right.
- **Input:** H or a click arms a targeting mode like **A**: the mode bar reads `FORTIFY` with `LMB Cast` and `RMB / Esc Cancel`, the dock reads `Pick a region · Esc`, and the minimap legend reads `LMB FORTIFY / RMB CANCEL`. LMB on a ground region or the minimap casts. The mode ends on acceptance and stays open on rejection; H again, Esc or RMB cancels.
- **Cursor preview** (the order cursor's resolver): green `LMB: Fortify <Region> · <cost>` with the effects line from [commanders.md](commanders.md); amber `LMB: Refresh Fortify at <Region> · 0:41 left` (allowed, it only warns); red `Not allowed: <reason>` with `<Region> is held by JEV`, `<Region> is neutral`, `Need 12 more Data` or `Cooldown 0:47`; **[Later]** `Opens at 0:00` arrives with the planning phase, which is not built. While armed, valid regions get a dashed green border and the rest dim, on the minimap too. The world dims the other regions' outlines rather than filling them.
- **Active badge:** a chip in the region's chip row, a shield glyph with `FORTIFIED C2 0:42`, a caster-colour stripe and a 3 px drain bar that pulses at 1 Hz for its last 10 s. The minimap node gets a cyan dashed ring that drains clockwise. If the team loses the region the badge drops and the feed posts `Fortify at X ended: region lost`.
- **Teammates' casts:** one expanding ring at the region, a badge with their stripe and `C2`, and the feed row `Commander 2 fortified X` (cyan stripe, click focuses; the camera never moves). The caster hears the same voiced line, **Fortify active.**, and sees no row for their own cast.
- **Why:** A-mode is already learned, H is free, and the dock is the only pocket that survives 1280×720 with five force cards and the deck open.

### 5. Tier-2 branch in the production panel [New]

- **Placement:** once a Barracks' unit type is locked, its FORCE TYPE column shows the locked row and a 30 px `TIER 2 BRANCH` button below it, e.g. `MARKSMAN +20% range · 100 Power + 50 Data · 20 s`, with the text taken from the unit data ([forces.md](forces.md), [units.md](units.md)). No hotkey: it is a rare, costly decision.
- **States:** `Lock a type first`; unaffordable and greyed, `Need 50 more Data` (a click explains); `Opens at 0:00` during planning; buying, a bar `Upgrading to Marksman 12 / 20 s` with an amber `UPGRADING` header pill; done, a static `✓ MARKSMAN +20% range`.
- **Paused production:** the FORCE column reads `Production paused while upgrading` and its pause / resume toggle is disabled with that reason.
- **Force card:** an amber `REFIT 2/5` chip and the refill line `Refit 2/5 → Marksman · cut-off members keep old form`. A `▲ T2` chip shows while the branch is affordable. It is information only; the existing click on the producer opens its panel.
- **Unit type picker:** an unlocked Barracks lists five types in a two-column grid of 28 px chips, three rows, because `Layout::Row` would shrink a five-row list to about 14 px.
- **Why:** the 720 px deck keeps three columns; progress shows both where the player looks (the card) and where they decided (the panel).

### 6. Shields, Scrambler pulse and building stun [Built]

- **Unit bar (36 px):** a 3 px shield bar above the 5 px HP bar, pale cyan for both teams. The existing show rule applies (damaged or in a highlighted force); a unit with no shield draws none.
- **Pulse on units:** the shield bar flashes white for 0.25 s, then shows an empty outline for the regeneration delay; regeneration refills it ([units.md](units.md)).
- **Pulse cue [Built]:** a ring expanding to the pulse radius over 0.4 s at the Scrambler, and a spark on each hit shield bar or building; no numbers. There is no pulse cooldown display (orchestrator 2026-10-04).
- **Stun [Built]:** a yellow `STUN 2.4s` chip (bolt glyph, drain bar) beside the building's name plate on the world overlay (the force badge sits above the plate), refreshed by a new stun. The wire carries only the stun's end time, so the drain bar runs from the length this client first saw; enemy buildings show the chip too. A stunned building's construction bar freezes to a desaturated grey. Your own buildings post `Barracks 1 stunned by a Scrambler` (at most one per 5 s per building) as a local feed row.
- **Stun panel [Built]:** the inspector's status slot reads a yellow `STUNNED` pill (it wins over `UPGRADING`, since a stun freezes the upgrade timer), and the construction and production bars freeze and desaturate to grey with `frozen: stunned` / `Production frozen: stunned` under them. Research has no bar, so a research building shows the pill alone; an Extractor shows neither, since extraction is unaffected.
- **Why:** the HP bar keeps its position; the ring and the chip both appear at t = 0, so it reads within 1 s.

### 7. Region trait icons [New]

- **World [Built]:** a 20 px glyph left of the name on the label plate, and the trait word in 9.5 px capitals under it, so nothing is hover-only. High ground is a chevron, Cover a brick, Open a double arrow, Hazard a warning triangle in amber; the silhouettes differ, so colour is secondary ([map.md](map.md)). 20 px meets the glyph rule in [STYLE.md](../../Art/UI/STYLE.md); 9.5 px stays at or above its 7.1 px caption floor down to the 0.78 minimum HUD scale.
- **Stack above a region anchor [Built],** top to bottom: the JEV badge, the label plate with the glyph, the chip row. Draw order: defend-post decals, then deposit labels, then the region stack, then the JEV badge.
- **Minimap [Built]:** a 10 px glyph at the node's top-left; deposit ticks stay bottom-left, the JEV marker and countdown stay on the right, and the Fortify ring wraps the node. 10 px is under STYLE.md's 20 px glyph rule because the minimap is 144 px wide; the world plate carries the full glyph and word, and if art cannot make the four silhouettes readable at 10 px the minimap falls back to one-letter pips (`H`, `C`, `O`, `!`).
- **War-table zoom [New]:** the plate collapses to a 24 px glyph plus the name.
- **Why:** the other node corners are taken, and a glyph inside the plate cannot collide with the badge, the deposit label or the post marker.

### 8. JEV timeline and the battle clock [Built]

- **Always drawn [Built]:** the timeline bar keeps its 40 px even with no plans, so the memo feed never jumps. Empty text: `No JEV plans · next release v1.1 in 1:42`.
- **Release cell [Built]:** appears for the lead time in [jev.md](jev.md), pinned leftmost with cyan trim: `v1.1 RELEASE 0:24` and a tag from the release's added behaviour: `WAVE · RAIDS DRILL RIGS`, `COUNTERS <ARMOR>`, `ALL FORCES ATTACK`, `WAVES +<n>% SPEED`, `OVERRUN`. `<ARMOR>` is the humans' most numerous armor class, the one JEV publishes with its release state at each evaluation and buys the wave against; it reads plain `ARMOR` with no human unit alive. The v1.2 feed row keeps the class the cell showed. A release also posts one local feed row, `JEV v1.1 released: <tag>`. There is no voiced release line: the announcer's Raise runs on the server, in JEV's wave code ([status.md](status.md)).
- **Waves [Built]:** waves, emergency waves and the Split-Brain pair are ordinary plan cells after it, soonest first; `+N more` counts plans only. Clicking a cell focuses its target (a release focuses JEV's main).
- **Battle clock [Built]:** the top bar's right end, left of the key hint, `JEV v1.1  4:12`, minutes and seconds counting up, frozen while paused. It and every release countdown read the game state's battle clock ([battle.md](battle.md)), so both read 0:00 through planning.
- **Why:** [battle.md](battle.md) says the HUD counts down to the next release and [jev.md](jev.md) gives each release cell a lead time before it happens; the empty text and the cell are both true.

### 9. Guarded HQs and the objective strip [Built] (emergency badge [New])

- **Nodes row:** under each HQ bar, `Nodes ◆◆` (filled standing, hollow lost, a double outline while the plating lasts, [buildings.md](buildings.md)). While a node stands the HQ bar is hatched with a shield glyph, its world label reads `LATTICE HQ 900/900 · immune (2 nodes)`, and the Attack cursor chip warns `Attack Lattice HQ · immune while Nodes stand` (allowed). A node loss posts `Hardline Failover Node lost · 1 left`.
- **Offline hold:** the HQ bar becomes the hold bar, same 225×20: `HARDLINE OFFLINE … 0:38 / 1:15` (progress over the full hold, from data), a plain fill with no tick marks. The state line under it: `▲ JEV HOLDING THE MAIN` (red), `‖ PAUSED: defenders in the main` (amber), `▼ DECAYING: main is clear` (green). For the Lattice: `THE LATTICE OFFLINE`, `▲ UPLINK HELD`, `‖ PAUSED: JEV in the main`, `▼ DECAYING: send a force in`. Back online posts `Hardline HQ back online at <restore %> HP`.
- **Minimap [Built]** (owner-confirmed surface-9 decision, 2026-10-04): an offline HQ is its team's 12 px outline over a dim fill with an amber X; a lost HQ is the grey solid square; a standing HQ keeps the solid team square. While any HQ is offline the legend's `sector` entry reads `X: HQ offline` (`HQ/base | X: HQ offline | amber: contest/front`).
- **Emergency wave:** the latest-event title reads `Hardline HQ offline. Emergency forces deployed.` and its forces carry an `EMERGENCY` badge.
- **Standing sentence** replaces `No objective alerts yet`: `Objective: break the Lattice's Failover Nodes, take its HQ offline, then hold the uplink <hold duration>`; once the nodes are gone, `Destroy the Lattice HQ to take it offline.` ([battle.md](battle.md)).
- **Why:** it reuses the strip's two bars, so the hold is a state of the bar, not a new panel.

### 10. Planning phase [New]

- **Clock slot:** `PLANNING 0:47`, an amber real-time countdown that pulses in its last 10 s. The battle clock stays hidden until 0:00, so there is only one clock. The timeline shows JEV's first plans with muted countdowns and `frozen: starts 0:00`.
- **Ready:** the Pause button slot becomes **READY (1/2)** with an **Enter** prompt; the counter counts humans only, because planning ends when every human is Ready ([battle.md](battle.md)). P explains `Nothing runs during planning`. The strip shows roster chips: `C1 PLACING`, `C2 ✓ READY`, and an AI adjutant as `C3 AI · READY` (shown, not counted). Enter toggles Ready and un-Ready; Ready locks kit and unit-type edits until un-Ready; with an unplaced kit one line asks `Barracks not placed: default spot. Press Enter again`.
- **Kit placement:** the build bar becomes a KIT bar with Barracks (`B Q`) and Drill Rig (`B W`): `FREE KIT · place`, then `PLACED · click to move`; the Workshop is greyed `after 0:00`. Placement is the existing placement mode. An unplaced kit shows a dashed default-spot ghost, `auto-placed at 0:00 if unplaced`.
- **Planning panel:** it takes the deck slot, 720×186, pinned open: place Barracks, place Drill Rig, unit type (five chips, 104×30, one row), first order (the existing RMB, A and Shift input; the panel lists the queue), and a `LOOK AT JEV BASE` button. Kit and unit type stay editable until Ready or 0:00.
- **Why:** every control reuses a slot that is free before the first force exists; only Enter is new.

## Feedback and presentation — decided

| What | How it's shown |
|---|---|
| Counters | Hit flashes coloured by effectiveness: bonus hits flash bright, normal hits plain. Every force badge, including JEV's, shows its armor and damage icons. No floating numbers. |
| Supply chain | **Always-visible cables** across region borders between connected regions, in team colours (the humans' teal, and JEV's in the Machine skin's pearl, [STYLE.md](../../Art/UI/STYLE.md)). A cut snaps the cable, flashes the region and greys its Drill Rigs. [New] The cut's map, minimap, top-bar and force-card presentation is decided in "Step 1b surfaces" above. |
| Alerts | [Built] Objective feed, global announcer and explicit camera jumps described above. [New] Voiced supply cuts, JEV releases and calldowns follow their mechanics in [economy.md](economy.md) and [jev.md](jev.md). |
| Incident report (after each battle) | A timeline graph of income, forces and regions with key events; **3 "why" callouts**, e.g. *Line cut at 4:12 cost 600 Power*; plus a Machine memo for the joke |
| Contribution card | **1–2 positive highlights per player**, e.g. *Saved West Cut at 6:40* or *Gifted 800 Power*, plus personal stats. It never ranks players. |
| Music | **Adaptive layers tied to JEV releases:** calm, then build-up, then a peak around each release and wave, with today's ambience underneath |
