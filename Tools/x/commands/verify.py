"""The only executable entry point for slow verification harnesses."""

import argparse
import importlib
from pathlib import Path
import sys
from types import ModuleType

from x.building import ensure_editor
from x.context import Context
from x.verifying.artifacts import package_snapshot
from x.verifying.cleanup import cleanup
from x.verifying.sessions import lease

NAME = "verify"
SUMMARY = "Run network, offscreen HUD or owned native/desktop verification."
HELP = """./x verify network|hud|native|desktop --help

Use ./x check for scoped change proof. Run slow extras only when the task brief
names the required contract/surface. Before launching, state the changed behavior,
smallest supported scenario, observable pass condition and remaining limits.
Run one selected scenario, inspect its result, then add another only for a distinct
contract. Full acceptance is separately requested, not an ordinary development gate.
Never weaken assertions or change product behavior to make a run pass.

Network:
./x verify network --mode editor --clients 1 --scenario ownership
Choose the requested ownership, production, economy or restart slice; each starts
fresh host/client worlds. Construction is the default continuous acceptance chain,
not a requirement after every change. Use host plus one remote for ordinary
replicated proof; --clients 0 or 4 and --emulation belong only to requested topology/
fault checks. Emulation requires observed native PktLag=120/PktLoss=8 on every peer.
Editor mode ensures a fresh editor build. Packaged mode requires ./x package.
If the current packaged -nullrhi launch actually fails, retain that failure and
try a new ./x verify network invocation with --rendered; the socket assertions
are unchanged and Vulkan windows alone add no visual proof.
This proves only the selected loopback contract through independent peer states.
Fixtures/funded budgets are controlled setup, not unaided play. These peers use
-nosteam: no OS input, rendering, Steam, WAN, human coordination, arbitrary rejoin
or delayed-peer multi-cycle acceptance follows from a slice or chain PASS.

HUD:
./x verify hud --mode editor --quick deck
./x verify hud --mode packaged --res 1600x900 --res 1280x720
Quick captures only the deck and one completed selected-barracks inspector at
the first resolution. It does not prove production, fronts, research, minimap
dispatch or victory. The full run exercises its controlled HUD state sequence
at the first resolution; additional --res values capture the selected barracks,
not the full sequence again. Inspect the actual images for any visual claim.
Offscreen shared hit geometry/controller clicks are not OS/compositor input,
focus, readable native windows or unrequested resolutions.

Owned packaged surface:
./x verify native launch
./x verify native doctor
./x verify native focus
./x verify native capture baseline
./x verify native key w --hold 350
./x verify native capture after-pan
./x verify native stop
For remote input/HUD use ./x verify desktop launch --clients 1, then doctor,
focus, capture, key, point or click with --peer host|c1 (four remotes only when
requested), and ./x verify desktop stop. Desktop uses -nosteam, not Steam.
Native/desktop require a fresh Development package, UE 5.8.3 readiness, Hyprland's
Lua dispatch API, wlr virtual-pointer support, wtype, grim with cursor capture,
cc, pkg-config and Wayland client headers/library. They do not install dependencies
or support arbitrary compositors. Shipping lacks the required ordinary logs/
probes; use ./x help play for manual Shipping/Steam surface checks instead.
Launch --map accepts a /Game/... package path without extension or URL options;
network/HUD accept --map too. Boot is the harness default. Shape validation is
not existence proof: a missing/unstarted requested map fails, and selecting one
level supplies no evidence for another.

Doctor checks recorded process identity, package hashes, requested-map readiness
and owned window geometry/scale; it cannot detect a logically wedged UI. Run it
for each instance and after surprises. Inputs/captures recheck ownership and focus;
explicitly focus the verified window, never bypass the guard or adopt another game.
Use inspected HUD/ground targets and current logical-window fractions (.05..95),
not copied screen coordinates. Correlate input with actual resulting state, not
just key delivery or accepted-order logs. For affected placement/production/goals,
selection, research, combat or restart, inspect the relevant before/after transition;
numeric invariants stay in assertions. Do not replay unrelated feature recipes.
For camera/cursor changes, native provides drag, scroll, move and --here actions:
positive scroll zooms out, negative zooms in; unwarped relative move and --here
checks ordinary pointer delivery, not just targeted cursor warps. Keep motion
inside the observed window and inspect the cursor itself. Coordinate metadata
does not prove cursor visibility. Temporal effects need an inspected sequence,
not absence in one frame. Native/desktop are manual observations, not automatic
gameplay/replication assertions or human playtesting.

Supervision and cleanup:
./x owns the headless/exclusive locks. Native/desktop keep an exclusive lease
across commands until stop/owned-game exit; a background or idle session still
blocks that resource. Stop after the last action or a failed iteration, before
unrelated investigation/build/package work. Never kill a user's editor/game.
The automatic watchdog stops the child process group after the configured interval
without watched-log growth, marks the run stalled and retains evidence. Failed/
interrupted network/HUD runs and failed launches also clean up recorded children.
This detects silence, not semantic stalls: repeated pending reports/observe replies
can keep the log growing forever. Inspect the exact predicate and peer snapshots
when state stops advancing; living PIDs/request IDs are not meaningful progress.
Distinguish loading/replication convergence from completed-state assertions. If the
predicate is impossible, a terminal error appears or progress cannot be established,
interrupt the owned running invocation through its cleanup path. For persistent
native/desktop sessions explicitly stop as well; between actions there is no
gameplay-progress watchdog. Confirm owned children stopped and the lease released.
Do not add elapsed-time success criteria, longer travel waits or unchanged retries.
Resume only with a diagnosis and a targeted next check; partial chains are not PASS.
Cleanup uses recorded identities/pidfds, not process names, and retains evidence.
Every invocation has a run record; session actions append to the launch evidence.
Full-output captures may include unrelated desktop content. Keep them local,
crop copies for sharing and preserve the original evidence.
Use ./x runs <id> to inspect results/logs/artifacts. Cite exact asserted/inspected
contracts and exclusions; old evidence cannot prove changed gameplay. For Steam
acceptance and Development session-failure maintenance use ./x help play.
"""
RECORD = True
SCRIPTS = {
    "network": "network",
    "hud": "hud_capture",
    "native": "verify",
    "desktop": "network_desktop",
}
LIMITS = {
    "network": "Replicated slice over loopback; not OS input, rendering, Steam or WAN.",
    "hud": "Offscreen HUD/hit geometry; not OS input, focus or unrequested resolutions.",
    "native": "Owned packaged input and captures; not automatic gameplay assertions, replication or WAN.",
    "desktop": "Owned host/client input and captures; not automatic replicated assertions, Steam, WAN or human coordination.",
}


