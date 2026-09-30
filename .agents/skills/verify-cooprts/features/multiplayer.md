# Real multiplayer verification

Use actual IPv4 loopback sockets with separate processes/worlds, not a second PlayerState spawned in one standalone test. The Development-only `ArmyNetworkVerification.cpp` probe is inert unless explicitly opted in per process; host-only authority fixtures additionally require the listen host flag and actual authority. Never enable fixtures on a public play server. `network.py` accepts only `--scenario construction` (also its default) and `--clients 0|1|4`. The former `full`, `restart` and `objective` flags are **not executable**. For an ordinary network change use editor host plus one remote; choose five players, loss or zero remote only for the affected contract or a separately scheduled acceptance run.

```bash
N=.agents/skills/verify-cooprts/scripts/network.py
"$N" --run Saved/Verification/construction-editor-two-unique --mode editor --clients 1 --scenario construction --max-fps 60
```

The following is a **release acceptance menu**, not an automatic command chain. Run one check at a time, inspect its result and release the shared runtime window before another. Editor host/remote precedes current package checks; a fresh package is a prerequisite for packaged proof:

Do not use the complete lifecycle as a blocking gate for ordinary force development. Focused standalone behavior plus a direct owning-peer/replica observation can establish the affected slice without waiting through outposts, research, victory and restart. Keep partial assertions/evidence, stop the exact owned run through coordinator cleanup, and report the uncompleted lifecycle as unverified rather than inventing PASS.

```bash
"$N" --run Saved/Verification/construction-package-two-unique --mode packaged --clients 1 --scenario construction --max-fps 60
"$N" --run Saved/Verification/construction-package-five-unique --mode packaged --clients 4 --scenario construction --max-fps 60
"$N" --run Saved/Verification/construction-package-five-loss-unique --mode packaged --clients 4 --scenario construction --emulation --max-fps 60
```

A zero-remote editor listen host is also accepted: `"$N" --run Saved/Verification/construction-editor-solo-unique --mode editor --clients 0 --scenario construction --max-fps 60`. `--emulation` supplies native `PktLag=120` / `PktLoss=8` to every process and requires those observed net-driver values; it does not model NAT. If current packaged `-nullrhi` demonstrably fails, preserve the failed run, then try a **new** directory with `--rendered`; this changes rendering, not socket assertions. Do not infer package NullRHI support from an older artifact.

## What the construction socket scenario actually observes

It starts with independent commander identities, real listen/client modes, empty friendly armies and explicit host-funded budgets. The owning remote builds a barracks; invalid/overlap and foreign commands preserve cost/state. First Start locks Siege for exactly 180 with no recruit funds, then pause/resume and invalid/different/foreign roles preserve type, force identity, progress and front. A 50-unit budget produces one physical traveller, real movement and joined arrival; another 50 fills the two-slot force with no repeated fee. A second Ranged barracks independently fills four paid slots and retains its front while the first changes. Actual hostile lethal damage opens one vacancy; exactly 50 creates a replacement, which retargets and physically joins as the force moves. Joined/travelling counts, roles, owners, composition slots and producer backlinks converge in separate worlds.

The produced force naturally captures a sector; a paid completed outpost establishes it after real departure. Workshop research is paid/once-only. Actual targeted HQ weapon damage plus an explicit finish fixture reaches Victory, not a native Q command or unaided match. Restart checks fresh world/actor identities, preserved net drivers/commander slots, reset wallet/research/HQs/sectors, and no friendly buildings, configured forces or recruits. Isolation, income pause, funding and lethal setup are recorded fixtures. This does not prove five-player coordination, all-wallet natural income, every doctrine, delayed-peer three-cycle restart, fault recovery or arbitrary rejoin.

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
