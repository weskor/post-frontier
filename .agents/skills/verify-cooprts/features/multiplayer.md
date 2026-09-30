# Real multiplayer verification

Use actual IPv4 loopback sockets with separate processes/worlds, not a second PlayerState spawned in one standalone test. The Development-only `ArmyNetworkVerification.cpp` probe is inert unless explicitly opted in per process; host-only authority fixtures additionally require the listen host flag and actual authority. Never enable fixtures on a public play server. `network.py` accepts `--scenario ownership|production|economy|restart|construction` (default `construction`) and `--clients 0|1|4`. The former `full` and `objective` flags are **not executable**; `restart` is the new connected-restart slice, not the former three-cycle delayed-peer scenario. For an ordinary network change use editor host plus one remote and the slice that owns the changed contract; choose five players, loss or zero remote only for the affected contract or a separately scheduled acceptance run.

```bash
N=.agents/skills/verify-cooprts/scripts/network.py
"$N" --run Saved/Verification/ownership-editor-unique --mode editor --clients 1 --scenario ownership --max-fps 60
"$N" --run Saved/Verification/production-editor-unique --mode editor --clients 1 --scenario production --max-fps 60
"$N" --run Saved/Verification/economy-editor-unique --mode editor --clients 1 --scenario economy --max-fps 60
"$N" --run Saved/Verification/restart-editor-unique --mode editor --clients 1 --scenario restart --max-fps 60
```

Every slice starts its own host and clients, destroys the enemy planner, pauses income and funds the owning peer explicitly, so nothing depends on an earlier slice or on natural income. Each slice's setup is the minimum the contract needs: `ownership` stops after the rejection barrier; `production` builds and configures one siege barracks; `economy` additionally recruits the two-unit siege force; `restart` recruits one unit, buys research and uses the finish fixture to reach a terminal state before the restart request. Their assertions are the same `require`/converge calls the acceptance chain runs (`build_barracks`, `configure_siege`, `reject_locked_commands`, `produce_and_replace`, `expand_and_research`, `restart_and_converge` in `network.py`).

The following is the **release acceptance menu**, not an automatic command chain. Run one check at a time, inspect its result and release the shared runtime window before another. Editor host/remote precedes current package checks; a fresh package is a prerequisite for packaged proof:

Do not use the complete chain as a blocking gate for ordinary force development. The matching slice plus focused standalone behavior establishes the affected contract without waiting through outposts, research, victory and restart. Keep partial assertions/evidence, stop the exact owned run through coordinator cleanup, and report the uncompleted chain as unverified rather than inventing PASS.

```bash
"$N" --run Saved/Verification/construction-editor-two-unique --mode editor --clients 1 --scenario construction --max-fps 60
"$N" --run Saved/Verification/construction-package-two-unique --mode packaged --clients 1 --scenario construction --max-fps 60
"$N" --run Saved/Verification/construction-package-five-unique --mode packaged --clients 4 --scenario construction --max-fps 60
"$N" --run Saved/Verification/construction-package-five-loss-unique --mode packaged --clients 4 --scenario construction --emulation --max-fps 60
```

A zero-remote editor listen host is also accepted: `"$N" --run Saved/Verification/construction-editor-solo-unique --mode editor --clients 0 --scenario construction --max-fps 60`. `--emulation` supplies native `PktLag=120` / `PktLoss=8` to every process and requires those observed net-driver values; it does not model NAT. If current packaged `-nullrhi` demonstrably fails, preserve the failed run, then try a **new** directory with `--rendered`; this changes rendering, not socket assertions. Do not infer package NullRHI support from an older artifact.

## What the slices and the construction chain actually observe

`ownership` starts with independent commander identities, real listen/client modes, empty friendly armies and explicit host-funded budgets. The owning remote builds a barracks for exactly 220; duplicate, outside-arena (twice the replicated `arenaHalfExtent`) and foreign commands preserve cost/state. First Start locks Siege for exactly 180 with no recruit funds and reports capacity 2, unit cost 50 and 20⁄3 s from the definition; then pause/resume and invalid/different/foreign roles preserve type, force identity, progress and front behind an owner resume/pause RPC barrier.

`production` continues from a configured siege barracks: a 50-unit budget produces one physical traveller that leaves the producer toward the front; its movement is **latched per peer** the first time that peer observes it, so host and remote never have to show the same transient sample; then joined arrival. Another 50 fills the two-slot force, `productionState` reads `ForceComplete` then `Paused`, with no repeated fee. A second Ranged barracks independently fills four paid slots and retains its front while the first changes. Actual hostile lethal damage opens one vacancy; exactly 50 creates a replacement whose retarget after the front moves is again latched per peer, and it physically joins without touching the other force's four members, front or wallet.