def harness_module(name: str) -> ModuleType:
    directory = Path(__file__).resolve().parents[2] / "harness"
    sys.path.insert(0, str(directory))
    try:
        return importlib.import_module(SCRIPTS[name])
    finally:
        sys.path.remove(str(directory))


def configure(parser: argparse.ArgumentParser) -> None:
    commands = parser.add_subparsers(dest="harness", required=True)
    for name in SCRIPTS:
        child = commands.add_parser(
            name,
            help=LIMITS[name],
            description=LIMITS[name],
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        harness_module(name).configure(child)
        child.epilog = (child.epilog or "") + "\n" + LIMITS[name]


def execute(args: argparse.Namespace, ctx: Context, folder: Path) -> int:
    if ctx.run is None:
        raise RuntimeError("verification requires a recorded command")
    argv = ctx.run.record["argv"][2:]
    script = ctx.repo / "Tools/harness" / f"{SCRIPTS[args.harness]}.py"
    code = 1
    try:
        code = ctx.exec(
            [sys.executable, script, *argv],
            log=f"verify-{args.harness}",
            env={"PYTHONPATH": str(ctx.repo / "Tools"), "X_HARNESS_DIR": str(folder)},
        )
    finally:
        action = getattr(args, "command", getattr(args, "action", ""))
        if code and (args.harness in ("network", "hud") or action == "launch"):
            cleanup(folder)
    ctx.run.add_artifact(folder, f"{args.harness} evidence")
    ctx.run.add_result(f"verify-{args.harness}", code == 0)
    return code


def run(args: argparse.Namespace, ctx: Context) -> int:
    if ctx.run is None:
        raise RuntimeError("verification requires a recorded command")
    folder = ctx.run.dir / "harness"
    if args.harness in ("native", "desktop"):
        action = args.command if args.harness == "native" else args.action
        if action == "launch":
            package_snapshot(ctx.repo)
        with lease(ctx, args.harness, action, folder) as session:
            return execute(args, ctx, session)
    if args.mode == "editor" and not ensure_editor(ctx):
        return 1
    exclusive = args.harness == "network" and args.clients > 0
    with ctx.locks.exclusive() if exclusive else ctx.locks.headless():
        if args.mode == "packaged":
            package_snapshot(ctx.repo)
        return execute(args, ctx, folder)
