# Balance simulation

## Method

The default whole-match mode of `./x sim` ([`./x help sim`](../x)) runs one fresh authoritative standalone Unreal process per match. Team 5 uses the ordinary `AEnemyCommander`; an explicitly opted-in `FMatchSimulation` spawns the same class with `TeamIndex=0` and the local `ACommandPlayerState` as `Commander`. Human-side autopilot exists only in opted-in standalone worlds; economy overrides are similarly isolated. No fixtures grant money, armies, capture or damage in whole-match mode.

The runner owns process launch, headless scheduling, freshness and cleanup. Editor standalone and packaged Development matches both require the requested map to start; a wrong/unstarted map is a failure, not a result. Runtime artifact mutation is rejected.

### Current planner [Built]

Both sides evaluate every **2 game seconds**, pay through the same placement/production APIs, and use `FCommandService::IssueForceOrder`, never planner-supplied location fronts. Composition starts Frontline, then Ranged, then Siege; the planner maintains at most **3 barracks**. Optional buildings, extractors and research retain **120 Power** (one complete infantry force) as a replacement reserve. The dated whole-match results below predate this verb cutover and measured the former goal driver; they have not been re-measured under the current executor.

Expansion excludes unreachable regions and enemy mains, splits forces across distinct targets, and scores each candidate as `8 + 2 × free deposit rate − 5 × graph hops − 4 × hostile units − distance² / 4000² − 3 if enemy-controlled + 4 if already targeted`. Distance is force-center to region-anchor in centimetres. Nearest available forces Move & Hold threatened controlled regions. An offensive Attack on the enemy-main region requires at least **6 allied living units**, at least **1.25×** opposing living strength, and income no lower than the opposing team's total; exhaustion of expansion targets also leads to Attack. Identical Attack orders are not reissued, preserving the executor's casualty-withdrawal/refill state. Current verb completion and orphan rules live in [forces.md](Design/forces.md#steering-forces-change--decided).

Forward barracks require a controlled, uncontested non-main region without an owned producer; the nearest eligible anchor to the opposing HQ is preferred. Additional barracks wait for forward territory and at least one paying extractor. Extractors prefer rate and proximity, require a free nonempty deposit in safe controlled territory, and wait for the first allied recruit. Construction searches mirrored **32 directions × 9 rings**, with radii **380 + 160 × ring cm**, through normal grid, collision, navigation and footprint validation.

Construction preserves free-deposit space and existing full-force rally footprints: candidate centres must remain at least **`sqrt(2) × building footprint radius + 200 cm`** away. The 200 cm covers the six-unit formation extent and navigation-agent margin. A rejected preferred extractor placement does not deadlock investment; other eligible deposits are attempted through the normal placement API.

Average joined-unit health below **35%** requests only that producer's Retreat; planner recovery persists until average joined health reaches **80%**. This health-based planner state is distinct from Attack's alive-capacity threshold and joined-capacity resume. The Retreat executor chooses safety and handles sprint, refill and completion under [forces.md](Design/forces.md#steering-forces-change--decided), rather than a planner-supplied assembly-point goal.


### Simulation and tuning

Simulation batches, single matches, custom economy variants, wider matrices, paired dilation comparisons and report regeneration all use `./x sim`. Arguments and launch procedures live only in [`./x help sim`](../x).

The baseline3 and baseline4 variants change only the human baseline income (`human_baseline`; JEV's `jev_baseline` is a separate knob and the old single `baseline` key is gone); custom economy variants need no rebuild because their values apply at runtime.

The measured batch on **2026-10-01** was V2 only: **ten baseline-2/s matches and ten baseline-3/s matches**, seeds **1–10**, fixed-step **1×**, starting wallets **600/600**, normal deposits **4/s, 2400 total**, rich deposits **6/s, 3000 total**, and a **2400-game-second** cap. Classic-map matches, baseline 4/s, higher-dilation comparisons, extra matrices, packaging, network and presentation checks were explicitly skipped.

Higher-dilation equivalence has **not** been established. A paired sample without paid production/combat on both sides, HQ damage and capture is inconclusive, not a passing equivalence check. Until equivalence is established, the measured 1× conditions remain the tuning reference.

Runtime numeric bounds are baseline/rates **0–10000**, amounts **0–100000000**, seed **0–2147483647**, cap **1–86400 s**, requested dilation **1–32** (effective engine clamping is rejected as a request mismatch). The runtime requires an absolute output path; the runner owns it. Defaults are unchanged without opt-in.

### Step 1b gate [New]

The method for the build step 1b gate ([build-order.md](Design/build-order.md)). It records the procedure only; no result is claimed here until a run exists (starting values, orchestrator 2026-10-04). Arguments live in [`./x help sim`](../x).

- **Rush scenario:** the team-0 autopilot gives every force Attack along the path to JEV's HQ at spawn and after every refill, and never retreats except through casualty withdrawal. It uses the normal economy: the pre-built kit plus extra Barracks.
- **Seeds and cap:** at least 20 seeds per baseline variant (2/s and 1/s, [economy.md](Design/economy.md#resources-change--decided)), each with a 1200 s cap.
- **Pass:** no rush victory before 360 s, and a median battle length inside the length target ([battle.md](Design/battle.md)).
- **Reporting:** decisive matches and censored (capped) matches are reported separately, per variant, with the sample counts. A censored match is not a decisive length.
- **[Built] Rush executor:** `./x sim --scenario rush` (repeatable with `--scenario default`, the current autopilot) passes `-SimScenario=rush` to the game. Every 2 s evaluation the team-0 autopilot gives each force Attack to JEV's main region, whatever the planner would choose: planner recovery, defence and expansion are off. The force's own casualty withdrawal and refill still run, and an Attack that already stands is not reissued, so a refill resumes it. Extra Barracks follow the planner's producer maximum and build at home when no forward anchor exists.
- **[Built] Rush telemetry:** the match report carries `scenario` and `rush_target_region`, and per team-0 force the events `rush_force_seen` (first living member), `rush_force_attacking` (verb Attack toward that region), `rush_force_withdrawing` (status Withdrawing or Refilling begins: the casualty rule under the standing Attack, including a force that is under strength when it first gets the order), `rush_force_resumed` (the force leaves Withdrawing/Refilling with its verb still Attack) and `rush_force_retreat` (verb Retreat; a defect, expected never). The `resume_threshold` field on these events is the joined size at which a withdrawal resumes, not a count of resumes. `Report.md` shows the forces seen and ordered, the order delay, and the withdrawal, resume and retreat counts.
- **[Built] Run defaults:** `./x sim --gate 1b` plans V2, `baseline2` and `baseline1`, both scenarios, with the seed count and cap of the sample above. Explicit `--map`, `--variant`, `--scenario`, `--matches`, `--time-cap` and `--seed` override them, but then the gate may not reach PASS.
- **[Built] Statistics:** `Report.md` and `summary.json` (`battle_length`) give, per map, variant, scenario and dilation: matches, decisive count, median decisive length, censored count, and the median with censored matches counted at their cap. A censored match's true length exceeds its cap, so that median is a lower bound; when a censored match sits at a middle rank the median is shown as `≥` and the gate treats it as proving no ceiling. The earliest team-0 victory is listed per scenario. The variant column reads `baseline2+rush` for rush rows.
- **[Built] Gate evaluation:** `--gate 1b` prints and records PASS, FAIL or INSUFFICIENT per check and exits nonzero unless the verdict is PASS. The median battle length (censored at the cap) must lie inside the length target for each variant and the earliest rush victory (a team-0 win in the rush scenario) must respect the pass floor above; a median that is only a lower bound at the middle rank FAILs. A cell with fewer valid seeds than the sample above, or another cap, is INSUFFICIENT and can never PASS; both human baselines must be present, each with the default economy apart from `human_baseline`. An early rush victory is a FAIL at any sample size. `./x sim --report-only RUN --gate 1b` evaluates an existing run.
- **Outcome model:** a battle's end is read in one place per side: `FMatchSimulation::ResolveOutcome` in the game and `interpret_outcome` in `Tools/harness/simulation_validation.py`, which every statistic, the gate and the outcome validator (including the HQ states an outcome requires) call. Today only a destroyed HQ ends a battle; the guarded-HQ slice changes these two functions.

### Time, determinism and dilation

The world uses a **fixed 1/60 second game step**. `FApp::SetUseFixedTimeStep(true)` and `SetFixedDeltaTime(1/(60 × effective dilation))` scale the virtual undilated step inversely to world dilation; frame clamps preserve that game step. This avoids coarse actor deltas silently changing single-action-per-tick combat and production. Headless frames are uncapped and run as fast as the CPU/navigation allows at **both** 1× and higher dilation. Larger dilation does not reduce the number of gameplay frames or promise a wall-clock speedup; `wall_duration` records real `FPlatformTime` elapsed seconds rather than the engine's virtual clock.

The runner rejects a measured game delta above `1/60 + 0.0001 s`. Paired comparisons inspect shared 30-second samples **and terminal states**: wallets, income, producers/extractors, living units by role, observed production/casualties/attacks/health loss, HQ health, territorial control and concentration. Defaults allow **5% relative +1 absolute** numeric difference (share tolerance 0.02), require identical region-control transition sequences, and first construction/capture event times within **2 game seconds**. `summary.json` records metric deltas and every mismatch; the process returns nonzero for failed or inconclusive comparison.

Seeds initialize UE's global `Rand/FRand` and `SRand` streams before gameplay. Engine asynchronous navigation, actor tick ordering and floating-point work remain possible nondeterminism sources. Team 5's planner is spawned before team 0's, and this fact is recorded. The deterministic planner may not use randomness at all: ten equal seeded runs are not automatically ten independent statistical samples. Do not claim reproducibility merely from matching seed numbers.

### Telemetry API and artifacts

`FMatchSimulation` observes existing real-game APIs: `ACommandGameState::GetIncomePerSecond`, region/controller queries, `ACommandBuilding` production state, living `AArmyUnit` health/role/attack counters and `AArmyGroup` centers and replicated verb/status/target/waypoint state. `FSimulationSettings::ForWorld` supplies the actual baseline payments and deposit rate/reserve initialization; it is not a reporting-only override. Economy still pays through the existing two-second finite-extraction policy.

Each match has `launch.json` (job, command, artifact identity, return code and harness status), `stdout.log`, `game.log`, and atomically replaced `match.json` checkpoints. Schema version **1** includes:

- Map, seed, engine version/command line, requested/effective dilation, fixed-step metadata, cap, actual duration, wall duration, outcome and winning team (`0`, `5`, or null).
- Snapshots at game-time zero, every **30 s**, and termination. `time` is the actual observation time; `scheduled_time` is the sampling boundary. Past states are never manufactured for skipped frames.
- Per team: `wallet`, `income_per_second`, all living `extractors`/`barracks` (including construction), `completed_extractors`, `deposits_remaining` in currently controlled regions, `units_alive`, `units_reinforcing`, `units_by_role` (Frontline/Ranged/Siege), controlled region count/indices, HQ health, cumulative observed births/casualties/attacks/unit health loss, units per region, largest-region unit share, building production/order details, and force identities/strength/centers/regions.
- Individual deposit reserves, controlling region team and occupying extractor team at each snapshot. Occupancy is not the same as regional ownership.
- First placed/completed extractor and barracks, first capture, every region-control transition, observed HQ damage, deposit depletion, and start/finish events. Placement and completion are distinct timings.
- Static HQ locations, region polygons/roles/neighbour graph, deposit positions/rates/initial amounts, unit production and combat definitions, initial wallets, planner class and spawn ordering.
- **[Built] Current order telemetry:** building rows use `verb`, `status`, `target_region` and `waypoint_region`, replacing the former goal keys. A building without a force reports `-1` for absent verb/status/region state, not an invented holding order. Every force with living members, including every orphan, keeps its existing `id`, `number`, `alive`, `center` and `region` fields and adds `orphan`, `verb`, `status`, `target_region`, `target_structure` (actor name or null), `waypoint_region` and `resume_count`. These fields are direct observations; orphan state is not inferred from missing building rows. Schema remains version **1**, with additive force/status fields and unchanged aggregate consumers. Historical artifacts are not backfilled.
- Numeric enums follow [ForceOrders.h](../Source/CoopRTS/ForceOrders.h): verbs **0 MoveHold, 1 Attack, 2 Retreat**; statuses **0 Marching, 1 Holding, 2 Withdrawing, 3 Retreating, 4 Refilling**. Attack + Refilling + `resume_count` identifies automatic withdrawal recovery ([UI interpretation](Design/ui.md#selecting-and-giving-orders-change--decided)).

The batch writes `run.json`, `summary.json`, `Report.md`, and four PNGs: income, living units, concentration and duration. Only successful, validated, natural-exit matches enter aggregation. Win percentages use all complete matches as denominator, with draws reported separately. Duration includes censored draws; compare decisive-match distributions separately when recommending the **12–18 minute** pacing target. Curves average only matches still alive at a scheduled sample, so later points have survivorship bias.

### Outcome and failure discipline

A natural HQ destruction wins; simultaneous HQ loss follows the game's team-5 precedence. A cap reached with **both HQs alive** is a draw. The runtime flushes a terminal snapshot/status before requesting a **non-forced natural exit** with `RequestExitWithStatus(false, ...)`. A persisted complete JSON **and exit code zero**, correct requested map/seed/economy/dilation, full 30-second history, and a consistent HQ/cap outcome are all required.

Missing output, crash/nonzero exit, invalid world, write failure, interrupted world/run, wrong metadata, changed binary or stalled progress are **failures**, never draws. The default runner watchdog fails after **180 wall seconds without persisted game-time advancement**; it neither produces a winner nor modifies game time. SIGINT/SIGTERM cleans up only the private process session started by this runner, while retaining the shared lock until that process stops. Failed/interrupted attempts and unattempted planned jobs remain visible, and the batch returns nonzero.

### Symmetry, depletion and deathball interpretation

V2 is **explicitly asymmetric** in `Build/Maps/AvailabilityZoneV2.json`: the drawn outlines, blockers, deposits and seven tactical regions do not admit 180° rotation. HQ coordinates are approximately **(-6807, -6912)** for team 0 and **(8281, 7825)** for team 5. Straight-line HQ-to-near-expansion-anchor distances are about **5110 cm** versus **4161 cm**, before obstacles/navigation. Equal side win rates are not a justified null expectation for this layout. The report computes nearest-natural distances and reward-region BFS hops from the recorded real layout; use polygons, deposit positions and planner spawn ordering to distinguish travel/economy advantages from an AI bug. These are layout measurements, not match results.

Use `deposit_depleted` times and individual reserve histories to identify exhaustion. Summed controlled-region reserves can rise/fall on capture even without extraction. Report the fraction of matches reaching depletion alongside conditional timing; absence of depletion is not a zero-minute measurement.

Largest-region unit share is summarized only for snapshots with **at least 12 living units**. Force centers, `verb`/`status`, `target_region`/`target_structure`/`waypoint_region`, roles, production and regional counts support inspection of concentrated advances versus split pressure, including surviving orphan orders. A high share is a **proxy**, not proof that deathballs dominate strategically; region sizes differ, travellers count as living, and automated one-commander matches do not prove human five-player readability. Unit health-loss observations are a lower bound (same-tick repairs/fatal removal can hide damage); attack counts measure shots, not damage landed. Recommendations require actual match outcomes and those evidence limits, not a synthetic harness smoke or successful compilation.

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

**Provenance limit [Change]:** this is acceptance of the recorded `be17c3b` geometry and executor, not a new forty-seed measurement of the verb cutover. Current duel placement selects non-main region anchors rather than the former arena-grid candidates; the current geometry and evidence boundary are recorded [below](#current-duel-geometry-built--measurement-limit).

### Accepted runtime definitions

[Built] The authored source and profile semantics live in [units.md](Design/units.md); production durations and the unchanged configuration fee live in [forces.md](Design/forces.md).

| Unit | Capacity | Unit Power | HP | Damage / interval (s) | Base DPS | Range (cm) | Speed (cm/s) |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: |
| Brawler | 6 | 20 | 330 | 7 / 1 | 7 | 175 | 360 |
| Rifle | 5 | 24 | 92 | 24 / 1 | 24 | 550 | 480 |
| Artillery | 3 | 40 | 160 | 40 / 2.5 | 16 | 1150 | 480 |

[Built] Compared with the starting sheet, Brawler HP changed **150 → 330 (×2.2)** and base DPS **10 → 7 (−30%)**; Artillery HP changed **100 → 160 (+60%)** and base DPS **22 → 16 (−27.3%)**. Rifle HP changed **90 → 92 (+2.2%)**, with base DPS unchanged. These are substantial changes to the Brawler's wall identity, not minor tuning. [New] The unbuilt rows in [units.md](Design/units.md) were sized against the old Brawler and need stat/counter revalidation when added; this acceptance covers only the existing three units.

### Accepted ordered matrix

[Built] Each row contains **40 fights**; team 0 is left, team 5 right. Draws remain in denominators. Splash and all acceptance definitions, including the revised mirror policy, live in [units.md](Design/units.md).

[Built] At exactly 40 fights the revised mirror rule accepts **14–26 wins per side (35–65%), inclusive**. This is much looser than the former **45–55%** window: non-rejection at 95% does not establish ±5% fairness.

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

### Current duel geometry [Built] — measurement limit

`FSimulationDuelRunner::FindGround` now tries **non-main region anchors, ordered by distance from the world origin**, so region Attack has a real destination. It no longer searches the former arena grid. Candidates still require a whole-area pawn collision check, dense **100 cm** navigation samples and unobstructed center rays; the accepted centre, clearance, member spacing/jitter, team creation order, **1000 cm** squad separation and **1050 cm** pursuit radius are recorded in each current report's `geometry`. `site_selection` identifies the anchor-based candidate rule.

**[Change] Evidence boundary:** changing the site changes duel geometry and movement relative to anchors. Neither the historical ten-seed matrix below nor the accepted forty-seed `be17c3b` matrix above has been re-measured with this site-selection/verb executor. Their counts and balance conclusions remain attributed to their recorded commits; they are not current-branch acceptance. Runtime smoke/scoped checks do not replace a fresh balance matrix, and no old samples are relabelled as new measurements.

### Reading the report

`Report.md` separates runtime validity from balance acceptance. Its ordered matrix reports left/team-0 wins, right/team-5 wins and draws for each pair; mirror rows expose side bias rather than averaging it away. Prey/predator checks combine both ordered appearances of the unit against that opponent. The rule evidence includes measured wins, worth, efficiencies and pass/fail; the support composition rule reports the partner squad with and without the support unit against its target, per side order (from the Lancer and Scrambler run below; older runs carry no composition fights).

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

## Lancer and Scrambler — starting-stat duel, 2026-10-04

**Historical:** this run measured the 45 Power / 110 HP + 80 shield Lancer and the 35 Power / 400 cm Scrambler from the first `units.md` sheet. The catalogue now holds the X1 values; see [X1 adopted](#x1-adopted--committed-values-run-and-open-owner-decision).

Both units were catalogue rows in [Build/Content/units.json](../Build/Content/units.json) with the [units.md](Design/units.md) starting values (Lancer 45 Power, 110 HP + 80 shield, 30 damage, 550 range, 420 speed; Scrambler 35 Power, 80 HP, 12 EMP damage, 550 range, 480 speed, pulse 10 s / 400 cm / 3 s). Each is produced at the Barracks. The duel exporter now reports shields (`shield` on every definition, `shield_damage_dealt` per fight, shield loss restarts the stall clock) and runs the support composition fights; the rules module has one prey/predator rule per listed opponent, worth and dominance on combat units with HP plus shield, and the `scrambler_composition` rule.

Evidence run **`20261004-104423-sim-69e3`**, task branch `task/units-content` at **`4893614`** plus locally generated `.uasset` files built from that commit's text (`dirty: True` only for those regenerated binaries): V2, seeds **1–40**, fixed-step 1×, 300 game seconds per fight. **40/40 seed processes validated; 1000/1000 pair fights and 160/160 composition fights ended on a wipe; zero stalled, zero cap draws, zero failed or missing.** Odd seeds create team 0 first, even seeds team 5. The starting values **fail** the acceptance rules; no stat was changed.

| Rule | Verdict | Measured |
| --- | --- | --- |
| Brawler prey Artillery / predator Rifle | pass | 74/80, 6/80 |
| Brawler predator Lancer (at most 35%) | **fail** | Brawler wins 80/80 |
| Rifle prey Brawler / predator Artillery | pass | 74/80, 4/80 |
| Artillery prey Rifle / predator Brawler | pass | 76/80, 6/80 |
| Lancer prey Brawler (at least 65%) | **fail** | Lancer wins 0/80 |
| Mirrors (40 fights each, five units) | pass | Brawler 23–17, Rifle 26–14, Artillery 18–22, Lancer 21–19, Scrambler 19–21 |
| Scrambler composition (+20 points) | **fail** | Rifles alone 80/80, with one Scrambler 80/80, gain 0.0 points |
| Roster worth ratio (at most 1.25) | **fail** | Brawler 0.607, Rifle 0.600, Artillery 0.643, Lancer 0.151; ratio 4.26 |
| No HP-plus-shield and DPS per Power dominance | **fail** | Lancer (4.22 / 0.667) dominates Artillery (4.00 / 0.400) |
| Runtime counter-table coverage | pass | five ids, Scrambler flagged support |

Ordered matrix (team 0 wins / team 5 wins of 40, no draws): the Lancer loses to every combat unit **0/40** (Brawler, Rifle, Artillery) and beats only the Scrambler 40/40; the Scrambler loses to everything. Brawler, Rifle and Artillery keep their accepted relations (Brawler–Rifle 4/36, Brawler–Artillery 37/3, Rifle–Artillery 3/37). Equal budgets buy two Lancers (90 Power), three Scramblers (105) and five Rifles (120); the composition squad is one Scrambler plus three Rifles (107).

**Diagnosis [Inference from the matrix and unit stats].** Per Power the Lancer is a Rifle with more durability and 30% less damage, in squads of two against squads of five or six, and fights end within five to seventeen game seconds, so the first-pulse range (400 cm, inside the 550 cm weapon range) is rarely used. Candidate changes screened for 10 seeds each (diagnostic, not committed, not acceptance): damage 60 passes every prey/predator rule but lets the Lancer dominate the Rifle and Artillery (worth ratio 1.99); damage 45 with 100 HP passes prey/predator and dominance but loses to Rifle and Artillery (worth ratio 1.66); cost 30 with 50 HP + 70 shield passes prey/predator and dominance (worth ratio 1.62, the Brawler now loses to two units). None passes the composition rule: Rifles alone already win 65–100% against the Lancer, and swapping two Rifles for a Scrambler lost 40–80 points in every screen. Figures: `/tmp/cooprts-work/tasks/units-content/evidence/`.

## Lancer and Scrambler tuning round — no passing point, 2026-10-04

**Historical:** this section records the search before the orchestrator adopted X1; its statements about the committed `units.json` describe that moment. The committed values are X1 (next section).

Search over Lancer cost, HP, shield and damage and Scrambler cost, damage and pulse radius (Scrambler HP, cooldown and stun, the three existing units and every acceptance rule unchanged). Each point was a 8–10 seed diagnostic screen on the rebased branch; two candidates got a 40-seed run. **No point passes every rule; the committed `units.json` still holds the starting values.** Screens, scripts and captures: `/tmp/cooprts-work/tasks/units-content/evidence/`.

**What the engine does (measured).** Shots to kill are discrete: a Rifle (92 HP) dies to three Lancer shots at damage 31 or more and to four at 23–30; the Lancer's 1.5 Piercing bonus makes a Brawler (330 HP) need eight shots at 30 and ten at 24; an Artillery (160 HP) needs six at 30. A Lancer with HP plus shield at the Rifle's per-Power value (3.83) cannot raise damage per Power above the Rifle's unless the ratio is exactly 3.8333, so cost 24, 30 and 36 (durability 92, 115, 138) are the free points. Lancers at cost 24 and damage 24–30 are near parity with five Rifles (Rifles win 62–78%); damage 31 at cost 30, or 46 at cost 36, flips them to winning 95–100%.

| Candidate | Lancer | Scrambler | Seeds | Worth (Brawler / Rifle / Artillery / Lancer) | Ratio | Composition | Other rules |
| --- | --- | --- | --- | --- | ---: | --- | --- |
| **X1** `20261004-115935-sim-41c1` | cost 24, 36 HP + 60 shield, 24 damage | cost 24, 12 damage, radius 600 | 39 valid of 40 (seed 26's process received SIGTERM, exit 143, from outside the run; the report marks every rule failed for the missing seed, the values below are measured on the 39) | 0.376 / 0.494 / 0.599 / 0.531 | **1.59 fail** | 55.1% to 96.2%, **+41.0 pass** | all prey/predator, dominance and mirrors pass |
| **C0** `20261004-121422-sim-050b` | cost 30, 50 HP + 70 shield, 30 damage | cost 24, 12 damage, radius 600 | 40/40 | 0.373 / 0.515 / 0.611 / 0.501 | **1.64 fail** | 81.3% to 95.0%, **+13.75 fail** (the baseline is too high) | all prey/predator, dominance and mirrors pass |

Lancer results for X1 (wins of the Lancer): Brawler 78/78, Rifle 29/78, Artillery 11/78. Mean scores (row against column): Brawler 0.13 against the Lancer, Rifle 0.55, Artillery 0.73.

**Binding constraint: the worth ratio, and it is structural.** A pair of units scores 1 together, so Brawler worth is (0.91 + a) / 3 with a the Brawler's score against the Lancer, Rifle (0.97 + r) / 3, Artillery (1.12 + q) / 3 and Lancer (3 − a − r − q) / 3 (existing-pair scores from the measured matrix). A ratio of at most 1.25 needs a at least about 0.44, r in 0.4–0.7 and q below 0.57: the Lancer must beat the Brawler narrowly while roughly tying the Rifle and the Artillery. Every screened point has a between 0.01 and 0.14 and q between 0.43 and 0.77: wherever the Lancer ties the Rifle (cost 24 or 30, damage 23–30) it beats the Brawler 100% with 80–90% of its Power left, and the Artillery's two volleys kill it. Raising damage to beat the Artillery (cost 30, damage 32: Artillery score 0.57) also crushes Rifles and Brawlers (worth ratio 2.04). The Brawler's own worth is the floor: it loses to the Rifle and now to the Lancer, and the Artillery is already 1.23 times the Brawler without a Lancer.

The composition rule is not binding: with a Scrambler of cost 24 (one Scrambler plus four Rifles at the 120 Power budget) and radius 600 the pulse lands as the fight starts and X1 gains 41 points; with the starting Scrambler (cost 35, radius 400) the gain stayed at or below 0 in every earlier screen.

### X1 adopted — committed-values run and open owner decision

[Built] X1 is committed in [Build/Content/units.json](../Build/Content/units.json) (orchestrator decision 2026-10-04): Lancer cost 24, 36 HP + 60 shield, 24 damage per 1.0 s, 550 range, 420 speed (4.0 s per unit); Scrambler cost 24, 80 HP, 12 EMP damage, pulse radius **600 cm** (cooldown 10 s, stun 3 s unchanged). Evidence run **`20261004-124942-sim-0ec9`** on commit **`911928f`** (`dirty: True` only for the locally generated `.uasset`s built from that text): V2, seeds 1–40, 300 game seconds per fight; **40/40 seed processes valid, 1000 pair and 160 composition fights all wiped, zero stalled, failed or missing.**

| Rule | Verdict | Measured |
| --- | --- | --- |
| Brawler prey Artillery / predator Rifle / predator Lancer | pass | 73/80, 3/80, 0/80 |
| Rifle prey Brawler / predator Artillery | pass | 77/80, 9/80 |
| Artillery prey Rifle / predator Brawler | pass | 71/80, 7/80 |
| Lancer prey Brawler | pass | 80/80 |
| Mirrors (40 fights each, five units) | pass | Brawler 20–20, Rifle 18–22, Artillery 17–23, Lancer 23–17, Scrambler 22–18 |
| Scrambler composition (+20 points) | pass | five Rifles 42/80 (52.5%); one Scrambler plus four Rifles 80/80 (100%); +47.5 points |
| No HP-plus-shield and DPS per Power dominance | pass | none |
| Runtime counter-table coverage | pass | five ids, Scrambler flagged support |
| **Roster worth ratio (at most 1.25)** | **fail, open owner decision** | Brawler 0.370, Rifle 0.490, Artillery 0.605, Lancer 0.535; ratio **1.634** |

Lancer wins over both ordered sides: Brawler 80/80, Rifle 37/80 (46%), Artillery 10/80. The rule itself is unchanged. **Owner decision:** the derivation above shows the ratio cannot be met by Lancer or Scrambler stats under the prey rule (the Brawler would need to score about 0.44 against the Lancer while losing 65% of fights, and the Artillery is already 1.23 times the Brawler without a Lancer). Options: relax the limit, or change the worth opponent set (for example, count only each unit's prey and predator opponents, or include the Scrambler as an opponent; the latter lowered the ratio only from 1.59 to 1.56 on the X1 screen).

## Verification and failure history

- Final `CoopRTSEditor Linux Development` build succeeded under the shared lock. The initial incomplete-`AArenaBounds` compile failure was fixed with the missing include.
- On 2026-10-01, final Boot `CoopRTS.Enemy.ConstructionEconomy` **PASS**, engine/SoftQuit exit **0**, **69.62 s**. It checks exact paid private economy, accepted region goals, natural forward construction/production/joining, nearest-region defense, real joined damage, producer-only retreat/backlinks and natural repairs/resumption.
- An earlier V2 strategy run passed before the final construction-clearance changes. It is not final-code V2 strategy proof; the twenty natural autonomous matches are the V2 evidence for this balance record.
- The first Boot attempt ended in Defeat and its latent fixture stalled; it was interrupted through its owned PID and retained as **failed/interrupted**, not a pass. The fixture now fails immediately on terminal match state and reports progress every **10 game seconds**. Passive human guards and an explicit **1000000-HP** human-HQ survival budget isolate economy/recovery from unrelated HQ outcomes.
- The next Boot attempt failed specifically because the fallback goal did not obtain an accepted navigable front. AI construction now preserves full-force rally space and free deposits, and rejected extractor candidates do not deadlock selection. The final Boot run passed the same damage/fallback/repair assertions after this correction. The historic V2 compound failure cannot be retrospectively assigned an exact failed predicate; raw HQ-offset rejection remains an inference.
- Harness-only validity/comparison boundary checks passed before runtime. Runtime logs/checkpoints were inspected within the owner's three-minute supervision window; the batch's per-match watchdog rejects missing progress rather than inventing draws. All owned Unreal processes exited; shared lock released.
- Lean owner policy excluded classic-map simulation, baseline 4/s, higher-dilation comparisons, extra scenario matrices, network, packaging and native/rendered presentation checks. No Steam/WAN, human co-op or visual gameplay claim follows.
- Protected `Builds/Linux`, `Builds/LinuxShipping` and the friend tarball were not rebuilt or overwritten. Development/Shipping executable and tarball SHA-256 values matched their pre-run identities. The later rally-clearance fixes are in the editor source/module, **not those protected playtest packages**.
