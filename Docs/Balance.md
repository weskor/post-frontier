# Balance simulation

## Method

The default whole-match mode of `./x sim` ([`./x help sim`](../x)) runs one fresh authoritative standalone Unreal process per match. Team 5 uses the ordinary `AEnemyCommander`; an explicitly opted-in `FMatchSimulation` spawns the same class with `TeamIndex=0` and the local `ACommandPlayerState` as `Commander`. Human-side autopilot exists only in opted-in standalone worlds; economy overrides are similarly isolated. No fixtures grant money, armies, capture or damage in whole-match mode.

The runner owns process launch, headless scheduling, freshness and cleanup. Editor standalone and packaged Development matches both require the requested map to start; a wrong/unstarted map is a failure, not a result. Runtime artifact mutation is rejected.

### Planner under measurement

Both sides evaluate every **2 game seconds**, pay through the same placement/production APIs, and use `SetGoal`, never planner-supplied location fronts. Composition starts Frontline, then Ranged, then Siege; the planner maintains at most **3 barracks**. Optional buildings, extractors and research retain **120 Power** (one complete infantry force) as a replacement reserve.

Expansion excludes unreachable regions and enemy mains, splits forces across distinct targets, and scores each candidate as `8 + 2 × free deposit rate − 5 × graph hops − 4 × hostile units − distance² / 4000² − 3 if enemy-controlled + 4 if already targeted`. Distance is force-center to region-anchor in centimetres. Nearest available forces Hold threatened controlled regions. Assault requires at least **6 allied living units**, at least **1.25×** opposing living strength, and income no lower than the opposing team's total; exhaustion of expansion targets also leads to Assault. The goal driver retains its own casualty-refill behavior without identical Assault goals resetting it.

Forward barracks require a controlled, uncontested non-main region without an owned producer; the nearest eligible anchor to the opposing HQ is preferred. Additional barracks wait for forward territory and at least one paying extractor. Extractors prefer rate and proximity, require a free nonempty deposit in safe controlled territory, and wait for the first allied recruit. Construction searches mirrored **32 directions × 9 rings**, with radii **380 + 160 × ring cm**, through normal grid, collision, navigation and footprint validation.

Construction preserves free-deposit space and existing full-force rally footprints: candidate centres must remain at least **`sqrt(2) × building footprint radius + 200 cm`** away. The 200 cm covers the six-unit formation extent and navigation-agent margin. A rejected preferred extractor placement does not deadlock investment; other eligible deposits are attempted through the normal placement API.

Average joined-unit health below **35%** requests only that producer's Fall Back goal; recovery persists until average joined health reaches **80%**. Fall Back targets the force's validated assembly point through the goal driver, not a fixed HQ offset.


### Simulation and tuning

Simulation batches, single matches, custom economy variants, wider matrices, paired dilation comparisons and report regeneration all use `./x sim`. Arguments and launch procedures live only in [`./x help sim`](../x).

The baseline3 and baseline4 variants change only baseline income; custom economy variants need no rebuild because their values apply at runtime.

The measured batch on **2026-10-01** was V2 only: **ten baseline-2/s matches and ten baseline-3/s matches**, seeds **1–10**, fixed-step **1×**, starting wallets **600/600**, normal deposits **4/s, 2400 total**, rich deposits **6/s, 3000 total**, and a **2400-game-second** cap. Classic-map matches, baseline 4/s, higher-dilation comparisons, extra matrices, packaging, network and presentation checks were explicitly skipped.

Higher-dilation equivalence has **not** been established. A paired sample without paid production/combat on both sides, HQ damage and capture is inconclusive, not a passing equivalence check. Until equivalence is established, the measured 1× conditions remain the tuning reference.

Runtime numeric bounds are baseline/rates **0–10000**, amounts **0–100000000**, seed **0–2147483647**, cap **1–86400 s**, requested dilation **1–32** (effective engine clamping is rejected as a request mismatch). The runtime requires an absolute output path; the runner owns it. Defaults are unchanged without opt-in.

