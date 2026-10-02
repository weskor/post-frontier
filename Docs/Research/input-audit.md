# Post-Frontier: input audit of the current build

Source is ground truth. Paths are relative to `game/Source/CoopRTS/` unless stated otherwise. `PC` = `CommandPlayerController.cpp`, `HUD` = `CommandHUD.cpp`.
HUD geometry is in **virtual px**: 1280×720 reference, scaled by `clamp(min(W/1280, H/720), 0.78, 1.0)` (HUD:59-62, 209). At 1600×900 the scale is 1.0. At 1280×720 or smaller it is 0.78 or lower.

---

## 1. Bindings

### 1.1 Binding source
- `Config/DefaultInput.ini` only selects the Enhanced Input classes (3 lines). It has **no** action or axis mappings.
- All bindings are created in C++ at runtime. A single `UInputMappingContext` gets one boolean `UInputAction` per key (PC:88-120).
- `bTriggerWhenPaused` is true only for **LMB, Esc and Enter** (PC:98). In paused solo menus, every other key is dead.
- Middle mouse is **not** an action. `PlayerTick` polls it with `IsInputKeyDown(MiddleMouseButton)` (PC:148-160).
- There are **no gamepad keys**, **no edge scroll**, and **no rotation**. The cursor is never locked to the viewport (`EMouseLock::DoNotLock`, PC:44-46), so edge scroll could not work even if it were added.
- The cursor shape is never changed: `DefaultMouseCursor = Default` (PC:34), and no code sets `CurrentMouseCursor`.

### 1.2 Binding table by mode

Modes:
- **Normal**: Game screen, no pointer mode.
- **Place**: `bPlacingBuilding`.
- **Goal**: `bAssigningGoal`.
- **Menus**: MainMenu, Pause, Controls, Audio, ConfirmLeave, ConfirmQuit, Result.

| Input | Normal | Place | Goal | Menus |
|---|---|---|---|---|
| **LMB** (Started, PC:109 → `SelectUnderCursor` PC:597) | 1) The HUD gets it first (`HandleHUDClick` PC:546). On the **minimap**, the camera centres on that point (PC:563-565). On any other **panel rect**, the click runs the button's action or is swallowed (PC:567-568). 2) Otherwise the click raycasts on `ECC_Visibility` (PC:267-270) and runs `SelectActor` (PC:572-595). | Minimap: pans the camera; it does **not** place (PC:556-565). Panel: button action. World: the preview is validated and `ServerPlaceBuilding` is sent (PC:601-616). | Minimap: assigns the goal to the region under that point (PC:558-561). Panel: button action. World: ground-plane hit → `AssignGoalAt` (PC:618-624). | Every click is sent to the screen button hit test (PC:550-553). Clicks outside buttons do nothing. |
| **RMB** (Started, PC:110 → `CancelPointerMode` PC:718-725) | **Nothing.** No deselect and no order. | Cancels placement. Feedback: "Mode cancelled." | Cancels goal pick. Feedback: "Mode cancelled." | Nothing. Not paused-enabled. |
| **Wheel up / down** (Started, PC:107-108) | Zoom 15% per notch. Arm length is clamped between 700 and the arena half-extent (CommandCamera.cpp:99-105). Works over the HUD and minimap. | Same | Same | Ignored (PC:264-265) |
| **MMB hold + drag** (polled, PC:148-160) | Grab-pans the camera so the ground point under the cursor stays put (CommandCamera.cpp:53-97). Works even when the drag starts over the HUD or minimap. | Same | Same | Cleared (PC:127-131) |
| **W/A/S/D** (Triggered, PC:103-106) | Pans at a speed of `ArmLength × 1.1` per second, so speed scales with zoom (CommandCamera.cpp:45-51). | Same | Same | Ignored (`PendingPan` is zeroed, PC:127-131) |
| **Space** (PC:111 → PC:703-716) | Snaps the camera to the selected owned building, or to the friendly HQ if nothing is selected. Never targets the force's units. | Same | Same | Ignored |
| **Esc** (PC:112 → `Escape` PC:411-424) | Opens the Pause screen (pauses only in solo, PC:406-407). | Cancels placement. **No feedback text and no sound** (PC:413). | Cancels goal pick, same as Place. | Pause → Game. MainMenu → ConfirmQuit. Controls, Audio and Confirm screens → `ReturnScreen`. **Result: nothing**, but the click sound still plays (PC:422-423). |
| **F4** (PC:113 → `ToggleHUD` PC:540-544) | Collapses or expands the command deck. | Toggles the deck but **does not cancel the mode** (see §4). | Same as Place. | Ignored |
| **Enter** (PC:114 → `RequestRestart` PC:364-368) | Nothing | Nothing | Nothing | **Result**: `ServerRequestRestart`. **MainMenu**: immediately starts **Play Solo** on the selected map. Other screens: nothing. |

