# Engineering setup for parallel agents — decided 2026-10-02

Target setup for building Post-Frontier with many agents working in parallel. The repo is optimised for agents, not for human convenience. The current state is measured in [Audit/](Audit/): [architecture](Audit/architecture.md), [tests](Audit/tests.md), [workflow](Audit/workflow.md).

## Principles

1. **Exactly one way from A to B.** Every outcome (build, test, verify, generate, simulate, package, land) has one command. There is no second path to fall back on, so a workaround is impossible rather than discouraged.
2. **Enforced, not written down.** Rules live in the runner, hooks and lint. A rule that exists only in prose gets broken; today's parallel-agent rules live in `/tmp/cooprts-work` and untracked briefs ([workflow audit](Audit/workflow.md)).
3. **Scoped proof.** An agent working on an isolated task proves it with the smallest test set that covers the files it changed, and doesn't choose that set itself.
4. **Parallel by default.** Two agents on different tasks never block each other's build, tests or files.
5. **No red.** The suite is all green, or the change doesn't land. No quarantine lists, no known failures.

## Decisions

| Area | Decision |
|---|---|
| Baseline | The working tree was committed as-is on 2026-10-02 (`3104bdc`). HEAD had been two days behind (125 modified, 289 untracked files). |
| Entry point | One Python task runner, `./x`, at the repo root. The only documented way to do anything. |
| Fast tests | Automation tests in an editor-hosted `-game` process, start-up trimmed; Low-Level Tests need a source engine build (see [Open](#open)). |
| Unreal processes | Headless automation and Unreal generators lease one pool slot; network verification leases one per Unreal peer, capped at N and acquired together. Packaging, `./x play`, `./x editor` and native/desktop sessions stay exclusive. `./x` owns FIFO admission and both lock modes. |
| Binary assets | `.uasset`/`.umap` are generated outputs. Tuned values and maps live in text. Agents change text only; `./x land` regenerates and commits binaries. |
| Live-editor automation | None. Assets change only through generators from text sources; inspection uses the runner's tests and HUD verification. |
| Command path | One validated command path for humans, JEV, tests and the harness. Test-only RPCs and debug flags leave release builds. |
| Landing | Only through `./x land`: rebase onto main, run the scoped checks, fast-forward. A hook blocks any other commit to main. |
| Red tests | The 9 world tests failing since the regions/Extractors cutover are ported if still relevant, otherwise deleted. |

## `./x`, the only entry point

| Command | Does |
|---|---|
| `./x build` | Builds the editor target for this worktree. |
| `./x test <scope>` | Runs one test scope (see tiers below). |
| `./x check` | Runs exactly the scopes mapped to the files changed against main, plus format and lint. The only proof an agent offers. |
| `./x verify <feature>` | Runs a feature's slow verification (network, HUD capture, native window). |
| `./x gen <asset>` | Runs one generator. Unreal generators lease one headless slot; non-Unreal generators take no Unreal lock. |
| `./x sim` | Runs the balance harness. |
| `./x package` | Builds a package into a run-specific folder, never over the friends' playtest build. |
| `./x land` | Rebases, runs `check`, regenerates binary assets from text if their sources changed, fast-forwards main. |
| `./x help` | The procedure reference. Docs link here instead of repeating commands. |

The runner owns:
- **Locks:** the headless process pool and exclusive desktop lock admit updated runner requests first-come, first-served using ordered, flock-protected tickets; external flock users and older runners bypass the queue. Dead waiters and holders cannot block the queue. Stale editor builds take their worktree module writer lock before the global one-at-a-time build lock and pool admission; waiting builds hold no pool slot and module waiters hold no global build lock. Fresh editor checks take no pool slot. Automation builds once up front, then runs up to pool-size scopes at once; each scope leases and releases its own slot, and a worker requests its next slot only after finishing a scope, so earlier waiters from other runs go first. Python and script scopes take no Unreal lock. Unreal generators use one slot; network verification acquires one slot per peer together, capped at the configured pool size. Packaging, play, editor and native/desktop sessions remain exclusive. Every worktree reads its pool size from [settings.toml](../../Tools/x/settings.toml).
- **Freshness:** content hashes of sources, not file timestamps. A per-worktree module readers/writer lock prevents builds from replacing a module used by automation, Unreal generators or editor-mode verification; input/module changes during a scope still fail that scope.
- **Evidence:** every run writes a machine-readable record (command, commit, scopes, results, timings, log paths) into a per-run folder. Source identities and retained dirty deltas distinguish equivalent contents from changed or unknown inputs across run, scope and execution boundaries; build/automation entries also retain their guarded editor-input hashes. Matching source is not a passing result or proof of every tier; legacy provenance remains unknown. Inspection and comparison semantics live in `./x help runs`. Every automation lease includes its scope-labelled wait in `lock_waits`, even without contention. Agents cite run IDs; nobody hand-writes RESULTS.md.
- **The engine path, maps list and ports,** each defined once.

## Test tiers

| Tier | What | Speed | Lock |
|---|---|---|---|
| 0 | Pure logic in editor-hosted `-game` automation; Python tool tests (map validators without rendering) | Rules: see phase 2 exit check; Python: milliseconds to seconds | Rules: headless pool; Python: none |
| 1 | Headless world scenarios, one feature tag per scope, on an uncapped 1/60 s fixed step: stage waits use the world clock, failure deadlines wall time | 4–8 s each, mostly engine start-up (run `20261003-133918-test-bdaa`) | Headless pool |
| 2 | Network slices, HUD capture, simulation, packaging | Minutes | Exclusive or pool, per command |

- **A path-to-scope map** in the repo lists which scopes cover which paths, e.g. `Source/CoopRTS/Rules/**` → `rules`, `Build/Maps/**` → `maps`. `./x check` reads it, and lint fails if a source file maps to no scope.
- Every new behaviour lands with a tier-0 test if its logic can be pure, otherwise a tier-1 scenario.

## Parallel work

- **One branch per task, one git worktree per worker slot.** A slot's worktree is reused for its next task once the previous one has landed, so its `Binaries/` and `Intermediate/` stay warm; a full non-unity module build in a fresh worktree takes ~30 s (run `20261002-103603-build-c862`). The derived-data cache is shared. Roles, panes and the task loop are in the [orchestrate skill](../../.agents/skills/orchestrate/SKILL.md).
- **Unity builds are off** for the game module (done in phase 1: `CoopRTS.Build.cs` sets `bUseUnity = false`). Each `.cpp` compiles separately, so same-named helpers in different files no longer collide ([architecture audit](Audit/architecture.md)).
- **Generated binaries never conflict:** agents don't commit them; each worktree's generators write only its own assets using a headless slot, and `land` regenerates before committing.
- **Feature folders:** code, tests and the scope entry for a feature live together, so a task touches one folder plus the shared interfaces.

## Architecture targets

From the [architecture audit](Audit/architecture.md):
- **One command path:** a validated command layer that humans (through RPCs), JEV, tests and the harness all call. It replaces the three paths per command and the test-only `ServerIssueOrder`/`ServerIssueAttack`.
- **Pure decision logic:** the ~850 lines of extractable logic listed in the audit move into world-free functions with tier-0 tests. Today only ~6% of gameplay logic is world-free.
- **Roster-derived force ceiling:** `ForceCap::Limit` is shared by production admission and force-number selection. World readers supply the filtered human commander count; occupancy, number reservation and camera behaviour keep their existing owners. Mechanics: [forces.md](../Design/forces.md).
- **Split the god objects by feature:**
  - `ACommandGameState` into registry, economy, territory and placement pieces;
  - the 13 RPCs out of `ACommandPlayerController` into per-feature command components;
  - `CommandHUD.cpp` into one file per panel;
  - `AEnemyCommander` into a pure planner plus an executor that uses the command layer.
- **Content as text:** one text source for unit stats (today two scripts carry the table, and tuned values live only in binary assets) and one source for gameplay constants (the map JSON has drifted from C++: baseline income 10 vs 2).
- **Release builds contain no test hooks:** the `-autopilot`/`-Sim*` flags, the income-pause test flag and network-probe fixtures sit behind dev-only guards.

## Enforcement — decided 2026-10-02

**Git hooks:** every `./x` invocation installs the shared absolute `Tools/hooks` path and refuses hooks whose bytes differ from main or are not executable. Main moves only with a one-time exact-tip grant from a live landing runner holding the landing lock; deletion and recreation are blocked, while ref packing, no-op updates, other refs and Git LFS continue to work. Git's committed transaction hook durably appends each landed range and anchors the entry count and last landed hash in a dedicated ref before the merge returns, independently of the landing runner. `./x check` and `./x land` audit main's first-parent history against that seeded ledger and its anchor with one history traversal, blocking unrecorded moves, ledger deletion, truncation or disagreement: stop and ask the owner. Check never seeds or rewrites evidence; successful audits do not wait for the landing lock, and inconsistent snapshots retry under it. Only land seeds under the lock after checks pass, immediately before merging; seed-only evidence without an anchor has no authority and is replaced at the current main tip. Landing requires a clean main worktree and restores it after a rejected fast-forward when main's ref is unchanged. Deliberate same-user forgery remains possible, including deleting both ledger and anchor together; only a separate OS user owning main closes that limit. The binary-commit rule switches on in phase 4 together with binary regeneration at landing.

### Lint policy

Strict in enforcement, careful in which rules it carries: checks that people see as noisy get ignored (Google's Tricorder work kept effective false positives under about 10%), and agents go further and work around them.

- **Every finding blocks.** There is no warning level and no "fix later".
- **No inline suppressions.** `NOLINT`, `noqa`, `type: ignore`, `#if 0` and disabled or skipped tests are themselves lint errors.
- **One central exceptions file.** When a rule is genuinely wrong somewhere, the exception is listed there with the file, the rule and a reason. Every exception is visible in one place.
- **No grandfathered violations.** A rule is switched on only where the code already passes. Turning a rule on fixes every existing violation in the same change, or covers one folder at a time with each folder fully clean.
- **Fast and on changed files only,** inside `./x check`.
- **Auto-fix where possible:** formatting is applied, never argued about.

### Rules, most valuable first

1. **Workaround detectors:**
   - inline suppressions, disabled or skipped tests;
   - test-only symbols outside dev-only guards;
   - direct engine or UBT invocations outside the runner;
   - edits to generated files outside `land`;
   - `TODO`, `FIXME`, `HACK` and stub markers;
   - docs citing paths under the untracked `Saved` directory;
   - procedure text duplicated outside `./x help`, `AGENTS.md` and the orchestrate skill.
2. **Architecture rules:** which layer may include or call which.
   - Pure rules code includes no actors.
   - Only the command layer calls network commands.
   - The HUD reads game state but never changes it.
   - Every source file sits in the path-to-scope map.
3. **Size limits:** files at most **500 lines**, functions at most **60 lines**, to stop new god objects. They switch on folder by folder as each folder becomes clean. Measured 2026-10-02 for files:
   - `Source/CoopRTS/Rules/` and `Source/CoopRTS/Content/` already pass; they switch on in phase 1, once function lengths are checked.
   - `Tools/harness/**` switches on in phase 2 together with the harness tests.
   - `Build/` has 10 files over 500 lines (mostly art and map generators); it switches on in phase 4.
   - The rest of `Source/CoopRTS/` has 8 files over 500 lines, including `CommandHUD.cpp` at 1,575; it switches on in phase 5, after the splits.
4. **Formatting:** clang-format for C++ and a Python formatter, applied automatically. Uniform formatting also cuts merge conflicts between agents.
5. **C++ static analysis:** a curated set of clang-tidy bug and performance checks, only ones that fire correctly on Unreal code (its macros make a full run noisy). Runs on changed files using UBT's compile database and grows one check at a time.
6. **Python:** a strict linter with auto-fix and strict type checking, switched on by the same folder-by-folder rule. `./x` and the plain-Python tools (map validators, the harness) start strict in phase 1. Generators that import Unreal's or Blender's Python modules follow in phase 4, once type stubs for those modules are set up.

## Migration plan

Each phase ends with `./x check` green and its exit criteria met. Phase 2's timing criterion is separate run-record evidence, not a statistic produced by `./x check`.

| Phase | Work | Exit check |
|---|---|---|
| 0 | Baseline commit | Done: `3104bdc` |
| 1 | Unity builds off for the game module (done); `./x` wrapping today's scripts; locks, evidence records and hash freshness; hooks; the lint policy with workaround detectors, architecture rules, formatting and Python lint and types; size limits for `Rules/` and `Content/`; the path-to-scope map; delete duplicate procedure text from README, the skill and feature docs; move still-valid rules from `/tmp/cooprts-work` into the repo | Done: `a9c8669`. Every documented procedure is a `./x` command; a direct commit to main is rejected; lint is green |
| 2 | Green suite: port or delete the 9 red tests; trim editor-hosted automation start-up; Python tests for map validators | Done: `d8e25fb`. `./x check` is green; in five consecutive `./x test rules` records with editor inputs unchanged and the editor already built (no build execution), the median `execs[].duration_s` for the entry whose `log` ends in `/rules-stdout.log` is under 5 s. This measures process wall time, excluding build and lock wait. |
| 3 | One command path; JEV through it; test hooks out of release builds | Done. [Commands/](../../Source/CoopRTS/Commands/) supplies the real authoritative path for humans, JEV, tests and the probe; test-only RPCs are deleted. No test-only RPC or flag in a release build; simulation, automation and verification hooks use `WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING`, and `test-only-symbol` lint enforces guarded uses throughout `Source/`. |
| 4 | Content as text; deterministic generators, split to the size limits; `land` regenerates binaries; size limits switch on for `Build/` | Changing a unit stat is a text-only diff |
| 5 | Split the god objects; extract pure decision logic; size limits switch on for the rest of `Source/CoopRTS/`; clang-tidy check set grows with each split. HUD panel split complete: `CommandHUD.*` and `HUD/` enforce the 500-line file and 60-line function limits; other splits remain | Hotspots from the audit no longer need edits for unrelated features |

Gameplay work (build step 1a in [Design/build-order.md](../Design/build-order.md)) can start after phase 1. Each later phase can run alongside gameplay work, as long as the two don't touch the same files.

**Evidence history (resolved):** the owner's existing untracked verification archive stays on disk; docs no longer cite it. Run records under the runs root are the evidence from now on; see `./x help runs`.

## Open

- **N for the headless pool (resolved 2026-10-02):** N = 2 on this 30.40 GiB host. World scopes peaked at 2.267 GiB per editor; a fresh build's UBT/compiler tree peaked at 5.825 GiB.
  Cross-worktree N = 2/3/4 trials passed, with minimum `MemAvailable` 12.00/9.99/8.57 GiB. Observed total concurrent editors were 2/3/4 (owned trial editors 2/2/3; other workers occupied remaining slots). Two subsequent N = 2 batches passed all five scopes with two owned worlds overlapping in each.
  Fifteen agents plus helpers peaked at 11.236 GiB (12-agent projection 8.989 GiB); desktop/other processes peaked at 2.541 GiB. Budget 9.5 GiB for twelve agents/helpers, 2.75 GiB for the desktop, 3.5 GiB for the owner's editor (not running during measurement; HUD's editor measured 2.576 GiB), and 6 GiB free.
  With a 6 GiB build slot and 2.6 GiB headless slots (max of the 2.267 GiB world and 2.576 GiB HUD editor peaks), `N = 1 + floor((30.40 - 9.5 - 2.75 - 3.5 - 6 - 6) / 2.6) = 2`; N = 3 would exceed that mixed-build budget. Concurrent unguarded UBT failed in shared `Trace.uba` log rotation, so builds serialize. Simulation memory is unmeasured and not covered by this sizing evidence.
  Exec records sample the union of the process group and its descendants every 250 ms as `peak_rss_mb` (MiB), counting each PID once and including children that start other sessions. Historical UBT/HUD records measured only launcher groups; their sizing above used separately sampled process-tree/editor RSS.
- **Low-Level Tests on UE 5.8/Linux (resolved 2026-10-02):** the installed 5.8.3 engine cannot build LLT programs. An explicit `TestTargetRules` / `TestModuleRules` Refund prototype failed before C++ compilation: the default environment was rejected with “Targets with a unique build environment cannot be built with an installed engine.” Selecting the shared environment then failed with “Program targets are not currently supported from this engine distribution.” Run records `20261002-135836-test-8a36` and `20261002-140237-test-9be9` retain both UBT logs. A source engine build, or a separately produced distribution supporting Linux Program targets and the LLT dependencies, would be required to revisit LLT; neither has been verified. The owner chose editor-hosted automation instead; no no-editor cutover is planned. No executable was produced, so clean/incremental build and test-run timings remain unmeasured.
- **Asset regeneration at landing:** generators take minutes, so batch them, and decide whether `land` blocks on regeneration or a follow-up commit does it.
