# Real multiplayer verification

Milestone 8 requires real IPv4 loopback sockets and **separate processes/worlds**, never a second PlayerState spawned in a standalone world. `Source/CoopRTS/ArmyNetworkVerification.cpp` is development-only (`WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING`) and inert without explicit `-CoopRTSNetVerifyDir` plus `-CoopRTSNetVerifyPeer`. Only the listen host receives the additional `-CoopRTSNetVerifyAuthority` flag. Clients cannot enable server fixtures by a join URL or by writing a fixture request: the probe checks both the host-only flag and `GetAuthGameMode()` / `NM_ListenServer`. Never distribute an enabled Development host as a normal play server.

Build **after** all C++ slices are ready; do not run these while a gallery editor/build/package owns the artifact. From the repository root, after the README editor build/package commands:

```bash
N=.agents/skills/verify-cooprts/scripts/network.py
"$N" --run Saved/Verification/m8-editor-two-unique --mode editor --clients 1 --max-fps 60
"$N" --run Saved/Verification/m8-package-two-unique --mode packaged --clients 1 --max-fps 60
"$N" --run Saved/Verification/m8-package-five-unique --mode packaged --clients 4 --max-fps 60
"$N" --run Saved/Verification/m8-package-five-loss-unique --mode packaged --clients 4 --emulation --max-fps 60
```

Order matters: editor host/remote first, package host/remote second, package host/four remote third, loss/lag package run fourth. Do not reuse an evidence directory. The package `-nullrhi` launch has to succeed on the actual artifact; failure is a blocker, not proof. `--emulation` passes UE's `-PktLag=120 -PktLoss=8` to **every process** and asserts each native net driver's observed simulation values (milliseconds/percent); it does not simulate Internet NAT. All processes have their own `game.log`, `stdout.log`, `responses.jsonl`, process identity, and a shared `events.jsonl` with commands/results/PASS or FAIL. Package/editor stamps are checked before and throughout. The coordinator checks PID+start+executable, never adopts a user's process, stops only its own peers by pidfd on exit, and keeps evidence. Every wait prints its pending predicate and peer summaries at most five seconds apart; the positional wait numbers are reporting intervals, not deadlines. Terminal failures, contradictory snapshots, process exits and artifact mutation fail loudly. A process exit, actor echo or server-only snapshot is never sufficient.

If that fresh package demonstrably fails `-nullrhi`, preserve its failed run and retry with a new directory and `--rendered` (e.g. `"$N" --run Saved/Verification/m8-package-two-rendered-unique --mode packaged --clients 1 --rendered`). This uses real Vulkan game windows for the **same** socket assertions but is not visual proof without inspection. Do not silently switch modes or count the failed attempt as a pass.

The host fixture can isolate the enemy planner, teleport one unit into a resource site's capture radius, lower wallet/health, then let the **actual capture tick and hostile weapon** cause capture/death/HQ destruction. RPCs come from the *owning local PlayerController in each client process*: `ServerIssueOrder`, `ServerIssueAttack`, `ServerReinforce`, `ServerChooseDoctrine`, `ServerRequestRestart`. Every `observe` walks that process's own replicated `PlayerArray`, `CommanderIndex`, `OwningPlayerState`, groups, composition slots/roles/health/positions/order serial, capture sites, HQs and wallets. No fixture RPC is compiled into shipping gameplay. Tests require foreign-army order/attack/purchase rejection with an actual casualty, invalid/repeated doctrine lockout and independent choices, one-site capture/income, hostile-weapon death, insufficient/spammed paid purchase (exact 80-resource siege debit and exact six-role roster), replacement inheritance and arrival after replaced orders, both replicated outcomes and fresh non-seamless travel. Five-player run additionally checks all five peer worlds at capture/death/purchase, identity separation, late join, logout/army removal, reused free slot, and sixth-player rejection. Two-player run checks terminal late-join rejection. Failure stages must be diagnosed, not weakened.

The development-only listen-host probe first observes natural +32 captured-site income ticks in every player's separate wallet. It then pauses the server income tick only for the zero-funds rejection, exact 360-resource starting wallet, paid-spam barrier and 80-resource debit audit; natural income would otherwise make those transactional snapshots time-dependent. The pause is not a client command, is excluded from shipping, and resets on each fresh world after restart. `events.jsonl` records each verified phase and any contradictory response fails instead of waiting for a transient wallet range.