### 1.3 Clickable HUD surfaces in the Game screen
Hit testing and drawing share one source, `ForEachButton` (HUD:286-409). Only `Available()` buttons, meaning not blocked, can be hit (HUD:411-419).

**Always present:**
- `MENU / ESC`: 90×32, top-right (HUD:213, 348).
- `CONSTRUCTION`: 174×28 (HUD:217, 349).
- Minimap: 144×144, bottom-left (HUD:215).

**Deck expanded only (HUD:350):**
- Build cards: up to 6, each 154×≤40 (HUD:246-250, 359-364).
- Under-construction building selected: `CANCEL BUILD`, 230×36 (HUD:258-261, 368-372).
- Producer (barracks) selected, in three columns of about 224 px each:
  - Recipe rows: about 26 px tall when there are 3 recipes.
  - `START & LOCK / PAUSE / RESUME` row: 26 px.
  - Goal rows: **4 rows, (86−12)/4 = 18.5 px tall each** (HUD:238-244, 373-396).
- Workshop selected: three research cards, about 224×86 (HUD:252-256, 397-408).

**Click-blocking areas that have no actions** (`IsPanelPoint`, HUD:1453-1467):
- The top bar, up to 980×32.
- The mode bar (collapsed deck).
- The feedback strip (720×26, sitting above the deck).
- Empty parts of the deck panels.

A world click in any of these is swallowed.

### 1.4 Engine-level entry points that no player input reaches
These are declared and implemented, but no input or HUD path calls them. Only tests and the verification harness do (grep hits are confined to `*Tests.cpp` and `ArmyNetworkVerification.cpp`):
- `ServerIssueOrder` (Move/Hold/Retreat), PC:847-863, with player-facing feedback at PC:865-869.
- `ServerIssueAttack`, PC:812-839 and 841-845.
- `ServerAssignFront` (Secure/Defend/FallBack), PC:777-783.
- `EHUDAction::FrontSecure/FrontDefend/FrontFallBack` (CommandHUD.h:13) are never emitted by `ForEachButton`.

---

## 2. Click flows

"Click" means one LMB press. Selection counts assume the target is on screen.

### 2.1 Place a building — 2 clicks
1. Click a build card. HUD → `HandleHUDAction` → `BuildSlot`. This sets `bPlacingBuilding`, **collapses the deck**, and sets feedback "Left-click valid ground; right-click/Esc cancels." (PC:639-650).
   - The card is unclickable while short of funds: block `Funds`, label "Need N more" (HUD:351-357, 854, 877-878).
2. Click the ground.
   - **Client-side reject** (outside territory, overlapping, contested, or short of funds): feedback shows the reason, a Reject sound plays, and the mode **stays** (PC:607-612).
   - **Otherwise**: `ServerPlaceBuilding` is sent with feedback "Placement sent; server checks navigation and cost." (PC:613-615). Further clicks are ignored while `bPlacementPending` is set (PC:603).
   - **Server accept**: the mode exits, the deck re-expands, and feedback reads "Building placed; construction started." (PC:800-808, 740).
   - **Server reject**: the mode stays and a Reject sound plays.
- The new building is **not auto-selected**. The previous selection persists.
- Extractors snap to a free owned deposit within 300 cm (`PlacementPolicy.h:59`, `ResolveBuildingLocation` called at PC:327).
- There is no multi-place or queue. Each placement re-enters from the card.
- You cannot click a minimap location to place (minimap clicks pan, PC:556-565).
- To switch to a different building while placing, the build cards are hidden. Cancel first (RMB, Esc or CONSTRUCTION, then click a card = 2 clicks), or press F4 and click a card.

