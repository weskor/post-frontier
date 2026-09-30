---
name: verify-cooprts
description: Verify CoopRTS Unreal Engine gameplay on Linux with live state assertions first, targeted packaged-input and visual checks, build freshness, and explicit evidence limits. Use for gameplay changes or verification workflow maintenance.
---

# Verify CoopRTS

Use live Unreal state assertions for gameplay rules; use the packaged game for input integration and observable presentation. Screenshots are visual evidence, not the primary gameplay test. Neither compilation, accepted-order logs, nor an automation-only pass proves every layer. Never change product behavior or weaken assertions to make verification pass.

This is a project-local OMP/Agent Skills skill, not a Cursor plugin. Discovery occurs at session startup; after adding it, a fresh session can use `/skill:verify-cooprts`. Within an existing session, read this file directly. Commands below run from the repository root. Read `features/README.md`, then the files for the affected features. For a complete baseline, cover every mapped feature.

## Tiers

Prove the cheapest tier that can fail for the change, then move outward only for a contract the inner tier cannot express. Each tier has its own command and its own limit; a pass at one tier is never a claim about another.

| Tier | Command | Proves | Cannot prove |
| --- | --- | --- | --- |
| Rules | `verify.py regression --scenario rules` | Every `CoopRTS.Rules.*` deterministic test in one editor process: production precedence, progress preservation, deployment-due boundary, terminal freeze, exactly as asserted in `Source/CoopRTS/RulesTests.cpp` | Navigation, actors, replication, rendering |
| World | `verify.py regression --scenario <construction\|production\|strategy\|...>` | One latent test on a fresh standalone selected world with real navigation, placement, payment and arrival | Client ownership, replicated state, presentation |
| Network slice | `network.py --scenario ownership\|production\|economy\|restart` | One replicated contract on fresh host+clients over loopback sockets, observed through the in-process probe | Other slices, OS input, rendering, WAN |
| Presentation | `hud_capture.py --quick <label>` (deck and selected-barracks inspector) or the full `hud_capture.py` sequence | Offscreen-rendered HUD states through shared hit geometry | OS/compositor input, focus, other resolutions than requested |
| Acceptance | `network.py --scenario construction` plus the packaged full `hud_capture.py` and the desktop drive recipe | The whole chain in one continuous world | Human coordination, real WAN, unscripted play |

- Rules: `verify.py regression --scenario rules --map <package path>` selects the launched level; Boot (`/Game/Maps/Boot`) remains the default and rules assertions remain world-free.
- World: `verify.py regression --scenario <scenario> --map <package path>` selects the level; Boot remains the default.
- Network slice: `network.py --map <package path>` selects the listen-host level that clients join; Boot remains the default.
- Presentation: `hud_capture.py --map <package path>` selects the offscreen level for quick or full captures; Boot remains the default.
- Acceptance: select the same level with `network.py --map`, `hud_capture.py --map`, and desktop `verify.py launch --map` or `network_desktop.py launch --map`; Boot remains the default.

Map arguments accept `/Game/...` package paths without extensions or URL options; only argument shape is validated, and the engine resolves existence at launch. Missing or unstarted requested maps are launch failures, never PASS. Map selection adds no runtime evidence for additional levels.

## Development checks versus acceptance runs

**Default to targeted development verification, not the full milestone matrix.** Before launching, state the changed behavior, the smallest scenario that exercises it, the observable pass condition, and any required native input/rendering check. Start with one scenario; add another only for a distinct affected contract that the first cannot prove.