During the explicit connected `ServerTravel` restart checks, the probe accepts a brief `game world unavailable`/`ready=false` response while each peer changes worlds, then requires a new generation with a complete roster and reset doctrines. Other observations reject an unavailable game world; initial joins may report `ready=false` but not a missing world. A client's `NetworkFailure`, terminal `?closed` Browse, or any peer's `TravelFailure` fails immediately with its log line. No wall-clock deadline determines success or failure. For a focused three-cycle five-world comparison, use `--scenario restart --clients 4`; `--max-fps 60` on a fresh run caps every peer to test CPU/frame-stall sensitivity without changing production travel behavior.

The 2026-09-29 Development artifact passed fresh editor two-peer, packaged two-peer, packaged five-peer, and packaged five-peer packet-emulated full runs (exact paths in `Saved/Verification/m8-package-five-loss-final-a-20260929/RESULTS.md`). Every full run's `run.json` records `max_fps: 60`; final emulated net-driver snapshots show 120 ms/8% in the host and every connected client. Focused packaged five-world restart ran three cycles capped (`m8-restart-focus-capped-b-20260929`, 31.3 s) and uncapped (`m8-restart-focus-uncapped-b-20260929`, 395.8 s). The capped run logged no `Very long time between ticks`; the uncapped run logged repeated 5–9.5 s stalls. Both passed, but earlier uncapped `m8-package-five-travel-b-20260929` had a client receive `HostClosedConnection`/`Boot?closed` without first browsing `?Restart`. [INFERENCE] A starved client can miss the nonseamless travel notification before server teardown; the cap is a verification-load mitigation, not a production fix or proof that race cannot recur. Preserve failed evidence and report that residual risk.

Native inspected captures: `Saved/Verification/m8-view-two-final-b-20260929/` correlates client-focused 1/Space, a real right-click Move with server `accepted Move`, F2 Field Repairs and H Hold with the host roster/order log. `Saved/Verification/m8-view-five-final-a-20260929/c4-five-player-roster-clear.png` shows all five entries from focused Commander 5 after temporarily expanding its cramped owned window. The first rendered launch `m8-view-two-final-a-20260929` failed before `game.log` existed; `network_desktop.py::doctor` now treats a missing startup log as not-ready, and a fresh run passed. Numeric/headless and inspected UI evidence do not prove human five-player readability or deathball balance.

Numeric assertions do not establish render readability or teamwork. Use a **fresh, separate** native-window run for short host/client and five-player visual/input proof:

```bash
D=.agents/skills/verify-cooprts/scripts/network_desktop.py
R=Saved/Verification/m8-view-two-unique
"$D" --run "$R" launch --clients 1
"$D" --run "$R" doctor --peer host
"$D" --run "$R" focus --peer host
"$D" --run "$R" capture --peer host host-initial
"$D" --run "$R" focus --peer c1
"$D" --run "$R" key --peer c1 1
"$D" --run "$R" capture --peer c1 client-own-army
"$D" --run "$R" stop
R=Saved/Verification/m8-view-five-unique
"$D" --run "$R" launch --clients 4
"$D" --run "$R" focus --peer c4
"$D" --run "$R" capture --peer c4 five-player-roster
"$D" --run "$R" stop
```

For actual native input-to-order, inspect a screenshot to choose ground inside the focused window, then `point --peer c1 --x .7 --y .7` and `click --peer c1 right --x .7 --y .7`, `key --peer c1 h`, `key --peer c1 f2`, etc.; capture after each meaningful transition and correlate the peer's `LogArmyOrders` / HUD feedback. `doctor` checks owned window, package freshness and engine startup; `focus` deliberately targets the recorded Hyprland address. Input and capture refuse any unfocused target. The driver preserves existing `verify.py` single-instance mode. Native screenshots need visual inspection and correct host/client focus; five simultaneous windows may not all fit the display. Human **five-player coordination/readability and deathball dominance** are unverified until a real five-human playtest; screenshots and numeric probe cannot settle that judgement. Summarize exact commands, stamps, per-peer results, inspected images, failures and that human gap in a local `RESULTS.md` only after a run.