### 2.2 Configure a barracks (type, then Start & Lock) — 2–3 clicks after completion
1. Click the barracks (1 click), or click any of its units. While it is under construction, the inspector only offers `CANCEL BUILD` (HUD:368-372).
2. Optionally click a recipe row (`RecipeSlot`). This sends `ServerConfigureProduction(role, false)` (PC:693-699).
   - If you skip this step, Start uses the building's default `ProductionRole`. The HUD highlights that default recipe as active (HUD:159-167, 384-385).
3. Click `START & LOCK`. This sends `ServerConfigureProduction(role, !enabled)` (PC:688-692).
   - The fee (Siege 180) is shown as "+N fee". The button is blocked with "Need N more" when short (HUD:387-389, 941-947).
   - **No confirmation.** The lock is permanent: recipe rows become `ForceLocked` with the label "Type locked" (HUD:378, 855), and PC:695 silently ignores any recipe action afterwards.
4. After the lock, the same row toggles `PAUSE` / `RESUME` (HUD:941, 948).

### 2.3 Give a force a goal
**Hold / Expand** — 2 clicks with the barracks already selected, 3 with a selection click:
- Click `HOLD` or `EXPAND` (18.5-px row).
  - Before Start & Lock the rows are blocked with "Start & Lock first" (HUD:390-395, 856). The click is swallowed silently. The controller's own reject message at PC:666-671 can't be reached by mouse.
  - If allowed, this sets `bAssigningGoal`, **collapses the deck**, and sets feedback "Choose a region on ground or minimap; right-click/Esc cancels." (PC:683-686).
- Click a region on the ground or on the minimap.
  - **Outside every region**: feedback "Choose a region on the ground or minimap." plus Reject sound. The mode stays (PC:303-308).
  - **Cursor not on the ground plane**: feedback only, no sound (PC:622).
  - **Otherwise**: the mode exits, the deck expands, feedback reads "Goal sent; awaiting server." and `ServerAssignGoal` is sent (PC:309-312).
  - The server answers "Goal assigned to this building's force." or "Goal rejected: invalid or unreachable region, enemy main, or unavailable force." (PC:764-775).
  - On server rejection the pick mode has **already exited**, so you must click the goal button again.

**Assault / Fall Back** — 1 click with the barracks selected, 2 with a selection click:
- Clicking `ASSAULT` or `FALL BACK` sends `ServerAssignGoal(goal, INDEX_NONE)` immediately. There is no region pick and no confirmation (PC:676-682).
- No interim feedback is set. The previous feedback text stays until the server replies.

**Switching from one pending goal to another** (e.g. Hold → Expand): the goal rows are hidden while picking. Cancel first, or press F4 to re-show the deck and click a different goal.

**General:**
- There is no "cancel" for an assigned goal. It can only be replaced by another goal.
- Goals apply per barracks. There is no multi-barracks assignment.

### 2.4 Select a force by clicking one of its units — 1 click
- Unit capsules block `ECC_Visibility` (ArmyUnit.cpp:31-32, capsule radius 34 cm, half-height 60 cm).
- `SelectActor` maps an owned, living unit → `Group->GetProductionBuilding()` and selects that barracks (PC:576-592).
- If the producer is dead or not a producer, selection clears and feedback reads "Barracks destroyed; survivors keep their last front." (PC:585-589).
- Selecting **re-expands the deck** and plays the "Select" sound (PC:593-594). The camera does not move.
- Space then focuses the **barracks**, not the units (PC:707).
- The same click on any other actor **clears the selection silently**: ground, own HQ (`AHeadquarters` is not a `ACommandBuilding`), enemy unit or building, or a co-op teammate's building (PC:592).
- A click that hits nothing (e.g. sky) leaves the selection unchanged.
- There is no box select, no multi-select, no double-click, no control groups and no selection-cycling hotkey.

### 2.5 Buy a Workshop specialization — 2 clicks after the Workshop completes
1. Click the completed Workshop.
2. Click one of three research cards (150). This sends `ServerResearch` (PC:657-662). The card says "click to buy" (HUD:904-909).
- **No confirmation.**
- It is one per commander, across all workshops. After purchase the other cards show "Locked: one per commander" (HUD:403-405, 857), and the owned card shows "OWNED · ACTIVE".
- Server replies: "Workshop specialization purchased for your forces." or "Research rejected: …" (PC:786-792).
- Irreversible.