- For an ordinary multiplayer change, start with host plus one remote. Use five players, packet loss or a stopped peer when the specific failure requires that topology/fault, or during a separately scheduled acceptance run—not automatically after every change.
- `network.py --scenario ownership|production|economy|restart` are independent slices; each starts its own host and clients, funds and isolates as needed, and stops when its contract is proved, so one stall never blocks unrelated evidence. `--scenario construction` (the default) is the acceptance chain running all four in one continuous world. All accept `--clients 0|1|4`; use host plus one remote for ordinary ownership/replication checks. If no slice covers the changed behavior, state the gap rather than naming an unsupported option.
- The full construction chain is an acceptance run, not a development gate. Use the matching slice during development and record exactly its assertions. Stop an owned stalled run instead of withholding build/HUD work for unrelated later stages. An interrupted or partial chain is not a full network PASS.
- Full gameplay baselines and the complete multiplayer/fault matrix are explicit milestone/release verification activities. Implementing a milestone slice does not automatically authorize repeating the entire matrix. Record deferred acceptance work without claiming full milestone completion.
- Run one scenario at a time and inspect its result before scheduling another. Do not queue a long build/test/retest chain that can monopolize the shared artifact or hide a stalled stage.
- A failed check blocks claims about the affected behavior, not unrelated development. Preserve its evidence and separate product defects from harness/setup failures. Make a targeted correction and rerun the affected scenario; if it still fails or stalls, record the blocker and move further investigation into an explicit task rather than silently expanding the current feature task.
- Acquire the shared build/runtime window only for the selected build or check. Stop owned processes and release that window before unrelated investigation, documentation work or a deferred verification task. A background run is not nonblocking if it still holds the artifact/desktop lock.

## No command timeout does not mean unattended waiting

Do not add command deadlines, `-seconds`, or longer travel delays to force a result. Supervise meaningful progress instead:

1. When reports repeat the same pending condition without relevant state advancing, inspect the exact predicate and current peer snapshots/logs immediately. A living process, increasing request IDs or repeated successful `observe` replies are not gameplay progress. Changing construction progress, squad production, roster replication or travel generation can be.
2. Determine whether the expected state is still reachable. Separate replication/loading convergence from assertions about a completed state. Do not wait indefinitely for a transient condition—such as neutral territory after autonomous play has resumed—to become true again.
3. If the predicate is impossible, a terminal error is present, or progress cannot be established, interrupt the exact owned runner through its cleanup path. Preserve the run as failed/interrupted, confirm owned children stopped, and release shared resources. This is an evidence-based stop, not an elapsed-time test failure.
4. Resume only with a concrete diagnosis and a targeted next check. Do not restart an unchanged stalled scenario, weaken its assertions, or keep polling while investigating unrelated work.

For reset tests, observe initial state before autonomous play can change it. A controlled fixture may then stop gameplay to permit delayed replica inspection, but must not manufacture the reset values being asserted. Document this setup separately from normal gameplay proof.

## Choose the proof before running

1. Name the changed behavior and its failure condition. Read the relevant source and [feature map](features/README.md); select the scenarios and surface checks that actually exercise it.
2. Run live assertions first for construction, paid production, fronts, capture/outpost income, research, damage, targeting, order transitions, navigation and per-unit arrival. Legacy combat/orders/movement effect scenarios create controlled squad fixtures; they do not demonstrate the new-match construction loop. A standalone authoritative world does not prove client/server replication or hostile-client validation.
3. Exercise real packaged input when bindings, hit testing, selection, cursor handling or input-to-order integration changed. Correlate input attempts with the resulting order/state, not just key delivery.
4. Inspect rendering when camera behavior, HUD, cursor, assets or effects changed. Use the smallest set of captures that proves the visual transition; a before/after pair is often enough. Temporal visuals may need a short sequence. Numeric gameplay invariants belong in assertions, not screenshot counting.
5. For a new gameplay feature, include a short packaged end-to-end encounter as well as its assertions. For a targeted correction, do not repeat every baseline recipe. A full milestone/release baseline covers all mapped features.

