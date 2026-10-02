# Controls, UI and time [New] — decided

> Part of the [Post-Frontier design](../Design.md). Related: [forces](forces.md).

## Time

| Mode | Pause | Speed |
|---|---|---|
| Solo | [Built] **Active pause:** **P** or the on-screen **[P] Pause** button. Select, inspect and give orders while paused. Command intent and no-time changes apply immediately; movement, production, income and combat continue on resume. Esc still opens the menu; closing it preserves active pause. | [Built] Fixed 1×. [New] **Opt-in setting:** drop to half speed when a win or loss is imminent (an HQ offline, a hold three-quarters done). Never in co-op. |
| Co-op | [Built] **P** or the on-screen pause button: any player can pause; the team shares **one pause of up to 60 real-time seconds per battle**. Enough for a doorbell without draining the tension of the countdown. Any player can resume early; it resumes automatically at the deadline. Everyone sees the countdown. The spent action is grey and explains that the team's pause is used. | [Built] Fixed 1× |

**Owner decisions (2026-10-02):** P is the active-pause key. Co-op uses the single shared budget and early/automatic resume rules above. Pause and resume use the authoritative command layer and replicate to every player.

## Camera

- A free camera, as today.
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
- Click a force badge, its card, or any of its units to select the force. Box-select, or Shift-click to add. Keys **1–4** (solo **1–5**) select a force by its number ([forces.md](forces.md)).
- **Selecting never moves the camera.** Double-tap a force's number, or press F, to centre on the selection. Space jumps to the latest alert (see "Awareness" below).
- **Click a production building** to open its panel; its force's card lights up. The panel has a *Select force* button, and double-clicking the building selects its force.
- **Click a teammate's force** to see its card read-only (order, strength, ETA), with a ping shortcut (*need help here*). You can't command it.

**Giving orders:**
- **Right-click is the smart order:**
  - a region → **Move & Hold**, always, including regions JEV holds;
  - a hostile structure → **Attack**.
  - It works on the ground, the minimap and the war table.
- **Order keys** cover the rest, for one order each, then the mode ends: **A** then a region = Attack that region; **R** = Retreat. The same buttons sit on the force card. Esc or right-click cancels a pending order key.
- **Previews:** the cursor shows which order a right-click will give, and the route preview appears before you confirm. Shift queues up to 3 orders.
- **No drag-to-order.** It duplicates right-click and doesn't work on a controller.
- **Feedback rules:** a rejected order keeps the mode open and says why. A greyed-out button explains itself when clicked. Messages fade after a few seconds.

**Force bar:** a bottom row of force cards. Each shows the force number, unit type, strength (e.g. 5/6), current order, status with ETA (*Marching to West Cut · 0:20*), retreat or refill state, and whether it's cut off from supply. It also shows **production state**: refill progress, a pause/resume toggle, and an *upgrade available* badge that opens the building's panel.

**Building panel:** the rare decisions stay at the building: tier upgrades, perk slots and the unit lock ([forces.md](forces.md)).

**Building:**
- An always-visible **build bar** grouped by category ([buildings.md](buildings.md)). Every button has a grid hotkey: **B**, then a letter.
- Shift places several in a row. The new building is selected after placing. Esc or right-click cancels placement.

**On the map:** the selected forces show their path line, a target highlight and their badge. Teammates see your orders as intent arrows.

**Controller [Later]:**
- X gives the smart order on the snapped target.
- One radial with fixed slots for Attack region, Retreat and the build menu.
- Shoulder buttons select nearby forces or all forces; the D-pad cycles through forces and alerts.
- The cursor snaps to regions, structures and badges.
- **Target-first menu:** select a region or structure and get *Hold here with…* / *Attack with…*, listing your forces with arrival times. It comes with controller support and also works on the war table.

**Today [Built]:** you click a production building, choose a goal in its inspector, and pick a region. That flow goes away.

## Awareness [Built] / [New] — decided

Research: [pacing.md](../Research/pacing.md). In the 2026-10-01 playtest the only signs of a winning push were a small enemy-HQ bar and JEV's alarm, which plays at its HQ out of earshot.

- [Built] **Team announcer:** a global voiced line plus a feed entry for every player, including the team's own successes. Today's objective events are either HQ under attack, at half health, at quarter health or offline; a region captured or lost; and one of the team's Drill Rigs lost. Entries identify the causing player's force and region; captures include every participating force.
  - Each affected structure has its own attack episode, ending after 20 s without damage to it. Within an episode, "under attack" speaks only for a new attacking force, not for a damage-tier change. Every valid hit, including a suppressed repeat or a threshold announcement, extends the episode and records the force. Ending the episode clears the announced-force set.
  - Each HQ hit produces at most one line: offline takes priority over a quarter-health crossing, then a half-health crossing, then "under attack". Half/critical lines are the damage-tier announcements; crossing multiple thresholds in one hit emits only the most urgent line. State transitions bypass episode suppression. Cancellation, cleanup and destroying JEV's Drill Rigs are not team combat losses.
  - Voice and feed definitions use the same wording and event IDs; [the voice pipeline](../Audio.md) supplies each line's asset. Speech is serialized with at most 16 pending lines: overflow discards the oldest pending line, and pending lines reaching the episode's quiet-window age are discarded rather than spoken late. A line already playing is not interrupted.
- [Built] **Objective strip:** always on above the battlefield, showing both HQs' health, regions held by each side, and commander-coloured role/force badges for the latest objective event. Economy remains visible above it.
- [Built] **Alert feed:** newest first; entries last 8 s and fade over their final 2 s. Clicking a visible entry focuses its event location without changing selection.
- [Built] **Space jumps to the latest alert;** pressing it again steps back through retained history, clamped at the oldest entry. A new event resets the next jump to the newest. The latest 64 events remain navigable after their feed entries fade. Jumping is optional: the strip and announcer carry the state.
- [New] **Failover Node/exposure events, nodes left and the hold timer:** arrive with the guarded-HQ objectives in [battle.md](battle.md); they are not displayed before those mechanics exist.

## JEV intent display

- **Timeline bar:** shows upcoming JEV plans, version releases and JEV calldowns, each with a countdown.
- **Region badges:** show which regions each plan targets.
- **Memo feed:** the Machine's voice and the jokes.

## Feedback and presentation — decided

| What | How it's shown |
|---|---|
| Counters | Hit flashes coloured by effectiveness: bonus hits flash bright, normal hits plain. Every force badge, including JEV's, shows its armor and damage icons. No floating numbers. |
| Supply chain | **Always-visible cables** across region borders between connected regions, in team colours. A cut snaps the cable, flashes the region and greys its Drill Rigs. |
| Alerts | [Built] Objective feed, global announcer and explicit camera jumps described above. [New] Voiced supply cuts, JEV releases and calldowns follow their mechanics in [economy.md](economy.md) and [jev.md](jev.md). |
| Incident report (after each battle) | A timeline graph of income, forces and regions with key events; **3 "why" callouts**, e.g. *Line cut at 4:12 cost 600 Power*; plus a Machine memo for the joke |
| Contribution card | **1–2 positive highlights per player**, e.g. *Saved West Cut at 6:40* or *Gifted 800 Power*, plus personal stats. It never ranks players. |
| Music | **Adaptive layers tied to JEV releases:** calm, then build-up, then a peak around each release and wave, with today's ambience underneath |