### 2.6 Upgrade a barracks — **not possible**
- `EHUDAction` has no upgrade action (CommandHUD.h:10-24).
- `grep Upgrade` over `Source/` returns no matches.
- There is also no demolish or sell for completed buildings. Cancel exists only during construction.

### 2.7 Cancel paths
| Action | How to cancel | Clicks | Feedback |
|---|---|---|---|
| Placement mode | RMB (PC:718-725); Esc (PC:413); `CONSTRUCTION` (PC:637); `MENU / ESC` button (also pauses, PC:455-459) | 1 | RMB: "Mode cancelled." Esc, CONSTRUCTION, MENU: the stale "Left-click valid ground…" text stays |
| Goal pick mode | Same four ways | 1 | Same |
| Building under construction | Select it, then `CANCEL BUILD`, which shows "refund N" (HUD:967-972). **No confirmation.** | 2 | "Construction cancelled; unbuilt portion refunded." (PC:744-750) |
| Recipe choice before Start | Click a different recipe | 1 | Server feedback "Paused: …" (PC:759-761) |
| Start & Lock | **Impossible**. Only `PAUSE` / `RESUME` production | — | — |
| Assigned goal | **Impossible**. Replace it with another goal | — | — |
| Research | **Impossible** | — | — |
| Pending placement RPC | Not cancellable. Clicks are ignored until the reply | — | — |

### 2.8 Pause
- **Pause:** Esc (when not in a pointer mode) or the `MENU / ESC` button. Both take 1 action, but Esc needs 2 presses when a pointer mode is active.
- **Resume:** Esc or `RESUME MATCH`.
- **Scope:** pausing only happens when `NM_Standalone` (PC:405-407). In co-op the menu opens but the match continues. The Pause screen says so (HUD:1429-1430).
- **Pause menu buttons:** Resume (plus Invite Friends when hosting), How to Play / Controls, Audio, Return to Main Menu, Quit. Return to Main Menu and Quit open confirmation screens; Back or Esc returns (HUD:315-340, PC:470-492).

### 2.9 Minimap
| Mode | LMB on minimap |
|---|---|
| Normal | Centres the camera on the clicked world point (PC:563-565) |
| Goal | Assigns the region under the point (PC:558-561) |
| Place | Pans the camera; it does **not** place |

- The click is a single **Started** event. Holding LMB and dragging does **not** keep panning.
- RMB, wheel and MMB act as they do in the world: the wheel zooms the 3D camera, MMB grab-pans the 3D camera.
- **What it draws** (CommandMinimap.cpp:184-292):
  - region polygon borders tinted by controlling team;
  - deposits (2-px diamonds), capture anchors, and buildings (the selected one gets an extra box);
  - both HQs, every living unit (2-px dot), and squad centres;
  - the selected barracks' front marker (only if a front exists, which players can't set);
  - the camera frustum footprint;
  - header "ARENA / CLICK TO PAN" and legend "HQ/base | sector | amber: contest/front".
- **Goal mode on the minimap:** hovering resolves a region (`CursorGoalRegion` PC:282-296). The hover highlight is drawn as a cyan outline **in the 3D world** (PC:228), **not on the minimap**, so it may be off-screen. The header still says "CLICK TO PAN".

---

## 3. What the HUD tells the player

### 3.1 Persistent elements
- **Top bar** (HUD:827-845): `C# · Power · +income/s · Forces N · Regions a/b`, plus HQ bars for both sides.
- **World overlays:**
  - HQ and building name with HP bars, plus a construction progress bar (HUD:660-690);
  - "REGION n" capture bars and deposit labels "POWER/RICH remaining +rate FREE/TAKEN/EMPTY" (HUD:692-731);
  - force-number badges over owned units and buildings, coloured by commander (HUD:733-768);
  - unit health bars only when damaged, **or for every unit of the selected force** (HUD:616-637).
- **Feedback strip:** a single line above the deck (HUD:1324-1330, layout HUD:224-225). It shows the last `Feedback` string **indefinitely**. It is cleared only by `ResetLocalMatchView` at travel (PC:66), with no timeout.

### 3.2 Nothing selected — "COMMAND OVERVIEW" (HUD:1205-1256)
- **Base counts:** barracks, producing, workshops, extractors, under construction, configured forces.
- **"NEXT STEP"** hint is derived from state (HUD:1233-1242):
  - no barracks → "Build a Barracks on green preview cells";
  - barracks building → "Plan its force type and goal";
  - none configured → "Select your Barracks, choose a permanent type and Start";
  - free deposits → "Build an Extractor…";
  - ≤1 region → "Choose Expand and pick a region…";
  - no workshop → "A Workshop unlocks one paid specialization";
  - otherwise "Set Barracks goals and push toward the enemy HQ".
  - Always followed by "Click an owned building."