### Time, determinism and dilation

The world uses a **fixed 1/60 second game step**. `FApp::SetUseFixedTimeStep(true)` and `SetFixedDeltaTime(1/(60 × effective dilation))` scale the virtual undilated step inversely to world dilation; frame clamps preserve that game step. This avoids coarse actor deltas silently changing single-action-per-tick combat and production. Headless frames are uncapped and run as fast as the CPU/navigation allows at **both** 1× and higher dilation. Larger dilation does not reduce the number of gameplay frames or promise a wall-clock speedup; `wall_duration` records real `FPlatformTime` elapsed seconds rather than the engine's virtual clock.

The runner rejects a measured game delta above `1/60 + 0.0001 s`. Paired comparisons inspect shared 30-second samples **and terminal states**: wallets, income, producers/extractors, living units by role, observed production/casualties/attacks/health loss, HQ health, territorial control and concentration. Defaults allow **5% relative +1 absolute** numeric difference (share tolerance 0.02), require identical region-control transition sequences, and first construction/capture event times within **2 game seconds**. `summary.json` records metric deltas and every mismatch; the process returns nonzero for failed or inconclusive comparison.

Seeds initialize UE's global `Rand/FRand` and `SRand` streams before gameplay. Engine asynchronous navigation, actor tick ordering and floating-point work remain possible nondeterminism sources. Team 5's planner is spawned before team 0's, and this fact is recorded. The deterministic planner may not use randomness at all: ten equal seeded runs are not automatically ten independent statistical samples. Do not claim reproducibility merely from matching seed numbers.

### Telemetry API and artifacts

`FMatchSimulation` observes existing real-game APIs: `ACommandGameState::GetIncomePerSecond`, region/controller queries, `ACommandBuilding` production/goal state, living `AArmyUnit` health/role/attack counters and `AArmyGroup` centers. `FSimulationSettings::ForWorld` supplies the actual baseline payments and deposit rate/reserve initialization; it is not a reporting-only override. Economy still pays through the existing two-second finite-extraction policy.

Each match has `launch.json` (job, command, artifact identity, return code and harness status), `stdout.log`, `game.log`, and atomically replaced `match.json` checkpoints. Schema version **1** includes:

- Map, seed, engine version/command line, requested/effective dilation, fixed-step metadata, cap, actual duration, wall duration, outcome and winning team (`0`, `5`, or null).
- Snapshots at game-time zero, every **30 s**, and termination. `time` is the actual observation time; `scheduled_time` is the sampling boundary. Past states are never manufactured for skipped frames.
- Per team: `wallet`, `income_per_second`, all living `extractors`/`barracks` (including construction), `completed_extractors`, `deposits_remaining` in currently controlled regions, `units_alive`, `units_reinforcing`, `units_by_role` (Frontline/Ranged/Siege), controlled region count/indices, HQ health, cumulative observed births/casualties/attacks/unit health loss, units per region, largest-region unit share, building production/goal details, and force identities/strength/centers/regions.
- Individual deposit reserves, controlling region team and occupying extractor team at each snapshot. Occupancy is not the same as regional ownership.
- First placed/completed extractor and barracks, first capture, every region-control transition, observed HQ damage, deposit depletion, and start/finish events. Placement and completion are distinct timings.
- Static HQ locations, region polygons/roles/neighbour graph, deposit positions/rates/initial amounts, unit production and combat definitions, initial wallets, planner class and spawn ordering.

The batch writes `run.json`, `summary.json`, `Report.md`, and four PNGs: income, living units, concentration and duration. Only successful, validated, natural-exit matches enter aggregation. Win percentages use all complete matches as denominator, with draws reported separately. Duration includes censored draws; compare decisive-match distributions separately when recommending the **12–18 minute** pacing target. Curves average only matches still alive at a scheduled sample, so later points have survivorship bias.

### Outcome and failure discipline

