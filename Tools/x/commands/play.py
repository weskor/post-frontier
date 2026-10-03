"""Launch a content-hash-verified package while holding the exclusive lock."""

import argparse
import os
from pathlib import Path
import sys

from x import jsonio
from x.content.packages import latest_package
from x.context import Context

NAME = "play"
SUMMARY = "Launch the latest fresh package, offline by default."
HELP = """./x play [--shipping] [--smoke] [--map /Game/Maps/Boot] [--steam] [-- extra]

Launch the latest content-hash-fresh package while holding the exclusive lock
until the game exits. Missing/stale packages refuse with run ./x package;
use ./x package shipping for Shipping. No argument selects the packaged startup
map; --map selects an explicit level. Never mutate a running artifact or stop a
user's game to clear a blocker. Use ./x verify native|desktop for guarded automated
input/capture on Development, and ./x check for change proof.
Without --steam, -nosteam disables Steam. Extra arguments can select window size,
listen/client URLs, logging or an explicit SDL backend:
./x play --map /Game/Maps/Boot -- -nullrhi -ExecCmds=Quit
./x play -- /Game/Maps/Boot?listen -windowed -ResX=1280 -ResY=720
./x play -- 127.0.0.1:7777 -windowed -ResX=1280 -ResY=720
Each command holds its local exclusive lock, so direct-IP remote play needs a
separate workstation/lock domain; use ./x verify network|desktop for local peers.
IP proof does not prove Steam. A successful launch or clean exit is not an
automatic gameplay/input/visual acceptance result.

Smoke tiers (same command for Development and Shipping):
./x play --smoke --map /Game/Maps/Boot -- -nullrhi
./x play --smoke --shipping --map /Game/Maps/Boot -- -nullrhi
--smoke explicitly launches --map (or settings.default_map when omitted) under
the same exclusive lock and retains game.log, engine stdout and smoke.json.
Development waits at most 60 seconds for the engine's exact requested LoadMap
completion log, sends owned SIGTERM, then waits at most 15 seconds for exit 0
or Linux 143 plus the normal engine LogExit: Exiting. shutdown log.
Unrelated log growth does not extend the deadline; missing logs fail closed.
Shipping ordinary logs are compiled out, so its distinct tier requires the
process to stay alive 20 seconds without a crash report or crash-handler output,
then sends owned SIGTERM and requires exactly exit 143 within 15 seconds.
Exit 143 is accepted only after this runner actually sends SIGTERM; any early
exit, other Shipping exit, crash signature/report or timeout fails. Crash
evidence is checked through shutdown, not just before the signal.
Shipping proves bounded liveness/no observed crash, NOT map-loaded readiness,
gameplay, input or rendering. Development logs do not prove those surfaces either.
Smoke rejects extra -ExecCmds arguments (case-insensitive, both -ExecCmds=commands
and -ExecCmds commands); normal play still permits them. Smoke never uses engine
test hooks and never targets another game.

Steam setup and manual acceptance (only when requested):
./x play --steam -- -windowed -ResX=1280 -ResY=720 -log
./x play --steam -- "-LogCmds=LogOnline Verbose,LogOnlineSession Verbose,LogSteamShared Verbose,LogNet Verbose,LogSockets Verbose"
--steam supplies test SteamAppId=480/SteamGameId=480 and preloads this workstation's
~/.local/share/Steam/ubuntu12_64/gameoverlayrenderer.so. Log in to Steam and enable
the overlay first. Explicit SDL experiments belong in the launch environment
(for example SDL_VIDEODRIVER=x11 ./x play --steam), not engine arguments.
Overlay preload/backend choices do not prove usable overlay or restart input.
For remote acceptance use two machines with distinct logged-in friends accounts
and identical complete fresh package contents, not just matching executables.
Same-account loopback is not Steam invitation/Internet proof.

1. In Development inspect actual Steam client initialization, not just loaded
   plugins. Enable verbose logging with an extra -LogCmds argument as needed.
   Preserve initialization/App ID file failures; do not mutate the package.
2. Host co-op: inspect successful lobby creation, the requested listen world,
   SteamSockets connection/listening and the HUD hosting/player count out of five.
3. Open Invite friends and inspect a usable overlay, including dismissal.
   On the other account independently exercise invite acceptance and friends-list
   Join Game, returning to the menu between routes. Require accepted transport
   plus visible gameplay/player-count convergence; an overlay-open log is not a join.
4. Reach a result and exercise Play Again and terminal Enter separately. Observe
   fresh matches on both peers with retained lobby/connections, not recreated hosting.
   Confirm client Leave, host Leave and confirmed Quit, appropriate remote disconnect
   handling, and Development session-destruction logs before menu/exit. Inspect
   unavailable/expired invite or actual join-failure feedback as a distinct case.
5. In a separate launch with Steam stopped/logged out inspect unavailable host/
   invite feedback and Play vs JEV offline. Default ./x play also tests explicit
   -nosteam behavior; use the IP commands above with Steam running for IP fallback.
   Requested socket assertions remain ./x verify network slices, never Steam proof.

Use ./x play --shipping --steam for manual Shipping observations, not normal
Development log/probe assertions. App ID 480 is test setup, not release identity
or Steam distribution proof. Ordinary Shipping logging is compiled out: cite
inspected surface transitions, never invented session/driver/destruction logs.
Neither shell hosting nor a LAN Steam join proves Steam-managed launch, WAN/NAT/
relay, five-player coordination, sixth-player rejection or long-session stability.
Those require their own requested real topology/session and observations.

Development-only Steam session maintenance:
In an owned local Development game launched by ./x play --steam, host co-op,
then enter CoopSteam.Verify state in the game console. Never use fixtures on a
public match or in Shipping. Only when diagnosing the corresponding failure:
- CoopSteam.Verify checksum exercises the engine checksum failure; inspect retained
  lobby/listen world and host=1/session=1/busy=0.
- CoopSteam.Verify invite-refusal on a solo host briefly adds a roster entry;
  inspect the hosting-others refusal and retained host. This is not a remote invite.
- CoopSteam.Verify reject requires a solo host, joins its actual lobby, then
  injects a pending handshake rejection via unused loopback. Inspect destruction,
  the visible Menu reason, session=0/busy=0/canHost=1 and a new successful Host.
These fixtures do not prove server PreLogin rejection or distinct-account transport.

Supervise actual gameplay progress: the runner stops silent or repeat-only watched
logs after the configured stall interval, ignoring timestamps and polling counters.
Novel log lines do not prove usable input or advancing state. Verification harness
predicates have separate deadlines; manual play has no predicate assertions.
Interrupt only this owned run if wedged; never bypass assertions or restart unchanged
failures. Inspect ./x runs <id> for retained launch/log evidence and report only the
surfaces actually observed.
"""
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--shipping", action="store_true")
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--map", dest="map")
    parser.add_argument("--steam", action="store_true")
    parser.add_argument("extra", nargs=argparse.REMAINDER)