- **"CONTROLS" key caps:** LMB Select, Space Focus, F4 Deck, WASD Pan, Wheel Zoom (HUD:1249-1255).

### 3.3 Under-construction inspector (HUD:1078-1100)
- Title, "UNDER CONSTRUCTION" status, a progress bar with "% · Ns remaining".
- A "When complete: …" line per building type.
- "Cancelling refunds the unbuilt share of the cost."
- The `CANCEL BUILD` button with "refund N".

### 3.4 Barracks inspector (HUD:1102-1157)
- **Header:** `BARRACKS n`, subtitle `C# · GOAL · region name` (or "region unavailable"), production status (`StatusText` HUD:170-186), and an HP bar.
- **Column FORCE TYPE:** "choose before Start", or "LOCKED". Each recipe row shows "capacity · cost/duration".
- **Column FORCE:** "joined a/b", a progress bar, "Building n: x/ys", "Travelling n · Vacant n", and "N resources per unit".
- **Column GOAL:** the current goal title in its colour. Each goal row shows "pick region / capture region / enemy main / regroup", or "CURRENT" for the active goal.
- **Footer remedy line**, depending on state:
  - Unconfigured → lock warning plus fee;
  - ForceComplete;
  - InsufficientResources ("Need N more…");
  - Paused ("click Resume");
  - DeploymentBlocked;
  - Producing.
- **In the world:**
  - a cyan square around the footprint (PC:229-237);
  - a cyan line from the barracks to the force centre (PC:247-258);
  - the current goal region outlined in the goal colour — green Hold, light-blue Expand, red Assault, yellow Fall Back (PC:211-227);
  - health bars on every force unit.

### 3.5 Workshop and Extractor inspectors
- **Workshop:** status "RESEARCH AVAILABLE" or "SPECIALIZED", a SPECIALIZATION detail line, and three cards each with two effect lines and cost / "click to buy" / "OWNED · ACTIVE" / block reason (HUD:1159-1171, 881-912).
- **Extractor:** "EXTRACTING POWER" or "DEPOSIT EMPTY", "REGION n · rich/normal deposit · taken", "+N Power/s to C# only · remaining", and two rule lines (HUD:1181-1201).

### 3.6 Placement mode — the deck is replaced by the mode bar (HUD:1269-1292)
- "PLACE \<NAME\>" with "cost · Ns build". The cost turns warning-coloured if you can't afford it.
- A coloured square plus a live reason string from `GetPlacementPreview` / `CanPlaceBuildingAt`, e.g. "Point at ground to place.", "Need N more resources.", validation reasons (PC:321-349).
- Key caps "LMB Place" and "RMB / Esc Cancel".
- **In the world** (PC:162-209): a 22×22-cell window of territory tint (green = buildable territory, red = not), grid lines, and the footprint cells in solid green or red.

### 3.7 Goal-pick mode (HUD:1293-1305)
- Mode bar: "SET HOLD/EXPAND GOAL", "Pick a region on ground or minimap; this barracks' force follows its goal.", and key caps "LMB Assign" and "RMB / Esc Cancel".
- The hovered region is outlined **cyan** in the world (PC:211, 228).
- **No preview** of the path, reachability, or validity (e.g. enemy main, or unreachable for Expand). Invalidity is learned only from the server reply after the mode has exited.

### 3.8 Deck hidden with F4, no mode (HUD:1306-1321)
- "COMMAND DECK HIDDEN".
- "Selected: your X · status", or "Nothing selected · click an owned building".
- Key cap "F4 Show deck" and the text "Use Construction to reopen".

### 3.9 Button states
- **Hover:** card tint plus accent outline (HUD:870, 888-890, 981-983). Screen buttons get a gold outline (HUD:1357-1363).
- **Blocked:** dimmed card plus reason text — "Match over", "Need N more", "Type locked", "Start & Lock first", "Locked: one per commander" (HUD:847-861).
- **Clicking a blocked button does nothing**: no sound, no feedback (HitTest only returns available buttons, HUD:416).