| Changed path | Primary live scenario | Additional surface proof |
| --- | --- | --- |
| Construction placement, cost, territory, cancellation or outpost income | `construction` | Fresh package: build at HQ, capture sector, place outpost, inspect completion, wallet and sustained income; see [construction](features/construction.md) |
| Barracks type configuration, capacity, production, front or casualty recruitment | `production`; add `movement` only for changed fixture paths | Fresh package: lock a force type, inspect individual production/full/pause/funds states, front movement and physical replacement |
| Enemy construction, production, expansion or defense | `strategy` | Fresh package: observe paid enemy barracks/production, capture, outpost and defensive front if reached |
| Combat rules, role stats, target lifetime or pursuit | `combat` (fixture-owned groups) | Automatic-front encounter and visible hit/health when applicable |
| Internal Move/Hold/Retreat order replacement or validation | `orders` (fixture-owned groups); add `combat` if precedence changed | Player-facing changes use barracks fronts, not removed manual squad controls |
| HQ damage, outcome, terminal commands or restart | `match-win` and `match-loss`; add `strategy` if planner changed | Fresh package: actual HQ weapon damage, terminal guard, Enter reset to empty construction bases |
| Formation paths, crowd movement or boundary rejection | `movement` for fixture crossings; `production` for actual force recruitment; add `orders` if cancellation changes | Relevant paid-force routes and physical arrival, not just accepted destination markers |
| Capture, wallet income or established territory | `construction`; add `strategy` for enemy economy | Packaged capture followed by completed outpost and sustained territory income; bare capture is insufficient |
| Workshop specialization and owned effects | `construction` for purchase, plus the affected `doctrine-siege`, `doctrine-repairs`, `doctrine-frontline` or `doctrine-restart` for effect/restart | Click an owned completed workshop purchase card; verify wallet/choice, then the relevant produced-squad effect |
| Controller input, selection, HUD, minimap or cursor | Matching construction/production scenario; `doctrine-frontline` for stationary Defend mitigation | Actual affected building/HUD/minimap input and visible result. `scripts/hud_capture.py --quick <label>` renders the deck and one selected barracks inspector offscreen; the full run adds production, starvation, fronts, research and victory states and checks shared hit geometry/camera-only minimap dispatch. Neither proves OS input |
| Camera or visual-only assets/effects | No numeric camera/rendering scenario | Native input and inspected rendering; do not claim numeric limits without measuring them |
| Real multiplayer ownership, economy, outcome or travel | [Real network recipe](features/multiplayer.md): `network.py --scenario construction` with host+remote; five-player/fault only if required | Focused native host/client proof for affected input/HUD; human coordination remains unverified |
| Verification docs only | Check commands and assertions against implementations | No game rebuild or repeated desktop run just for prose; identify existing current runtime evidence and limits |

Select each required scenario once, even if several features map to it. The full recipe lists below are probe menus, not a requirement to capture every step for every change. Existing visual evidence may be reused only when the relevant packaged build and visual/input path are unchanged; cite its run and explain the exclusion. Never reuse old runtime evidence as proof of changed gameplay.

## Live assertions first

No packaged launch, compositor access or screenshot is needed for these scenarios. Use the installed UE engine and a fresh `CoopRTSEditor` module. Build with the root README command after C++/test changes. `regression` rejects a missing module or one older than any `Source/` `*.h`/`*.cpp`/`*.cs` file or the project descriptor, records its size/mtime in `actions.jsonl`, and rejects module changes during the run. Limit: only those suffixes under `Source/` are compared (editor runs load uncooked content); engine, plugin, toolchain, `Build/` and generated-code changes are not detected, and a newer mtime is a heuristic, not a reproducible-build guarantee. Package timestamps are not editor-build proof.

Start with the rules tier when production policy, precedence or costs changed:

```bash
V=.agents/skills/verify-cooprts/scripts/verify.py
"$V" --run Saved/Verification/rules-unique regression --scenario rules
```

`rules` runs `Automation RunTests CoopRTS.Rules`: every test beneath that path in one process. The runner passes only when at least one `Test Completed` line under `CoopRTS.Rules` was reported, every one is `Result={Success}`, none is `Result={Fail}`, the process exited zero and SoftQuit printed `**** TEST COMPLETE. EXIT CODE: 0 ****`; the `regression-result` record lists the completed and failed paths. Rules tests use no world actors, navigation or replication; a rules pass says nothing about deployment, travel or the HUD.

```bash
V=.agents/skills/verify-cooprts/scripts/verify.py
LOGIC_RUN=Saved/Verification/change-logic-unique
"$V" --run "$LOGIC_RUN" regression --scenario construction
```

Replace the suffix with a fresh identifier. `regression` defaults to `construction`; explicit `construction` runs `CoopRTS.Construction.Lifecycle`, and `production` runs `CoopRTS.Construction.Production`. `strategy` runs `CoopRTS.Enemy.ConstructionEconomy`. Existing `orders`, `movement`, `combat`, `match-win`, `match-loss` map to `CoopRTS.Orders.ReplaceHoldRetreat`, `CoopRTS.Movement.TwoGroups`, `CoopRTS.Combat.Encounter`, `CoopRTS.Match.VictoryRestart`, `CoopRTS.Match.DefeatRestart`. The `doctrine-siege`, `doctrine-repairs`, `doctrine-frontline`, `doctrine-restart` choices map to `CoopRTS.Doctrine.SiegeOptics`, `CoopRTS.Doctrine.FieldRepairs`, `CoopRTS.Doctrine.EntrenchedFrontline`, `CoopRTS.Doctrine.Restart`. No `economy`, `objective` or `objective-defeat` regression choice exists (`economy` is a network slice). Each run needs its own fresh standalone world on the selected map (Boot by default); serialize with builds and game instances.