def run(args: argparse.Namespace, ctx: Context) -> int:
    if ctx.run is None:
        raise RuntimeError("play requires a recorded run")
    config = "shipping" if args.shipping else "development"
    extra = list(args.extra)
    extra = extra[1:] if extra[:1] == ["--"] else extra
    if args.smoke and any(
        value.partition("=")[0].lower() == "-execcmds" for value in extra
    ):
        raise ValueError(
            "--smoke rejects -ExecCmds; use normal play for console commands"
        )
    env = {}
    if args.steam:
        env = {
            "SteamAppId": "480",
            "SteamGameId": "480",
            "LD_PRELOAD": str(
                Path.home() / ".local/share/Steam/ubuntu12_64/gameoverlayrenderer.so"
            ),
        }
    with ctx.locks.exclusive():
        executable = latest_package(ctx.repo, config, ctx.settings.game_target)
        argv: list[str | Path] = [executable]
        if args.map or args.smoke:
            argv.append(args.map or ctx.settings.default_map)
        if not args.steam:
            argv.append("-nosteam")
        argv.extend([*extra, f"-abslog={ctx.run.dir / 'game.log'}"])
        if args.smoke:
            return smoke(
                ctx, argv, executable, args.map or ctx.settings.default_map, config, env
            )
        return ctx.exec(argv, log="play", env=env, cwd=executable.parent)


def smoke(
    ctx: Context,
    argv: list[str | Path],
    executable: Path,
    requested_map: str,
    config: str,
    env: dict[str, str],
) -> int:
    if ctx.run is None:
        raise RuntimeError("smoke requires a recorded run")
    folder = ctx.run.dir.resolve()
    report = folder / "smoke.json"
    game_log = folder / "game.log"
    argv[-1] = f"-abslog={game_log}"
    user_config = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
    crash_roots = [
        executable.parents[2] / "Saved/Crashes",
        user_config / "Epic" / ctx.settings.game_target / "Saved/Crashes",
    ]
    helper: list[str | Path] = [
        sys.executable,
        "-m",
        "x.play_smoke",
        "--map",
        requested_map,
        "--game-log",
        game_log,
        "--stdout-log",
        folder / "engine.log",
        "--report",
        report,
    ]
    if config == "shipping":
        helper.append("--shipping")
    for root in crash_roots:
        helper.extend(["--crash-dir", root])
    helper.extend(["--", *argv])
    code = 1
    try:
        code = ctx.exec(
            helper,
            log="play",
            cwd=executable.parent,
            env={**env, "PYTHONPATH": str(ctx.repo / "Tools")},
            stall_seconds=max(ctx.settings.stall_seconds, 90),
        )
    finally:
        record_smoke(ctx, folder, code)
    return code


def record_smoke(ctx: Context, folder: Path, code: int) -> None:
    if ctx.run is None:
        raise RuntimeError("smoke requires a recorded run")
    report = folder / "smoke.json"
    details = jsonio.load(report) if report.exists() else {}
    for path, label in [
        (report, "smoke lifecycle and raw engine exit"),
        (folder / "game.log", "engine game log"),
        (folder / "engine.log", "engine stdout/stderr"),
    ]:
        ctx.run.add_artifact(path, label)
    ctx.run.add_result(
        "play-smoke",
        code == 0 and bool(details.get("ok")),
        str(details.get("failure") or details),
        details.get("duration_s"),
    )