### 3.10 Sounds
- `Select` on a successful selection (PC:594).
- `Click` on HUD actions (PC:636, 507).
- `Front` on goal buttons (PC:636).
- `Reject` on client/server rejection (PC:310, 610, 670, 797, 809).

### 3.11 Not present
- No world hover highlight on units or buildings in Normal mode.
- No tooltips.
- No cursor changes.
- No selection ring on units.

---

## 4. Inconsistencies and friction

### 4.1 One input meaning different things
1. **LMB** selects in Normal, places in Place, and assigns a region in Goal. While placing or picking, you cannot select another building: clicking a building tries to place, or assigns the region under it (PC:601-624).
2. **LMB on the minimap** pans in Normal and Place but assigns in Goal (PC:556-565). The minimap header says "CLICK TO PAN" in every mode (CommandMinimap.cpp:285-286).
3. **Esc** is cancel in Place/Goal, pause in Normal, back in sub-screens, quit-confirm on the main menu, and a no-op on Result. Leaving a pointer mode to pause takes two presses.
4. **The `CONSTRUCTION` button** never opens a construction list. It runs `CancelMode()`, which cancels any pointer mode and re-expands the deck (PC:637, 532-538). Clicking it with the deck already open and no mode does nothing except play a click.
5. **Enter** restarts on Result but **starts a solo match on the main menu** (PC:367). The main-menu Enter is documented nowhere.
6. **The same row** is `START & LOCK` (irreversible, costs a fee) before configuration and a harmless `PAUSE` / `RESUME` toggle after it (HUD:941).

### 4.2 Mode and state friction
7. **F4 during Place or Goal expands the deck but leaves the mode active** (PC:540-544 vs 413/720). The mode bar, with its hint, key caps and placement reason, is only drawn when the deck is collapsed (HUD:1561-1568). So the player is still in Place or Goal mode with no on-HUD indication except the world grid or cyan outline. This F4 route is also the only way to switch the pending goal or building type without cancelling first.
8. **Esc, CONSTRUCTION and MENU cancel without updating feedback.** The strip keeps showing "Left-click valid ground; right-click/Esc cancels." or "Choose a region…" after the mode is gone. RMB alone writes "Mode cancelled." (PC:723).
9. **Feedback never expires** (PC:66 is the only reset). Old server messages linger indefinitely, and the strip blocks world clicks under it (HUD:1466).
10. **Hold/Expand goal pick exits before the server validates** (PC:309-312). On rejection the player must click the goal button again and re-pick.
11. **Assault/Fall Back fire on one click with no confirmation** and no interim "sent" text. Hold/Expand get "Goal sent; awaiting server."
12. **Irreversible actions with no confirmation:** Start & Lock (with a fee), research purchase, and Cancel Build.
13. **Blocked buttons swallow clicks silently.** The richer controller messages are effectively unreachable via the mouse, e.g. "Complete this barracks and Start & Lock its force…" (PC:668) and "Select your building first." (PC:651).
14. **Placing a building does not select it.** Configuring it needs a separate world click later, after the 9–12 s build time.
15. **Click-to-select semantics are lossy.** Clicking ground, your own HQ, enemies or teammates' buildings silently deselects. RMB never deselects. The HQ cannot be selected or inspected; it is only reachable via Space when nothing is selected.

### 4.3 Missing paths and dead ends
16. **No way to select a force except through one of its units or its barracks.** Every per-force action (type, start, goal) lives only on the barracks inspector, and every one is mouse-only. There are no hotkeys for any deck button, force, building, or goal.
17. **Selecting a force via a unit moves the inspector to the barracks**, which may be far away. Space jumps the camera to the **barracks**, not to the force.
18. **Survivors of a destroyed barracks cannot be selected or commanded** (PC:585-589).
19. **Dead code paths with player-facing strings:**
    - manual Move/Hold/Retreat/Attack orders (PC:812-869);
    - Secure/Defend/FallBack fronts (PC:777-783; CommandHUD.h:13).
    - Related display code still renders front markers (PC:238-246, 254-257; CommandMinimap.cpp:270-278).
    - The Entrenched Frontline card says "while stationary at a Defend front" (HUD:897-898), but the player cannot issue a Defend front.