Earlier evidence (restructure, 2026-09-30): `Saved/Verification/restructure-20260930/RESULTS.md` records the rules tier (4 `CoopRTS.Rules.Production.*` results in one 9.6 s process), construction/production/strategy/match-win/match-loss world scenarios, all four network slices on editor host+one remote (ownership 27 s, production 103 s, economy 122 s, restart 70 s), editor and packaged `--quick` captures, the full editor HUD sequence at two resolutions, and a throwaway fourth unit rendering a fourth recipe row without C++ changes. `force-production-f-20260930` and `force-hud-package-a-20260930` predate the restructure (definition indices, `EProductionState`, placed HQ/sector/arena actors, probe fields `productionState`/`constructionProgress`/`arenaHalfExtent`/`gameStateId`) and are historical. Native OS input, the full `construction` acceptance chain and five-player/fault topologies were unverified at that initial checkpoint; later proof must be read against its recorded artifact.

Current map-integration evidence: `Saved/Verification/map-integration/RESULTS.md` records 14 rules results, Boot/AvailabilityZone strategy, Boot socket economy, natural two-human income (+20 each / +40 JEV), eight-sector editor/package connected restart, the all-three-map package, default AvailabilityZone startup and cooked HUD. Native solo and host/client launch/focus/capture/stop also passed; screenshots are partly obscured, so only the unobstructed offscreen inspector is full-surface visual proof. Full new-map construction acceptance, five-player/fault, native build/front sequences and human balance remain unverified.

Require process exit zero, the exact scenario's `Test Completed. Result={Success}` (for `rules`: every completed result beneath `CoopRTS.Rules` and no `Result={Fail}`), and `**** TEST COMPLETE. EXIT CODE: 0 ****` from UE Automation SoftQuit. The regression runner waits for natural completion without `-seconds` or a subprocess timeout. Preserve the matching log, stdout and `regression-result` action record. A timed exit, an unrelated test's success, or `Attack accepted` is not a pass. Failures remain failures: diagnose the assertion and state, fix the cause, then run in a new evidence directory. Do not loosen assertions merely to get green.

Coverage is limited to assertions actually present. `construction` checks paid/rejected placement, completion, cancellation, owner isolation, configuration lock, workshop purchase, real capture, completed outpost income and destruction. `production` checks type/configuration costs, no premature unit, independent capacities including travellers, per-unit payment, dynamic recruitment and actual arrival, recruit death, complete-wipe identity/refill, pause/starvation/blocked deployment, terminal guards and destroyed-producer survivors. Both use setup budgets and controlled isolation, not native playthroughs. `strategy` checks the enemy's paid units, capture/outpost income, reactive defense and producer-scoped recovery through natural repairs. Fixture `movement` checks per-member path success/distance/velocity; `orders` checks center return. The explicit Development probe can read standalone diagnostic worlds as well as network worlds; authority fixtures remain restricted to an opted-in listen host. Standalone observations do not prove replication.

## Real multiplayer sockets

Standalone automation cannot prove client ownership, replicated wallets or travel. For a selected network check, build the fresh editor target after source handoffs. For full acceptance, establish editor host+remote proof before packaging and proceeding through the [multiplayer recipe](features/multiplayer.md). Each run starts separate game processes, listens on an IPv4 loopback socket, drives the existing owning-controller Server RPC from an actual remote client world, and independently reads each world's replicated actors. Nothing in this probe runs without explicit per-process Development command-line opt-in; only the host gets a separate authority-fixture switch. No source changes or package mutation during a run. Do not enable fixtures on public servers.

Development slices, one contract each on fresh host+remote. Each starts its own processes, isolates the enemy, pauses income and funds explicitly; nothing carries over between slices:

```bash
N=.agents/skills/verify-cooprts/scripts/network.py
"$N" --run Saved/Verification/ownership-editor-unique --mode editor --clients 1 --scenario ownership --max-fps 60
"$N" --run Saved/Verification/production-editor-unique --mode editor --clients 1 --scenario production --max-fps 60
"$N" --run Saved/Verification/economy-editor-unique --mode editor --clients 1 --scenario economy --max-fps 60
"$N" --run Saved/Verification/restart-editor-unique --mode editor --clients 1 --scenario restart --max-fps 60
```

- `ownership`: paid barracks placement charging exactly 220, rejected duplicate/outside-arena placement, foreign role/cancel/front RPCs, game-time completion, siege first-Start fee of exactly 180 with 2/50/20⁄3 per-unit values, then locked/invalid/foreign commands rejected atomically behind an owner resume/pause barrier.
- `production`: one recruit for exactly 50, physical departure and travel (movement latched per peer when first observed, not required simultaneously), arrival, two-slot `ForceComplete`/`Paused` states, a second producer with its own force and front, real casualty, paid replacement retargeting to a moved front and arriving without touching the other force.
- `economy`: after a two-unit siege force is recruited, natural sector capture without income, paid outpost holding territory after departure, workshop research paid once and scoped, automatic-front HQ damage, fixture-shortened victory.
- `restart`: after one recruit, research and a fixture victory, the connected restart; every peer must converge on a predicate that includes the reset itself: fresh generation, same commander, result 0, both HQs at 900, no team-0 armies or buildings, doctrine reset, wallet ≥ 600 and the same sector count as before travel, all neutral with zero progress, plus a new GameState id on the preserved net driver.

Acceptance chain and topology/fault menu (not a per-change script; not proof of the former full/objective/three-cycle delayed-peer checks). Run individually and inspect each result:

```bash
"$N" --run Saved/Verification/construction-editor-two-unique --mode editor --clients 1 --scenario construction --max-fps 60
"$N" --run Saved/Verification/construction-package-two-unique --mode packaged --clients 1 --scenario construction --max-fps 60
"$N" --run Saved/Verification/construction-package-five-unique --mode packaged --clients 4 --scenario construction --max-fps 60
"$N" --run Saved/Verification/construction-package-five-loss-unique --mode packaged --clients 4 --scenario construction --emulation --max-fps 60
```

Positions in these scripts are offsets from the replicated HQ, sector and arena state the probe exposes (`friendlyHQPosition`, `enemyHQPosition`, `sites[].position`, `arenaHalfExtent`), never literal map coordinates; regenerated maps do not require script edits. Progress reports (`Pending …` lines and `waiting` events) list per-building `constructionProgress`, `joined`/`travelling`, `productionState` and each reinforcing unit's position so a stall can be classified without opening logs.

Require the runner's PASS record, native net-mode/peer identity, per-peer `responses.jsonl` and game logs, observed replicated convergence, and pidfd cleanup. The emulated run must read actual net-driver `PktLag=120` and `PktLoss=8` from every world. Network waits have no wall-clock deadline: the runner prints the pending predicate, elapsed time, latest peer state/connection health, and fails on terminal network/travel failures, contradictory states, process exit or artifact mutation. Positional wait numbers are reporting intervals, never success/failure deadlines. An unstarted packaged `-nullrhi` session is a blocker; report it, do not infer success. Separate native-window visual/input proof uses `network_desktop.py` from the recipe and focused per-peer `doctor`/`focus`/`capture`/`key`/`click`. The original `verify.py` remains single-instance. Numeric snapshots do **not** prove readable five-player teamplay or absence of a deathball; require a human five-player session for that criterion.

The current construction socket scenario has one connected restart cycle; the earlier three-cycle forced-delay result remains historical and was not rerun after cutover. See [multiplayer](features/multiplayer.md); a current socket PASS must not be presented as delayed-peer acceptance.

If packaged `-nullrhi` actually fails on the fresh artifact, preserve that failure and rerun in a new directory with `--rendered` (same sockets and assertions, Vulkan windows). The exact example and visual limits are in the multiplayer recipe. Do not assume package NullRHI support before observing it.

## Packaged launch when needed