A natural HQ destruction wins; simultaneous HQ loss follows the game's team-5 precedence. A cap reached with **both HQs alive** is a draw. The runtime flushes a terminal snapshot/status before requesting a **non-forced natural exit** with `RequestExitWithStatus(false, ...)`. A persisted complete JSON **and exit code zero**, correct requested map/seed/economy/dilation, full 30-second history, and a consistent HQ/cap outcome are all required.

Missing output, crash/nonzero exit, invalid world, write failure, interrupted world/run, wrong metadata, changed binary or stalled progress are **failures**, never draws. The default runner watchdog fails after **180 wall seconds without persisted game-time advancement**; it neither produces a winner nor modifies game time. SIGINT/SIGTERM cleans up only the private process session started by this runner, while retaining the shared lock until that process stops. Failed/interrupted attempts and unattempted planned jobs remain visible, and the batch returns nonzero.

### Symmetry, depletion and deathball interpretation

V2 is **explicitly asymmetric** in `Build/Maps/AvailabilityZoneV2.json`: the drawn outlines, blockers, deposits and seven tactical regions do not admit 180° rotation. HQ coordinates are approximately **(-6807, -6912)** for team 0 and **(8281, 7825)** for team 5. Straight-line HQ-to-near-expansion-anchor distances are about **5110 cm** versus **4161 cm**, before obstacles/navigation. Equal side win rates are not a justified null expectation for this layout. The report computes nearest-natural distances and reward-region BFS hops from the recorded real layout; use polygons, deposit positions and planner spawn ordering to distinguish travel/economy advantages from an AI bug. These are layout measurements, not match results.

Use `deposit_depleted` times and individual reserve histories to identify exhaustion. Summed controlled-region reserves can rise/fall on capture even without extraction. Report the fraction of matches reaching depletion alongside conditional timing; absence of depletion is not a zero-minute measurement.

Largest-region unit share is summarized only for snapshots with **at least 12 living units**. Force centers, goals, roles, production and regional counts support inspection of concentrated advances versus split pressure. A high share is a **proxy**, not proof that deathballs dominate strategically; region sizes differ, travellers count as living, and automated one-commander matches do not prove human five-player readability. Unit health-loss observations are a lower bound (same-tick repairs/fatal removal can hide damage); attack counts measure shots, not damage landed. Recommendations require actual match outcomes and those evidence limits, not a synthetic harness smoke or successful compilation.

## Results: V2 lean run — 2026-10-01

Balance record for the dated conditions above; these are measured results, not a claim about subsequent builds.

**20/20 matches validated, zero failed matches, all process exit codes 0.** All used identical AI code, starting wallets **600/600**, V2, baseline-specific real payments and fixed 60 Hz game steps. Maximum observed game delta was **0.016666667536 s**. Batch wall time was **1007.56 s**; median match wall times were **42.3 s** and **39.8 s**. This is uncapped headless throughput at 1×, not evidence for a higher-dilation shortcut.

| Baseline | Team 0 wins | Team 5 wins | Draws | Median, all matches | Median, decisive | Decisive in 12–18 min | First depletion, median |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2/s | 5/10 (50%) | 4/10 (40%) | 1/10 | 23.56 min | 23.45 min | 1/9 | 9.60 min (10/10) |
| 3/s | 4/10 (40%) | 4/10 (40%) | 2/10 | 21.55 min | 19.94 min | 2/8 | 9.47 min (10/10) |

Baseline 2 ranged **17.59–40.00 min**; baseline 3 ranged **10.31–40.00 min**. The 40-minute values are censored draws. Decisive side win rates were **55.6%/44.4%** at 2/s and **50%/50%** at 3/s. This small, asynchronously simulated sample establishes no systematic side imbalance or exact 50/50 underlying probability. V2 is not symmetric; machine and human reward-region routes differ. Equal initial wallets and first-match barracks placement/completion times (both **2.02/14.00 s**) do not indicate a wallet or opening-timer handicap.

### Income and depletion

Mean observed income (Power/s), using only matches surviving to that scheduled sample:

| Game minute | 2/s team 0 | 2/s team 5 | 3/s team 0 | 3/s team 5 |
| --- | ---: | ---: | ---: | ---: |
| 2 | 30.0 | 34.0 | 31.0 | 35.0 |
| 4 | 48.0 | 36.0 | 43.0 | 43.0 |
| 8 | 36.0 | 46.8 | 34.0 | 50.2 |
| 12 | 7.2 | 12.8 | 5.2 | 16.3 |
| 14 | 2.0 | 3.8 | 3.0 | 3.0 |

The 12/14-minute baseline-3 samples contain **9 matches**; the other table entries contain **10**. Extraction creates a substantial early economy, then finite deposits reduce income to baseline around 13–15 minutes. Every match reached actual deposit depletion; control changes alone were not counted as depletion. Median first HQ damage was **21.81 min** at 2/s and **18.50 min** at 3/s, conditional on HQ damage occurring. None of the three draws had an HQ-damage event; both HQs remained **900 HP**.

Median final wallets, team 0/team 5: **17499/17217** at 2/s; **16720/21472** at 3/s. Across **4775 completed-producer 30-second observations**, none were `InsufficientResources`; **3864** were `ForceComplete`, **902** were `Producing`, **8** were `DeploymentBlocked` and **1** was `Unconfigured`. These are sampled states, not an exact uptime trace. Replacement funding was not the observed bottleneck.

### Concentration and long draws

Median largest-region unit share for snapshots with at least 12 living units: **50%/50%** at 2/s and **50%/42.9%** at 3/s (team 0/team 5). There is **no demonstrated offensive deathball dominance** in this experiment: identical planners do not provide a strategy counterfactual, and most sampled armies are split across regions.

The two baseline-3 draws do expose defensive accumulation. At 40 minutes, team 0 had **26 units in 8 living forces** and **22 units in 9 living forces**, respectively, with only **one living barracks** in each match and **100%** of units inside the human main. Thus at least **7/8 forces** were surviving orphans, consistent with the existing rule that destroyed-producer forces retain their last fronts and cannot receive new producer goals. Peak team-0 strength reached **36** in baseline 3; the three-barracks planning limit is not a global unit cap.

These concentrated survivors preserved the HQ but did **not** win: neither side damaged an HQ in either draw. The concentration curve's high late baseline-3 team-0 values describe those surviving draws, not the typical ten-match army. Offensive route/goal progress and retained orphan defensive fronts are pacing risks to investigate before interpreting resource increases as a solution; no behavior was changed solely to force faster outcomes.

### Recommendation

**Retain baseline 2/s, normal deposits 4/s with 2400 total, rich deposits 6/s with 3000 total.** Baseline 3/s modestly improved median decisive duration toward the target, but still missed it, doubled draws from **1 to 2**, and produced large unspent balances with no sampled resource starvation. That is insufficient evidence to adopt 3/s as the pacing fix. Deposit depletion already occurs before the intended 12–18-minute ending window; larger reserves or rates are not supported by these observations.

The design target remains unmet. Further tuning should distinguish goal/front commitment, independent-force attrition and uncommandable orphan defensive accumulation from economic scarcity. This is a recommendation from this bounded run, not a claim that co-op or human strategy is balanced.


## Tuned existing roster — accepted 2026-10-03

[Built] Evidence run **`20261003-034351-sim-0afb`**, measured on clean committed HEAD **`be17c3b`** (`dirty: False`) after rebasing onto main: V2, seeds **1–40**, fixed-step **1×**, **300 game seconds per ordered pair**. All **40/40 seed processes** validated, with **360/360 wipes**, no cap draws, stalled fights, failed or missing processes. Every pair has **20 team-0-first and 20 team-5-first** creations. Each squad spends the full **120 Power**, excluding the Artillery configuration fee.

### Accepted runtime definitions

[Built] The authored source and profile semantics live in [units.md](Design/units.md); production durations and the unchanged configuration fee live in [forces.md](Design/forces.md).

