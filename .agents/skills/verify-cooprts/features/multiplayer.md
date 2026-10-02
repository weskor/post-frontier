# Real multiplayer verification

Use actual IPv4 loopback sockets with separate processes/worlds, not a second PlayerState spawned in one standalone test. The Development-only `ArmyNetworkVerification.cpp` probe is inert unless explicitly opted in per process; host-only authority fixtures additionally require the listen host flag and actual authority. Never enable fixtures on a public play server. `network.py` accepts `--scenario ownership|production|economy|restart|construction` (default `construction`) and `--clients 0|1|4`. The former `full` and `objective` flags are **not executable**; `restart` is the new connected-restart slice, not the former three-cycle delayed-peer scenario. For an ordinary network change use editor host plus one remote and the slice that owns the changed contract; choose five players, loss or zero remote only for the affected contract or a separately scheduled acceptance run.

```bash
N=.agents/skills/verify-cooprts/scripts/network.py
"$N" --run Saved/Verification/ownership-editor-unique --mode editor --clients 1 --scenario ownership --max-fps 60
"$N" --run Saved/Verification/production-editor-unique --mode editor --clients 1 --scenario production --max-fps 60
"$N" --run Saved/Verification/economy-editor-unique --mode editor --clients 1 --scenario economy --max-fps 60
"$N" --run Saved/Verification/restart-editor-unique --mode editor --clients 1 --scenario restart --max-fps 60
```

`network.py` adds `-nosteam` to **every** launched editor or packaged host, client and rejection peer, including rendered/offscreen and packet-emulated runs. This keeps these probes on plain IPv4/IP transport even when the local Steam client is running; do not remove the switch to test Steam. Post-cutover editor ownership/production passed in `Saved/Verification/round2/RESULTS.md`; ownership passed again with Steam running in `Saved/Verification/steam/RESULTS.md`. Economy/restart sockets, full construction and packaged network acceptance were not rerun in round 2. IP results do not verify lobby sessions, overlay, Steam relay/NAT traversal or Internet play.

Pass `--map /Game/Maps/AvailabilityZone` to select the eight-sector map; Boot remains the harness default. Startup derives sector count from the host, and restart preserves the pre-travel count rather than requiring Boot's three sectors. Other scenario geometry still uses HQ/sector-relative fixture offsets; selecting a map is not proof that every fixture fits it.

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

The production slice also requires every peer, including the remote client, to observe the first producer's `forceNumber` as 1 and the second as 2, with their respective groups carrying matching 1/2 numbers. The probe exposes `forceNumber` on both buildings and groups; this asserts socket-delivered actor state, not replication-property flags. It does not prove native teammate badge rendering.

`economy` recruits the two-unit siege force first. The force naturally captures sector 0 (front assigned to the replicated site position) without income; a paid outpost placed at an offset from that site establishes it after real departure. Workshop research is paid/once-only and scoped. Actual targeted HQ weapon damage plus an explicit finish fixture reaches Victory, not a native Q command or unaided match.

`restart` recruits one unit, buys research and finishes by fixture, then requests the connected restart. The host must first expose the fresh world before autonomous play (`isolate_fresh_host`); every peer must then converge on one predicate that includes the reset itself: new generation, same commander slot, all players present, result 0, both HQs at 900, no team-0 armies or buildings, doctrine 0 and wallet ≥ 600 for every player, and the same sector count as before travel, all neutral and at zero progress. Afterwards each peer's GameState id must differ from the old one on the same net driver. Isolation, income pause, funding and lethal setup are recorded fixtures. Nothing here proves five-player coordination, all-wallet natural income, every doctrine, delayed-peer three-cycle restart, fault recovery or arbitrary rejoin.

Pending reports print, per peer, every building's `constructionProgress`, `joined`/`travelling` and `productionState` plus each reinforcing unit's owner/army/slot/position, so a stalled predicate can be classified from the console before opening `responses.jsonl`. Map points in the scripts are offsets from `friendlyHQPosition`, `enemyHQPosition` and `sites[].position`; a regenerated map changes no script literal.

Require a PASS record, per-peer `responses.jsonl` and game logs, independent replicated convergence, net-mode/peer identity and pidfd cleanup. The coordinator fails terminal network/travel/contradictory state, owned process exit and artifact mutation. No command deadline or wall-clock predicate timeout: reports of an unchanged pending predicate require immediate inspection of peer snapshots and exact condition. A living process or new `observe` reply is not progress. Interrupt the exact owned runner via cleanup on impossible/stalled state; retain failed evidence. Build/package only after all source handoffs; never mutate the artifact during a run or terminate a user's process.

## Steam runtime smoke and pending acceptance

