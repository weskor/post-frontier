---
name: verify-cooprts
description: Verify CoopRTS Unreal Engine gameplay on Linux with live state assertions first, targeted packaged-input and visual checks, build freshness, and explicit evidence limits. Use for gameplay changes or verification workflow maintenance.
---

# Verify CoopRTS

Use live Unreal state assertions for gameplay rules; use the packaged game for input integration and observable presentation. Screenshots are visual evidence, not the primary gameplay test. Neither compilation, accepted-order logs, nor an automation-only pass proves every layer. Never change product behavior or weaken assertions to make verification pass.

This is a project-local OMP/Agent Skills skill, not a Cursor plugin. Discovery occurs at session startup; after adding it, a fresh session can use `/skill:verify-cooprts`. Within an existing session, read this file directly. Commands below run from the repository root. Read `features/README.md`, then the files for the affected features. For a complete baseline, cover every mapped feature.

## Choose the proof before running

1. Name the changed behavior and its failure condition. Read the relevant source and [feature map](features/README.md); select the scenarios and surface checks that actually exercise it.
2. Run live assertions first for damage, targeting, pursuit, order transitions, navigation, paid casualty replacement, capture, income and per-unit arrival. These worlds use real actors/AI/navmesh, not mocks. A standalone authoritative world does not prove client/server replication or hostile-client validation.
3. Exercise real packaged input when bindings, hit testing, selection, cursor handling or input-to-order integration changed. Correlate input attempts with the resulting order/state, not just key delivery.
4. Inspect rendering when camera behavior, HUD, cursor, assets or effects changed. Use the smallest set of captures that proves the visual transition; a before/after pair is often enough. Temporal visuals may need a short sequence. Numeric gameplay invariants belong in assertions, not screenshot counting.
5. For a new gameplay feature, include a short packaged end-to-end encounter as well as its assertions. For a targeted correction, do not repeat every baseline recipe. A full milestone/release baseline covers all mapped features.

| Changed path | Primary live scenario | Additional surface proof |
| --- | --- | --- |
| Combat rules, role stats, target lifetime or pursuit | `combat` | New encounter: attack input plus visible combat feedback; later rule-only changes: target any unproven integration edge |
| Move/Hold/Retreat order replacement or validation | `orders`; add `combat` when combat precedence changes | Changed bindings/feedback: one relevant input transition |
| Enemy planner, commitment or paid recovery | `strategy` | Packaged encounter: watch enemy move to a site, inspect changing ownership, plan and rationale, retreat/recovery and HQ defense if reached |
| HQ damage, outcome, terminal commands or restart | `match-win` and `match-loss`; add `strategy` if planner changed | Fresh packaged encounter: target enemy HQ with Q; inspect HQ health/outcome, blocked commands and Enter reset |
| Formation paths, crowd movement, boundary rejection or joining units | `movement`; add `orders` if cancellation changes | Changed controls/markers/arena: relevant route or joining presentation, not a second exhaustive assertion pass |
| Capture, wallet income, purchase eligibility, casualty restoration or wiped-army rebuild | `economy`; add `movement` for joining-path changes | Packaged capture and N purchase/rebuild with wallet, quote, eligibility and feedback visible |
| Controller input, selection, HUD or cursor | Matching `orders`/`combat`/`economy` scenario if semantics changed | Actual affected input and its visible result; use unwarped motion for cursor defects |
| Camera or visual-only assets/effects | No existing numeric camera/rendering scenario | Native input and inspected rendering; do not claim numeric limits without measuring them |
| Doctrine choice, owned effect scope, healing or stationary protection | `doctrine-siege`, `doctrine-repairs`, `doctrine-frontline`, `doctrine-restart`; fresh Boot per choice | Fresh package: inspect F1/F2/F3 or cards, locked choice, and a short affected combat/recovery encounter; see [doctrines](features/doctrines.md) |
| Real multiplayer ownership, joining, economy, outcomes or travel | [Real network recipe](features/multiplayer.md): editor host+remote, packaged host+remote, packaged host+four, then active UE packet emulation | Distinct native host/client windows, local-focused selection/order/camera/HUD and five-player roster view; human five-player coordination/deathball remains unverified |
| Verification docs only | Check commands and assertions against their implementations | No game rebuild or repeated desktop run just for prose; demonstrate the revised workflow on applicable runtime evidence |

Select each required scenario once, even if several features map to it. The full recipe lists below are probe menus, not a requirement to capture every step for every change. Existing visual evidence may be reused only when the relevant packaged build and visual/input path are unchanged; cite its run and explain the exclusion. Never reuse old runtime evidence as proof of changed gameplay.

## Live assertions first

No packaged launch, compositor access or screenshot is needed for these scenarios. Use the installed UE engine and a fresh `CoopRTSEditor` module. Build with the root README command after C++/test changes. `regression` rejects a missing module or one older than any `Source/` file or the project descriptor, records its size/mtime in `actions.jsonl`, and rejects module changes during the run. This conservative timestamp guard is not a reproducible-build guarantee; it does not track engine/plugin/toolchain changes. Package timestamps are not editor-build proof.