| Unit | Capacity | Unit Power | HP | Damage / interval (s) | Base DPS | Range (cm) | Speed (cm/s) |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: |
| Brawler | 6 | 20 | 330 | 7 / 1 | 7 | 175 | 360 |
| Rifle | 5 | 24 | 92 | 24 / 1 | 24 | 550 | 480 |
| Artillery | 3 | 40 | 160 | 40 / 2.5 | 16 | 1150 | 480 |

### Accepted ordered matrix

[Built] Each row contains **40 fights**; team 0 is left, team 5 right. Draws remain in denominators. Splash and all acceptance definitions, including the revised mirror policy, live in [units.md](Design/units.md).

| Left | Right | Team 0 wins | Team 5 wins | Draws |
| --- | --- | ---: | ---: | ---: |
| Brawler | Brawler | 23/40 (57.5%) | 17/40 (42.5%) | 0 |
| Brawler | Rifle | 2/40 (5%) | 38/40 (95%) | 0 |
| Brawler | Artillery | 34/40 (85%) | 6/40 (15%) | 0 |
| Rifle | Brawler | 40/40 (100%) | 0/40 (0%) | 0 |
| Rifle | Rifle | 19/40 (47.5%) | 21/40 (52.5%) | 0 |
| Rifle | Artillery | 8/40 (20%) | 32/40 (80%) | 0 |
| Artillery | Brawler | 2/40 (5%) | 38/40 (95%) | 0 |
| Artillery | Rifle | 38/40 (95%) | 2/40 (5%) | 0 |
| Artillery | Artillery | 20/40 (50%) | 20/40 (50%) | 0 |

[Built] **Runtime validity and all 12 balance rules pass**, with complete evidence. Combined ordered-side prey wins are Brawler **72/80 (90%)**, Rifle **78/80 (97.5%)**, Artillery **70/80 (87.5%)**; predator wins are **2/80 (2.5%)**, **10/80 (12.5%)**, **8/80 (10%)**, respectively. Both sides of each mirror have exact two-sided p-values **0.4295905078 Brawler**, **0.8746293124 Rifle**, **1 Artillery**, so none rejects 50% at the required significance.

[Built] Worth is **0.4904166667 Brawler**, **0.484375 Rifle**, **0.5252083333 Artillery**; maximum/minimum **1.0843010753**. HP/DPS per Power is **16.5 / 0.35**, **3.8333333333 / 1**, **4 / 0.4**; no unit strictly leads another on both metrics.

The selected candidate kept the original one-second Rifle cadence and passed every non-mirror rule in its ten-seed screen. Further cadence variants did not justify chasing ten-fight mirror noise. The accepted forty-seed measurement uses the orchestrator's revised statistical rule, not the superseded ±5% screen. Non-rejection is not proof of exact equality or independent seed samples. The earlier baseline below is historical; support compositions, expanded roster, network, rendering and packaged gameplay are not validated by this matrix.

[Built] Scoped check **`20261003-034304-check-5ada`** passed all selected scopes, including combat, four doctrines, both match outcomes, pursuit, production and the duel world scenario; **33 rules tests** and **872 Python tests** passed with no lint findings. Whole-match run **`20261003-034351-sim-7fa9`** validated **4/4 matches** with a **600-game-second cap**: classic-map team 0 won both matches (median **2.84 game minutes**); V2 had two natural ten-minute cap draws. This is a runtime regression smoke, not acceptance of whole-match pacing or side balance.

## Duel report — pursuit-fixed baseline published 2026-10-03

[Built] Duel mode is a separate, explicitly opted-in standalone encounter runner. It reads the selected map's live combat definitions and runs an entire ordered matrix in each fresh seed process. Operational budgets, win denominators, worth and definition-rule semantics live in [units.md](Design/units.md); launch and regeneration procedures live only in [`./x help sim`](../x).

### Reading the report

`Report.md` separates runtime validity from balance acceptance. Its ordered matrix reports left/team-0 wins, right/team-5 wins and draws for each pair; mirror rows expose side bias rather than averaging it away. Prey/predator checks combine both ordered appearances of the unit against that opponent. The rule evidence includes measured wins, worth, efficiencies and pass/fail; support composition checks are explicitly unmeasured.