Prerequisites: UE 5.8.3 with the README shader patch, a working Linux Vulkan desktop, Hyprland with the Lua dispatch API and wlr virtual-pointer protocol, `python3`, `cc`, `pkg-config`, Wayland client headers/library, `wtype`, and `grim` with cursor capture support (`-c`). This input driver is workstation-specific; do not pretend it supports other compositors. It changes no desktop configuration and installs no packages.

1. For a required packaged check, build/package using the root README commands if gameplay source, config or content changed since that package. Do not rebuild/repackage an unchanged artifact just to repeat verification. Do not regenerate Boot unless intentionally changing the arena. Never package while a game instance is using the package; request permission before closing a user's instance.
2. Choose a new desktop evidence directory, distinct from `LOGIC_RUN`; launch refuses any existing directory:

```bash
V=.agents/skills/verify-cooprts/scripts/verify.py
RUN=Saved/Verification/change-desktop-unique
"$V" --run "$RUN" launch
"$V" --run "$RUN" doctor
"$V" --run "$RUN" capture baseline
```

The helper launches `Builds/Linux/CoopRTS/Binaries/Linux/CoopRTS <package path>` windowed at 1600x900 with isolated logs and owned identity. Readiness requires the owned mapped window, the requested map's world-up log, level-up log and UE5.8.3, not rendering proof: inspect a baseline. Fresh Boot matches have HQs but no player squads/buildings. Human control is building/front-only; former squad keys are no-ops.

Launch failures terminate only the process just started. Record the failure, inspect retained logs, and use a new directory after fixing it. A missing/stale package is a prerequisite failure, not a passing check.

## Doctor

```bash
"$V" --run "$RUN" doctor
```

Read-only: verifies recorded PID/start time/executable, source/config/content timestamps versus packaged binary/content, unchanged package size/mtime since launch, engine readiness, and a mapped window belonging to that PID. It prints window geometry and monitor scales. The staleness guard compares only `Source/` `*.h`/`*.cpp`/`*.cs` against the binary and `Config/` `*.ini` plus `Content/` `*.uasset`/`*.umap` against the paks; other paths and suffixes are not detected, and freshness is conservative, not a reproducible-build guarantee. Build from the current checkout before release verification.

Run doctor before driving, after any surprising result, and for every new instance. It cannot detect a logically wedged UI: compare the screenshot with the expected state, then restart in a fresh run if necessary. Never resume sending inputs on the strength of a healthy PID alone.

## Drive

Every input and capture rechecks doctor and requires the owned game to be focused. If another window has focus, the helper refuses rather than typing into it. Run `"$V" --run "$RUN" focus` to explicitly focus the window whose address doctor verified, then retry; never bypass the guard. Do not interact with the desktop concurrently with these commands. Only one driver may control this desktop at a time. Other game instances are never adopted or killed; avoid simultaneous game sessions because Unreal may share saved settings even though evidence logs are isolated.

```bash
"$V" --run "$RUN" key w --hold 350
"$V" --run "$RUN" key space
"$V" --run "$RUN" scroll 3
"$V" --run "$RUN" scroll -3
"$V" --run "$RUN" drag 100 50
"$V" --run "$RUN" click left --x .5 --y .7
"$V" --run "$RUN" click right --x .7 --y .65
"$V" --run "$RUN" key escape
"$V" --run "$RUN" key f4
"$V" --run "$RUN" point --x .7 --y .65
"$V" --run "$RUN" key enter
"$V" --run "$RUN" capture after-transition
```

Click inspected HUD targets to build, choose a force type before first Start, lock/start/pause production, set Secure/Defend/Fall Back fronts or research. Siege's first Start costs 180 once; type stays locked during pause/wipe. Ground clicks commit placement/fronts; right-click/Escape cancels. Accepted placement reopens choices; persistent Construction reopens them even with F4-hidden context. Minimap clicks always pan only, including during targeting. Space focuses selected building or HQ; Enter restarts only after an outcome. Tab/Q/H/R have no human squad bindings. Driver x/y are fractions of the owned logical window, not image pixels: use doctor origin/scale and current captures. Avoid panels/raised obstacles for ground input.

A compositor cursor warp alone did not reliably update Unreal's hit-test position during initial verification. `pointer.c` emits a real motion event after the warp before clicking/dragging/scrolling. It sends discrete wheel ticks separately so Unreal's Started bindings receive each tick. Positive scroll zooms out; negative zooms in.