`economy` recruits the two-unit siege force first. The force naturally captures sector 0 (front assigned to the replicated site position) without income; a paid outpost placed at an offset from that site establishes it after real departure. Workshop research is paid/once-only and scoped. Actual targeted HQ weapon damage plus an explicit finish fixture reaches Victory, not a native Q command or unaided match.

`restart` recruits one unit, buys research and finishes by fixture, then requests the connected restart. The host must first expose the fresh world before autonomous play (`isolate_fresh_host`); every peer must then converge on one predicate that includes the reset itself: new generation, same commander slot, all players present, result 0, both HQs at 900, no team-0 armies or buildings, doctrine 0 and wallet ≥ 600 for every player, and all three sectors present, neutral and at zero progress. Afterwards each peer's GameState id must differ from the old one on the same net driver. Isolation, income pause, funding and lethal setup are recorded fixtures. Nothing here proves five-player coordination, all-wallet natural income, every doctrine, delayed-peer three-cycle restart, fault recovery or arbitrary rejoin.

Pending reports print, per peer, every building's `constructionProgress`, `joined`/`travelling` and `productionState` plus each reinforcing unit's owner/army/slot/position, so a stalled predicate can be classified from the console before opening `responses.jsonl`. Map points in the scripts are offsets from `friendlyHQPosition`, `enemyHQPosition` and `sites[].position`; a regenerated map changes no script literal.

Require a PASS record, per-peer `responses.jsonl` and game logs, independent replicated convergence, net-mode/peer identity and pidfd cleanup. The coordinator fails terminal network/travel/contradictory state, owned process exit and artifact mutation. No command deadline or wall-clock predicate timeout: reports of an unchanged pending predicate require immediate inspection of peer snapshots and exact condition. A living process or new `observe` reply is not progress. Interrupt the exact owned runner via cleanup on impossible/stalled state; retain failed evidence. Build/package only after all source handoffs; never mutate the artifact during a run or terminate a user's process.

## Native input is a separate proof layer

`network_desktop.py` retains per-peer identity, focus, freshness and capture/input guards for packaged windows (`--clients 1|4`). Inspect each viewport, click building choices/ground, wait for completion, configure production/fronts and observe real squads. Its driver still accepts `tab`, `h`, `r`, `q` for explicit no-op/removal checks; those keys no longer bind squad controls in the game. Space focuses building/HQ. Use persistent Construction to reopen choices and the minimap to pan. The socket scenario's internal order/targeted-attack RPCs are fixture/API proof, not current human controls. No 1/2, F1/F2/F3 or N driver inputs exist.

```bash
D=.agents/skills/verify-cooprts/scripts/network_desktop.py
R=Saved/Verification/construction-view-two-unique
"$D" --run "$R" launch --clients 1
"$D" --run "$R" doctor --peer host
"$D" --run "$R" focus --peer c1
"$D" --run "$R" capture --peer c1 client-empty-base
"$D" --run "$R" stop
```

Use current geometry to determine button/ground fractions before issuing `click --peer c1 left --x <fraction> --y <fraction>`; do not run literal placeholders. `launch --clients 1 --probe` enables explicit Development observations/host-only fixture support for a **separate** controlled native session, not normal play. Inspect images, correlate per-peer `LogArmyOrders`/HUD with actual state changes, and retain setup commands separately from human input. The five-player roster needs a separate owned `--clients 4` native run if in scope. Numeric/headless assertions and inspected windows still do not prove human five-player readability, coordination or deathball balance.

## Historical evidence, not current cutover proof

Before construction cutover, `Saved/Verification/restart-package-five-loss-delayed-a-20260929/RESULTS.md` recorded three-cycle forced-delay seamless restart acceptance; `Saved/Verification/m8-package-five-loss-final-a-20260929/RESULTS.md` retained earlier nonseamless capped/uncapped failure comparison. Old `m8-view-two-final-b-20260929` and `m8-view-five-final-a-20260929` captures covered the **previous** native HUD/army controls. The forced-delay three-cycle check has **not** been rerun on the construction cutover; the current one-cycle `construction` scenario must not be reported as its replacement. An older packaged game or native capture does not prove present construction UI. Preserve history as history, not an executable old recipe or new proof.