The per-seed table records actual spent Power, initial members, survivors, survivor Power, effective HP removed, weapon attacks, game seconds and wipe/cap outcome. Cap durations are censored, not kill times. A fight that stops landing damage while both sides survive is `stalled`, an invalid runtime result rather than a draw. `summary.json` retains accepted rows and rule evidence; `run.json`, per-seed launch records, logs and atomic `match.json` checkpoints retain process identity and failures. A valid run may exit successfully with failed balance rules; invalid or missing matrices cannot pass rules or enter denominators.

### Measured matrix

Evidence run **`20261003-001018-sim-7ca7`**, measured and published **2026-10-03** on clean committed HEAD **`2f2aeba`** (`dirty: False`) after the pursuit review fixes and rebase: V2, seeds **1–10**, fixed-step **1×**, **300 game seconds per ordered pair**. **10/10 seed processes validated; 90/90 fights ended on a wipe, zero stalled fights, zero cap draws, zero failed or missing processes.** Each pair has five team-0-first and five team-5-first creations. Brawler and Rifle squads spent **120 Power**; Artillery squads spent **100 Power**, because the equal budget admits only two whole Artillery. Configuration fees are excluded.

Each row reports the left/team-0 and right/team-5 result separately, with ten fights per row:

| Left | Right | Team 0 wins | Team 5 wins | Draws | Mean duration (game s) |
| --- | --- | ---: | ---: | ---: | ---: |
| Brawler | Brawler | 6/10 (60%) | 4/10 (40%) | 0 | 15.18 |
| Brawler | Rifle | 10/10 (100%) | 0/10 (0%) | 0 | 7.41 |
| Brawler | Artillery | 10/10 (100%) | 0/10 (0%) | 0 | 6.31 |
| Rifle | Brawler | 0/10 (0%) | 10/10 (100%) | 0 | 7.46 |
| Rifle | Rifle | 4/10 (40%) | 6/10 (60%) | 0 | 8.93 |
| Rifle | Artillery | 10/10 (100%) | 0/10 (0%) | 0 | 5.93 |
| Artillery | Brawler | 0/10 (0%) | 10/10 (100%) | 0 | 6.13 |
| Artillery | Rifle | 0/10 (0%) | 10/10 (100%) | 0 | 5.86 |
| Artillery | Artillery | 5/10 (50%) | 5/10 (50%) | 0 | 8.48 |

**Runtime validity passes; balance acceptance fails under the then-current ten-fight mirror screen: 4 rules pass, 8 fail, all with complete evidence.** Passed: Brawler prey, Artillery predator, Artillery mirror, runtime counter-table coverage. Failed: Brawler predator, Rifle prey/predator, Artillery prey, Brawler/Rifle mirrors, roster worth ratio, and HP/DPS-per-Power dominance. Combined ordered-side wins are Brawler over Artillery **20/20**, Brawler over Rifle **20/20**, and Rifle over Artillery **20/20**; the intended cyclic counters were not balanced in this baseline.

Measured worth is **0.875 Brawler**, **0.528125 Rifle**, **0.096875 Artillery**; maximum/minimum is **9.03226**, failing the design's worth check. Base HP/DPS per Power is **7 / 1.00000**, **3 / 0.52174**, and **2.2 / 0.32308**, respectively: Brawler dominates both other units on both metrics, and Rifle dominates Artillery. These are measured failures, not permission to tune stats in the pursuit fix. Rule definitions remain in [units.md](Design/units.md).

Pursuit rules live in [units.md](Design/units.md). In the failed intermediate run `20261002-223534-sim-10fc`, the former melee endpoint left two idle capsules **177.5 cm** apart despite **175 cm** weapons; that residual arrival stall is diagnostic evidence, not the baseline.

The earlier run `20261002-174444-sim-57a0` remains invalidated: the old harness counted pursuit stalls as cap draws. Its raw artifacts remain diagnostic only. Alternated creation order and seeded offsets reduce ordering bias and near-replication; ten seeds do not establish independent random samples or statistical reproducibility.