Steam host/invite/join is implemented with test **App ID 480**. Fresh editor, Development and Shipping builds passed in `Saved/Verification/round2/RESULTS.md`; the archives are `Builds/Linux` and directly `Builds/LinuxShipping`, not a nested Shipping/Linux directory. The enabled plugins are `OnlineSubsystemSteam` and modern `SteamSockets`; the driver is `/Script/SteamSockets.SteamSocketsNetDriver`, not the legacy Steam IP driver. Existing IP evidence is not Steam evidence.

`Saved/Verification/steam/RESULTS.md` records Development initialization, a GameSession lobby with **5 public / 0 private connections including host**, AvailabilityZone listen/SteamSockets on 7777, **Hosting on Steam / players 1/5**, and an inspected **Choose Friend to Invite** dialog (no invitations sent). Native Enter after a controlled defeat created a fresh GameState on the same driver without lobby recreation; confirmed Leave/Quit logged destruction before menu/exit. Shipping hosting/dialog, natural-defeat Enter restart with retained 1/5, Leave/Quit and `-nosteam` solo passed visually. Shipping `USE_LOGGING_IN_SHIPPING=0` prevents exact session/driver/destruction-log claims.

**Backend/input gap:** normal Development shell hosting initialized Steam but did not show the requested dialog. Wayland with manual renderer preload showed it but native input did not dismiss it. X11/preload showed and dismissed it, but native Play Again/Enter restart input failed; direct Development controller restart still travelled. Native Wayland/no-preload Shipping Enter restart passed. No tested configuration proves usable overlay and restart input together; check a real Steam-managed launch before claiming that. Same-account loopback was rejected by SteamSockets' local-identity check, not a successful remote join.

Run authorized builds and each runtime check under the shared `flock /tmp/cooprts-work/ue.lock` window. Use two machines with distinct logged-in Steam accounts that are friends and identical copies of the **entire fresh Development archive (`Builds/Linux`)**, not merely the same source/version or executable. A single account's local loopback processes do not establish Steam invitation/Internet proof. Enable the Steam overlay and use rendered windows. Launch on each machine without `-nosteam`:

```bash
./Builds/Linux/CoopRTS.sh -windowed -ResX=1280 -ResY=720 -log \
  -LogCmds="LogOnline Verbose,LogOnlineSession Verbose,LogSteamShared Verbose,LogNet Verbose,LogSockets Verbose"
```

For the recorded overlay-visible shell configuration, use the installed renderer (the path is workstation-specific):

```bash
SteamAppId=480 SteamGameId=480 SDL_VIDEODRIVER=x11 \
LD_PRELOAD="$HOME/.local/share/Steam/ubuntu12_64/gameoverlayrenderer.so" \
./Builds/Linux/CoopRTS/Binaries/Linux/CoopRTS /Game/Maps/Menu \
  -windowed -ResX=1600 -ResY=900 -log -stdout -FullStdOutLogOutput
```

Shipping shell smoke supplied `SteamAppId=480 SteamGameId=480` without a staged App ID file or archive modification; the executable is `Builds/LinuxShipping/CoopRTS/Binaries/Linux/CoopRTS-Linux-Shipping`. This is test setup, not release identity/distribution strategy. A Shipping release needs the project's real App ID defined via `ProjectDefinitions`/`UE_PROJECT_STEAMSHIPPINGID` in `Source/CoopRTS.Target.cs` and distribution/launch through Steam; the target currently leaves that definition unset (engine default `0`) and remains unchanged pending a real App ID. Shipping uses that compiled ID for its relaunch check, not the development `SteamDevAppId`/`bRelaunchInSteam` settings, and does not automatically write `steam_appid.txt`. Standard helpers target Development and require logs/probes unavailable in ordinary Shipping; prior Shipping checks used removed disposable adapters preserving artifact/ownership/focus guards with inspected surface readiness.

The following is the **remaining acceptance checklist**, not a claim that distinct-account joining or WAN has passed. Reuse recorded host-only results only for their unchanged artifact/path; verify both remote join routes and both restart inputs:

1. **Steam initialization:** require `[AppId: 480] Client API initialized 1` and verbose `SteamAPI initialized`. In Linux Development, the engine creates `Binaries/Linux/steam_appid.txt` with `480` before initialization and removes it at shutdown; see the README for writable-directory/editor details. Preserve failures such as `Failed to create steam_appid.txt` or `SteamAPI failed to initialize`, rather than claiming initialization from a loaded plugin alone.
2. **Host:** choose **Host co-op**. Require `FOnlineAsyncTaskSteamCreateLobby bWasSuccessful: 1 LobbyId: <nonzero>` and `CoopSteam: lobby created public=5; opening AvailabilityZone listen`, followed by the gameplay world and SteamSockets listening/connection logs. Observe the HUD's hosting/player count out of 5. Check the five-player total cap and sixth-player rejection in separately authorized topology acceptance; two peers do not prove the cap.
3. **Overlay and joins:** open the hosted match menu and choose **Invite friends**. Require `CoopSteam: invite overlay requested`, then actually inspect the visible Steam invitation overlay. Verbose `FOnlineAsyncEventSteamExternalUITriggered bIsActive: 1`/`0` records opening/closing, not usability by itself. On the other account, accept an invitation and separately exercise Steam friends-list **Join Game**; require `CoopSteam: lobby joined; ClientTravel <resolved address>`, an accepted SteamSockets connection, and independently observed client gameplay/player-count convergence. Re-enter from the menu between join routes rather than counting one join twice.
4. **Retained restart and teardown:** reach an actual result, then use Play Again/terminal Enter and observe a fresh match on both peers with the same lobby/session and retained connections. A Steam restart must not create/destroy the lobby; the IP `restart` slice cannot prove this Steam path. Confirm client Leave, host Leave and confirmed Quit: `CoopSteam: destroying session` then `CoopSteam: session destroyed`, menu/exit after cleanup, and appropriate remote disconnect handling. Exercise unavailable/expired invitations or a real join failure and inspect the menu's reason, not just an error log.
5. **No-Steam behavior and IP fallback:** on a separate run with Steam stopped or logged out, inspect unavailable host/invite feedback and start **Play vs JEV** offline. Then, with local Steam running, use the exact README direct-IP host/client commands with `-nosteam` on both. Expect `Steam subsystem has been disabled by command line (-nosteam)`, `SteamSockets: Disabled due to no Steam OSS running.`, and `IpNetDriver listening on port 7777`. The automatic IP fallback applies when the Steam driver is unavailable, not to an active Steam join/relay failure. Run all four `ownership`, `production`, `economy`, `restart` slices above individually on the fresh editor artifact, then required packaged slices; inspect each result before the next run.

Retain per-machine logs, commands/artifact identity, overlay/menu screenshots and observed join/restart/teardown results. Linux does not emit the explicit `SteamSockets: Initializing Network Relay` message, so do not require it. Enabled relay settings or a LAN Steam join do not establish WAN/NAT traversal; use a real Internet-separated session for that claim. Human five-player coordination and long-session stability remain separate acceptance work.

### Development-only session failure fixtures

`CoopSteam.Verify` is compiled under `!UE_BUILD_SHIPPING`. Run only in an owned local verification process under the shared lock, never a public match. Host co-op first; `state` logs hosting/busy/named-session/CanHost/status. The other commands deliberately exercise failure handling:

- `CoopSteam.Verify checksum`: broadcasts `NetChecksumMismatch` through the engine with the actual host world/driver. The lobby and listen world must remain; state must report host=1, session=1, busy=0.
- `CoopSteam.Verify invite-refusal`: on a solo Steam host, temporarily spawns one PlayerState roster entry, invokes the real invite callback with valid session/user data, then destroys that entry. Inspect “Cannot join an invitation while hosting other players. Leave the match first.” and retained hosting. This proves the roster guard, not a remote invitation.
- `CoopSteam.Verify reject`: requires exactly one hosted player. Reuses the host lobby snapshot, removes only the local named-session record, then calls the real `JoinSession` → `OnJoined` path. The fixture overrides transport to unused loopback `127.0.0.1:1` and sets the actual pending handshake's `ConnectionError` to `Match full (developer verification)`; the engine broadcasts its real null-world `PendingConnectionFailure`. Require destruction logs, visible Menu reason, `state` reporting session=0/busy=0/canHost=1, and a new successful Host click. Steam self-identity transport is intentionally avoided: the initial local-listener cancellation attempt crashed inside SteamSockets.

These fixtures do not prove server PreLogin rejection, distinct-account Steam transport, remote retention or WAN. Before/after evidence and fresh-build limits are recorded under “Round 2 fixes” in `Saved/Verification/steam/RESULTS.md`.

## Native input is a separate proof layer

`network_desktop.py` retains per-peer identity, focus, freshness and capture/input guards for packaged windows (`--clients 1|4`). Inspect each viewport, click building choices/ground, wait for completion, configure production/fronts and observe real squads. Its driver still accepts `tab`, `h`, `r`, `q` for explicit no-op/removal checks; those keys no longer bind squad controls in the game. Space focuses building/HQ. Use persistent Construction to reopen choices and the minimap to pan. The socket scenario's internal order/targeted-attack RPCs are fixture/API proof, not current human controls. No 1/2, F1/F2/F3 or N driver inputs exist.

`network_desktop.py` adds `-nosteam` to every host and client, keeping the native recipe on plain IP with Steam running. The switch belongs to the launched game command, not the Python CLI. Inspect native input separately; a headless `network.py` PASS does not prove it.

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