```bash
V=.agents/skills/verify-cooprts/scripts/verify.py
LOGIC_RUN=Saved/Verification/change-logic-unique
"$V" --run "$LOGIC_RUN" regression --scenario combat
```

Replace the directory suffix with a fresh identifier and choose the scenario from the table, not always `combat`. `regression` defaults to `orders`. `movement` runs `CoopRTS.Movement.TwoGroups`; `combat` runs `CoopRTS.Combat.Encounter`; `economy` runs `CoopRTS.Economy.CaptureIncomeRecovery`; `orders` runs `CoopRTS.Orders.ReplaceHoldRetreat`; `strategy` runs `CoopRTS.Enemy.StrategicDecisions`; `match-win` and `match-loss` run `CoopRTS.Match.VictoryRestart` and `CoopRTS.Match.DefeatRestart`. The `doctrine-siege`, `doctrine-repairs`, `doctrine-frontline`, `doctrine-restart` scenarios run `CoopRTS.Doctrine.SiegeOptics`, `CoopRTS.Doctrine.FieldRepairs`, `CoopRTS.Doctrine.EntrenchedFrontline`, `CoopRTS.Doctrine.Restart`, respectively. Each command starts its own fresh standalone Boot world. Serialize these with builds and other game instances; never mutate binaries/assets while a check is using them.

Require process exit zero AND the exact scenario's `Test Completed. Result={Success}`. The four doctrine scenarios additionally require `**** TEST COMPLETE. EXIT CODE: 0 ****` from UE `Automation SoftQuit`, rather than `-seconds` or a subprocess timeout; the older scenarios retain their existing deadline guards. Preserve the matching log, stdout and `regression-result` action record. A timed exit, an unrelated test's success, or `Attack accepted` is not a pass. Failures remain failures: diagnose the assertion and state, fix the cause, then run in a new evidence directory. Do not extend old deadlines or loosen tolerances merely to get green.

Coverage is limited to assertions actually present in the test. For example, `movement` checks every member's path success, distance and velocity; `orders` checks return of the group center, not every member's home arrival. There is no general packaged-world state-query API in this helper. An editor MCP or debugger could aid diagnosis, but neither is installed or required by this workflow, and editor observations are not automatically packaged-game evidence.

## Real multiplayer sockets (milestone 8)

Standalone automation cannot prove client ownership, replicated wallets or travel. Build the fresh editor target after all source handoffs, package only after the editor host+remote run passes, then use the [multiplayer recipe](features/multiplayer.md). Each run starts separate game processes, listens on an IPv4 loopback socket, drives the existing owning-controller Server RPC from an actual remote client world, and independently reads each world's replicated actors. Nothing in this probe runs without explicit per-process Development command-line opt-in; only the host gets a separate authority-fixture switch. No source changes or package mutation during a run. Do not enable fixtures on public servers.

```bash
N=.agents/skills/verify-cooprts/scripts/network.py
"$N" --run Saved/Verification/m8-editor-two-unique --mode editor --clients 1 --max-fps 60
"$N" --run Saved/Verification/m8-package-two-unique --mode packaged --clients 1 --max-fps 60
"$N" --run Saved/Verification/m8-package-five-unique --mode packaged --clients 4 --max-fps 60
"$N" --run Saved/Verification/m8-package-five-loss-unique --mode packaged --clients 4 --emulation --max-fps 60
```

Require the runner's PASS record, native net-mode/peer identity, per-peer `responses.jsonl` and game logs, observed replicated convergence, and pidfd cleanup. The emulated run must read actual net-driver `PktLag=120` and `PktLoss=8` from every world. Network waits have no wall-clock deadline: the runner prints the pending predicate, elapsed time, latest peer state/connection health, and fails on terminal network/travel failures, contradictory states, process exit or artifact mutation. Positional wait numbers are reporting intervals, never success/failure deadlines. An unstarted packaged `-nullrhi` session is a blocker; report it, do not infer success. Separate native-window visual/input proof uses `network_desktop.py` from the recipe and focused per-peer `doctor`/`focus`/`capture`/`key`/`click`. The original `verify.py` remains single-instance. Numeric snapshots do **not** prove readable five-player teamplay or absence of a deathball; require a human five-player session for that criterion.

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

The helper invokes `Builds/Linux/CoopRTS/Binaries/Linux/CoopRTS CoopRTS` directly with windowed 1600x900, stdout and an absolute per-run log. It compiles the bundled pointer helper into the evidence directory. Readiness requires the owned process, one mapped window, `Bringing up level for play` and UE `5.8.3` in that run's log. Readiness does NOT prove rendering: inspect the baseline. For a full baseline, use 1 / 2 and Space to confirm both six-unit groups and the command HUD; for a targeted check, establish only the required starting state.

Launch failures terminate only the process just started. Record the failure, inspect retained logs, and use a new directory after fixing it. A missing/stale package is a prerequisite failure, not a passing check.