20. **Unreachable text:**
    - The overview "Match over. / Press Enter for a fresh match." (HUD:1235) never shows: a terminal match forces the Result screen (PC:382-385), which replaces the deck (HUD:1547-1551).
    - `CanIssueGameplayCommand`'s "Match over: press Enter to restart." (PC:357-362) fires for **any** non-Game screen, e.g. Pause, not only match over. In practice `HandleHUDClick` intercepts non-Game clicks first (PC:550-553), so it is nearly unreachable.
21. **No upgrade, no demolish, no rally point, no production queue.** Production is one continuous refill toggle.
22. **Minimap:** no drag-pan, no in-minimap hover highlight in Goal mode, and no placement via the minimap.

### 4.4 Mismatches between docs and bindings
**In-game "HOW TO PLAY" screen** (HUD:1403-1417):
- Says "LMB: select / place / assign. RMB or Esc: cancel targeting." This is correct, but it omits:
  - minimap click-to-pan and minimap goal assignment (step 3 does mention the minimap for goals);
  - clicking a unit to select its barracks;
  - the `CONSTRUCTION` button;
  - Enter (restart, or solo start on the main menu);
  - Esc doing nothing on Result.
- Step 2 says "then Start". The button reads "START & LOCK".
- Step 6 says Fall Back "regroups at your barracks". HOW-TO-PLAY rule 9 says "home/rally point". This is a behaviour description mismatch, not a binding one.

**In-deck CONTROLS key caps** (HUD:1251-1255) list only LMB, Space, F4, WASD and Wheel. They omit MMB drag, RMB and Esc.

**`Saved/Verification/playtest-tonight/HOW-TO-PLAY.md`:**
- The controls paragraph (line 37) matches the source: WASD or MMB pan, wheel zoom, Space focus, LMB select/place, minimap pans when not picking a goal, F4 deck, Construction reopens, Esc pauses solo.
- Rule 8 (line 29): "right-click/Esc cancels" matches.
- Rule 5: the 300 cm deposit snap matches `DepositSnapRadius = 300` (PlacementPolicy.h:59).
- Rule 10: "click your unit to select its barracks" matches PC:576-590.
- Omissions: Enter, F4 hiding the mode bar while a mode is active, the RMB vs Esc feedback difference, and the main-menu Enter shortcut.
- Opening advice says to build an extractor first. The in-game NEXT STEP hint says to build a Barracks first and only suggests an extractor after a force is configured (HUD:1236-1239).

**Minimap header** "CLICK TO PAN" is wrong during Goal mode.

**Overview hint** "Click an owned building." (HUD:1246) omits that clicking an owned unit also works.

---

## 5. Controller-hostile elements
- **No gamepad support at all.** Only keyboard and mouse keys are mapped (PC:103-114). DefaultInput.ini has no mappings. There is no virtual cursor and no focus or navigation model.
- **Everything except camera keys requires pointing.** Every deck action, every menu button, selection and region picking go through screen-space hit rects or a cursor raycast (HUD:411-419, PC:597-627). Menus support only Esc and Enter from the keyboard; there is no up/down/confirm navigation.
- **Hold/drag:** MMB drag-pan (PC:148-160). There is no drag on the minimap.
- **Hover-dependent information:**
  - The Goal-mode region preview is only the cyan outline under the cursor (PC:211, 228). On the minimap that outline is drawn in the 3D world, possibly off-screen.
  - The placement preview and its reason follow the cursor (PC:162-209, HUD:1281-1288).
  - Button hover states.
- **Small or precise targets** (virtual px; ×0.78 at ≤1280×720):
  - Goal buttons are **18.5 px tall** (≈14 px at 0.78) and sit directly under each other (HUD:238-244, 392-395).
  - Recipe and Start rows are 26 px.
  - The minimap is 144 px square (≈112 px), carrying 15 v2 regions; deposits are 2-px diamonds and units 2-px dots.
  - Build cards are 154×40.
  - `MENU / ESC` is 90×32.
- **Moving targets:** selecting a force requires clicking a moving unit capsule 68 cm wide. `[INFERENCE]` At 1600-px width with a 60° FOV, that is about 39 px wide at the default 2400 arm length, and about 21 px at a 4500 arm length (the default `ArenaBounds::HalfExtent`, which caps zoom-out, ArenaBounds.h:30; per-map values are not checked).
- **Selection is click-only:** there are no cycle or next-force or next-building inputs. Space only focuses the current selection or the HQ.