This clean baseline supersedes `20261002-224929-sim-8c14`, which came from a dirty intermediate tree. The first clean repeat, `20261002-232439-sim-63fe`, was interrupted by the harness's wall watchdog while waiting for shared pool admission after eight completed seed processes, not by a combat stall. The successful repeat used a **900-second wall-progress watchdog** to accommodate that contention; combat stall detection, the game-time cap and balance thresholds were unchanged.

### Runtime verification

- Earlier pre-detector V2 world-scenario runs passed their then-current assertions, but those assertions tolerated pursuit stalls. They do not validate a combat baseline.
- Legacy whole-match smoke `20261002-174444-sim-3bfd` validated **4/4 matches**: two per default map, all natural cap draws at **300 game seconds**, process exit zero.
- Fixed-telemetry acceptance tests cover rule thresholds, draw denominators, spending normalization, malformed/incomplete evidence and per-map reporting; they are not proof of healthy combat.
- [Built] The revised world scenario fails on any stalled fight in its natural **60-second-per-pair** full matrix. A separate deliberately paused encounter still proves invalid stall detection; successful wipe/cap reporting never admits a stall into denominators.
- [Built] At this baseline's old stats, the separate pursuit world scenario exercised two hostile melee units leaving firing range to **200 cm**, both closing and fighting to a death within **20 game seconds**, two Brawlers killing the first then reaching and killing the second Artillery within **25 game seconds**, and standing survivors physically regrouping within **3 game seconds** without a replacement order. The tuned-stat fixture instead derives the melee death bound from HP and weapon cadence, funds the target-switch encounter at equal Power, and places the pre-combat formation goal away from contact so every survivor must physically regroup.
- No package, network, rendered presentation, friend playtest or support-composition result is claimed for this baseline.

## Verification and failure history

- Final `CoopRTSEditor Linux Development` build succeeded under the shared lock. The initial incomplete-`AArenaBounds` compile failure was fixed with the missing include.
- On 2026-10-01, final Boot `CoopRTS.Enemy.ConstructionEconomy` **PASS**, engine/SoftQuit exit **0**, **69.62 s**. It checks exact paid private economy, accepted region goals, natural forward construction/production/joining, nearest-region defense, real joined damage, producer-only retreat/backlinks and natural repairs/resumption.
- An earlier V2 strategy run passed before the final construction-clearance changes. It is not final-code V2 strategy proof; the twenty natural autonomous matches are the V2 evidence for this balance record.
- The first Boot attempt ended in Defeat and its latent fixture stalled; it was interrupted through its owned PID and retained as **failed/interrupted**, not a pass. The fixture now fails immediately on terminal match state and reports progress every **10 game seconds**. Passive human guards and an explicit **1000000-HP** human-HQ survival budget isolate economy/recovery from unrelated HQ outcomes.
- The next Boot attempt failed specifically because the fallback goal did not obtain an accepted navigable front. AI construction now preserves full-force rally space and free deposits, and rejected extractor candidates do not deadlock selection. The final Boot run passed the same damage/fallback/repair assertions after this correction. The historic V2 compound failure cannot be retrospectively assigned an exact failed predicate; raw HQ-offset rejection remains an inference.
- Harness-only validity/comparison boundary checks passed before runtime. Runtime logs/checkpoints were inspected within the owner's three-minute supervision window; the batch's per-match watchdog rejects missing progress rather than inventing draws. All owned Unreal processes exited; shared lock released.
- Lean owner policy excluded classic-map simulation, baseline 4/s, higher-dilation comparisons, extra scenario matrices, network, packaging and native/rendered presentation checks. No Steam/WAN, human co-op or visual gameplay claim follows.
- Protected `Builds/Linux`, `Builds/LinuxShipping` and the friend tarball were not rebuilt or overwritten. Development/Shipping executable and tarball SHA-256 values matched their pre-run identities. The later rally-clearance fixes are in the editor source/module, **not those protected playtest packages**.