## Doctor

```bash
"$V" --run "$RUN" doctor
```

Read-only: verifies recorded PID/start time/executable, source/config/content timestamps versus packaged binary/content, unchanged package size/mtime since launch, engine readiness, and a mapped window belonging to that PID. It prints window geometry and monitor scales. Timestamp freshness is conservative, not a reproducible-build guarantee; build from the current checkout before release verification.

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
"$V" --run "$RUN" key 1
"$V" --run "$RUN" key 2
"$V" --run "$RUN" click right --x .7 --y .65
"$V" --run "$RUN" point --x .7 --y .65
"$V" --run "$RUN" key q
"$V" --run "$RUN" key h
"$V" --run "$RUN" key r
"$V" --run "$RUN" key n
"$V" --run "$RUN" key f1
"$V" --run "$RUN" key f2
"$V" --run "$RUN" key f3
"$V" --run "$RUN" key enter
"$V" --run "$RUN" capture after-retreat
```

These illustrate supported controls, not a blind test sequence. F1/F2/F3 choose one doctrine per ongoing match and cannot all be accepted in one run; use separate fresh sessions for distinct choices. Q uses the live cursor hit: point at an observed enemy unit or HQ for targeted Attack, or reachable ground for attack-location; A remains camera pan-left. Enter requests restart only after Victory or Defeat. `point` delivers real pointer motion without issuing a click or zoom, so selection is preserved. Select targets from the current screenshot using the feature recipes. `--x`/`--y` are fractions of the owned window's logical rectangle; they are not screenshot pixels. The helper computes compositor coordinates from current geometry, then emits real Wayland pointer events. For a screenshot point, convert physical pixels to compositor logical coordinates using the monitor origin/scale from doctor, then to window fractions. On this workstation scale is often 1.5; do not hardcode it. Avoid the HUD, obstacle tops, and unit bodies when requesting ground movement. Coordinates must remain between .05 and .95; drag from near the center so its end stays inside the window.

A compositor cursor warp alone did not reliably update Unreal's hit-test position during initial verification. `pointer.c` emits a real motion event after the warp before clicking/dragging/scrolling. It sends discrete wheel ticks separately so Unreal's Started bindings receive each tick. Positive scroll zooms out; negative zooms in.

For cursor checks, use `move -200 0` / `move 200 0` to emit relative motion without repositioning the pointer first. Repeat only while the pointer remains inside the observed window. `click right --here`, `drag 100 50 --here`, and `scroll 3 --here` use the current pointer location without a compositor warp. Their initial 1-pixel delivery motion still applies. Coordinate-targeted actions alone cannot prove that ordinary pointer motion is usable.

## Evidence

Desktop `RUN` contains `session.json` (identity, launch command, package stamps), `actions.jsonl` (timestamped inputs and outcomes), `game.log`, `stdout.log`, and named PNG captures with companion window/monitor JSON. Logic-only runs contain scenario logs, stdout and action results; absence of PNGs is expected. Inspect any images you rely on, not merely their existence. Avoid captures that add no new observation. Full-screen images can contain unrelated desktop content: keep them local, crop for review/sharing, and preserve the original evidence.

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

Run after the last drive and after a failed verification iteration. Cleanup uses the recorded PID identity and a Linux pidfd, not a process name; it sends TERM, then KILL only if that same owned process will not stop. Already-exited instances are left alone. The old `regression` scenarios retain subprocess deadlines; the new doctrine scenarios wait for UE Automation SoftQuit to exit naturally, with no subprocess timeout.

Do not delete the evidence directory. Confirm PNGs, action history and logs remain after cleanup, and the recorded process is absent. The compiled `RUN/pointer` is a disposable build artifact within ignored `Saved/`; it can remain with the proof. Never terminate a user's existing editor/game to clear a verification blocker.

## Helpers and maintenance

Executable entry point: `scripts/verify.py --help`; all invocations above use it. `scripts/pointer.c` is compiled automatically by `launch` against system `wayland-client`; do not run it directly and bypass ownership checks. No Python third-party packages or runtime network downloads are needed.

When controls or behavior change, update the matching feature file and index, then execute the affected proof layer. When maintaining the workflow, compare documented commands and claimed assertions against their implementations. Documentation-only restructuring does not require replaying every gameplay recipe; demonstrate the new selection/evidence process and state which runtime evidence was reused. For executable driver changes, exercise the changed command with the same ownership/focus guards. Classify results as clean, corrected workflow, or blocked/product regression. Report product defects rather than rewriting expectations around them. No automatic commits or PRs.

The workflow design was informed by Cursor pstack's [create-verification-skill](https://github.com/cursor/plugins/blob/main/pstack/skills/create-verification-skill/SKILL.md) and [maintain-verification-skill](https://github.com/cursor/plugins/blob/main/pstack/skills/maintain-verification-skill/SKILL.md). This skill and its helpers are tailored to this repository; the upstream plugin collection is not installed.