For cursor checks, use `move -200 0` / `move 200 0` to emit relative motion without repositioning the pointer first. Repeat only while the pointer remains inside the observed window. `click right --here`, `drag 100 50 --here`, and `scroll 3 --here` use the current pointer location without a compositor warp. Their initial 1-pixel delivery motion still applies. Coordinate-targeted actions alone cannot prove that ordinary pointer motion is usable.

## Evidence

Desktop `RUN` contains `session.json` (identity, launch command, package stamps), `actions.jsonl` (timestamped inputs and outcomes), `game.log`, `stdout.log`, and named PNG captures with companion window/monitor JSON. Logic-only runs contain scenario logs, stdout and action results; absence of PNGs is expected. Inspect any images you rely on, not merely their existence. Avoid captures that add no new observation. Full-screen images can contain unrelated desktop content: keep them local, crop for review/sharing, and preserve the original evidence.

Desktop sessions and launch actions, regression actions/results, and network/HUD `run.json` and `events.jsonl` record the selected map so evidence identifies the requested level.

Captures include the native cursor via `grim -c`; companion JSON also records compositor cursor coordinates. Inspect the arrow itself and compare its position across unwarped motion, clicks, and drag release. Cursor coordinates alone cannot prove visibility.

Use `LogArmyOrders` to correlate real input with order type, serial, center and destination. Log acceptance proves submission, not travel, hits or successful disengagement. Use live assertions for exact per-member state. For temporal visual claims, inspect a sufficient interval: absence of one attack flash in a single frame is not evidence of Retreat suppression.

Record a concise `RESULTS.md` in the evidence directory:

- **Change and artifact:** behavior, engine/build used, freshness evidence, and whether this was a new run or inspection of unchanged prior visual evidence.
- **Live assertions:** exact commands, scenario names, exit codes, explicit results and log paths; name the asserted invariants, not a broader feature claim.
- **Input integration:** actions and accepted/rejected state transitions actually exercised; link action/game logs.
- **Visuals:** what was inspected and the relevant capture paths; `not needed` with a reason is valid for a logic-only correction.
- **Failures and gaps:** failed attempts, cause/fix evidence, untested paths, and explicit multiplayer/long-session exclusions.
- **Cleanup:** owned process stopped and evidence retained, or an explicitly requested user-play handoff.

Separate implemented, verified, previously verified unchanged, and unverified. A test author reporting completion is not verification. A fresh reviewer may reuse trustworthy exact results for unchanged paths, but must run the scenario for any correction they make and must not convert assumptions into evidence.

## Cleanup

```bash
"$V" --run "$RUN" stop
```

Run after the last drive and after a failed verification iteration. Cleanup uses the recorded PID identity and a Linux pidfd, not a process name; already-exited instances are left alone. Regression scenarios wait for UE Automation SoftQuit to exit naturally, with no subprocess timeout.

Do not delete the evidence directory. Confirm PNGs, action history and logs remain after cleanup, and the recorded process is absent. The compiled `RUN/pointer` is a disposable build artifact within ignored `Saved/`; it can remain with the proof. Never terminate a user's existing editor/game to clear a verification blocker.

## Helpers and maintenance

Executable entry point: `scripts/verify.py --help`; all invocations above use it. `scripts/pointer.c` is compiled automatically by `launch` against system `wayland-client`; do not run it directly and bypass ownership checks. No Python third-party packages or runtime network downloads are needed.

When controls or behavior change, update the matching feature file and index, then execute the affected proof layer. When maintaining the workflow, compare documented commands and claimed assertions against their implementations. Documentation-only restructuring does not require replaying every gameplay recipe; demonstrate the new selection/evidence process and state which runtime evidence was reused. For executable driver changes, exercise the changed command with the same ownership/focus guards. Classify results as clean, corrected workflow, or blocked/product regression. Report product defects rather than rewriting expectations around them. No automatic commits or PRs.

The workflow design was informed by Cursor pstack's [create-verification-skill](https://github.com/cursor/plugins/blob/main/pstack/skills/create-verification-skill/SKILL.md) and [maintain-verification-skill](https://github.com/cursor/plugins/blob/main/pstack/skills/maintain-verification-skill/SKILL.md). This skill and its helpers are tailored to this repository; the upstream plugin collection is not installed.
