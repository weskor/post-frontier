#!/usr/bin/env python3
"""Drive only the packaged CoopRTS process owned by a recorded verification run."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[4]
SCRIPTS = Path(__file__).resolve().parent
BINARY = ROOT / "Builds/Linux/CoopRTS/Binaries/Linux/CoopRTS"
READY = "Bringing up level for play"


def execute(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, text=True, capture_output=True, **kwargs).stdout.strip()


def identity(pid):
    try:
        stat = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        if stat[0] == "Z":
            return None
        return {"start": stat[19], "exe": str(Path(f"/proc/{pid}/exe").resolve(strict=True))}
    except (FileNotFoundError, ProcessLookupError):
        return None


def record(run, kind, **data):
    with (run / "actions.jsonl").open("a") as out:
        out.write(json.dumps({"time": datetime.datetime.now(datetime.timezone.utc).isoformat(), "kind": kind, **data}) + "\n")


# Staleness guard limit: only these directories/suffixes are compared against the artifact
# mtimes. Edits elsewhere (plugins, Build/ scripts, engine, generated code) are not detected,
# and an mtime newer than the artifact is a heuristic, not proof the artifact differs.
SOURCE_SUFFIXES = {".h", ".cpp", ".cs"}
COOKED = (("Config", {".ini"}), ("Content", {".uasset", ".umap"}))


def stale_file(directory, suffixes, limit):
    for root, dirs, files in os.walk(ROOT / directory):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for name in files:
            if os.path.splitext(name)[1] in suffixes:
                path = Path(root, name)
                if path.stat().st_mtime_ns > limit:
                    return path.relative_to(ROOT)
    return None


def package_stamp():
    if not BINARY.is_file():
        raise RuntimeError("Package missing. Follow README Linux build/package commands first.")
    packages = list((ROOT / "Builds/Linux/CoopRTS/Content/Paks").glob("*.pak"))
    if not packages:
        raise RuntimeError("Packaged content missing")
    compiled = BINARY.stat().st_mtime_ns
    cooked = min(p.stat().st_mtime_ns for p in packages)
    for directory, suffixes, limit in (("Source", SOURCE_SUFFIXES, compiled), *((d, s, cooked) for d, s in COOKED)):
        if newer := stale_file(directory, suffixes, limit):
            raise RuntimeError(f"Package may be stale: {newer} is newer. Rebuild/package first.")
    if (ROOT / "CoopRTS.uproject").stat().st_mtime_ns > min(compiled, cooked):
        raise RuntimeError("Project descriptor is newer than package; rebuild first")
    return {str(p.relative_to(ROOT)): [p.stat().st_size, p.stat().st_mtime_ns] for p in [BINARY, *packages]}


def editor_stamp():
    module = ROOT / "Binaries/Linux/libUnrealEditor-CoopRTS.so"
    if not module.is_file():
        raise RuntimeError("Editor module missing. Build CoopRTSEditor using the README command first.")
    compiled = module.stat().st_mtime_ns
    # Editor runs load uncooked content, so only compiled sources can invalidate the module.
    if newer := stale_file("Source", SOURCE_SUFFIXES, compiled):
        raise RuntimeError(f"Editor module may be stale: {newer} is newer. Build CoopRTSEditor first.")
    if (ROOT / "CoopRTS.uproject").stat().st_mtime_ns > compiled:
        raise RuntimeError("Project descriptor is newer than editor module; build CoopRTSEditor first.")
    return {str(module.relative_to(ROOT)): [module.stat().st_size, compiled]}


def state(run):
    return json.loads((run / "session.json").read_text())


def doctor(run, focused=False):
    session = state(run)
    if identity(session["pid"]) != session["identity"]:
        raise RuntimeError("Owned game is not running with its recorded identity; do not drive another instance")
    if package_stamp() != session["package"]:
        raise RuntimeError("Package changed during session; stop and launch a fresh run")
    log = (run / "game.log").read_text(errors="replace") if (run / "game.log").exists() else ""
    if READY not in log or "5.8.3" not in log:
        raise RuntimeError("Expected UE 5.8.3 game readiness not found in this run's log")
    windows = json.loads(execute(["hyprctl", "clients", "-j"]))
    owned = [w for w in windows if w["pid"] == session["pid"] and w.get("mapped")]
    if len(owned) != 1:
        raise RuntimeError("Expected exactly one mapped window for the owned game")
    window = owned[0]
    if focused and json.loads(execute(["hyprctl", "activewindow", "-j"])).get("pid") != session["pid"]:
        raise RuntimeError("Owned game is not focused. Focus that window explicitly; no input sent.")
    return {"pid": session["pid"], "window": window, "monitors": json.loads(execute(["hyprctl", "monitors", "-j"]))}


def stop(run):
    session = state(run)
    pid = session["pid"]
    if identity(pid) != session["identity"]:
        record(run, "cleanup", result="owned process already absent; no signal sent")
        return
    fd = os.pidfd_open(pid)
    try:
        if identity(pid) != session["identity"]:
            raise RuntimeError("PID identity changed before cleanup; no signal sent")
        signal.pidfd_send_signal(fd, signal.SIGTERM)
        deadline = time.monotonic() + 15
        while identity(pid) == session["identity"] and time.monotonic() < deadline:
            time.sleep(.1)
        if identity(pid) == session["identity"]:
            signal.pidfd_send_signal(fd, signal.SIGKILL)
            record(run, "cleanup", result="owned process required SIGKILL")
        else:
            record(run, "cleanup", result="owned process exited after SIGTERM")
    finally:
        os.close(fd)
    print(f"Stopped owned instance only. Evidence preserved: {run}")


def launch(run):
    stamp = package_stamp()
    for program in ("hyprctl", "wtype", "grim", "cc", "pkg-config"):
        if not shutil.which(program):
            raise RuntimeError(f"Missing dependency: {program}")
    execute(["hyprctl", "monitors", "-j"])
    run.mkdir(parents=True, exist_ok=False)
    command = [str(BINARY), "CoopRTS", "-windowed", "-ResX=1600", "-ResY=900", "-log", "-stdout", "-FullStdOutLogOutput", f"-abslog={run / 'game.log'}"]
    flags = shlex.split(execute(["pkg-config", "--cflags", "--libs", "wayland-client"]))
    execute(["cc", "-Wall", "-Wextra", "-Werror", SCRIPTS / "pointer.c", "-o", run / "pointer", *flags])
    with (run / "stdout.log").open("w") as out:
        process = subprocess.Popen(command, cwd=ROOT / "Builds/Linux", stdout=out, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            current = identity(process.pid)
            if current and current["exe"] == str(BINARY):
                break
            if process.poll() is not None:
                raise RuntimeError("Game exited during launch; inspect stdout.log")
            time.sleep(.05)
        else:
            raise RuntimeError("Could not establish game process identity")
        (run / "session.json").write_text(json.dumps({"pid": process.pid, "identity": current, "package": stamp, "command": command}, indent=2))
        record(run, "launch", command=command)
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError("Game exited before readiness; inspect evidence logs")
            try:
                report = doctor(run)
                print(json.dumps(report, indent=2))
                return
            except RuntimeError:
                time.sleep(.25)
        raise RuntimeError("Game did not become ready within 90 seconds")
    except BaseException:
        if (run / "session.json").exists():
            stop(run)
        elif process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
        raise


def capture(run, label):
    if not re.fullmatch(r"[A-Za-z0-9_-]+", label):
        raise RuntimeError("Screenshot label must contain only letters, numbers, underscores or hyphens")
    target = run / f"{label}.png"
    if target.exists():
        raise RuntimeError("Evidence label already exists; use a new label")
    report = doctor(run, focused=True)
    report["cursor"] = json.loads(execute(["hyprctl", "cursorpos", "-j"]))
    execute(["grim", "-c", target])
    (run / f"{label}.json").write_text(json.dumps(report, indent=2))
    record(run, "capture", path=str(target))
    print(target)


def drive(run, args):
    report = doctor(run, focused=True)
    record(run, "input", action=args.command, parameters={k: v for k, v in vars(args).items() if k != "run"})
    if args.command == "key":
        key = {"enter": "Return", "tab": "Tab", "escape": "Escape", "f4": "F4"}.get(args.key, args.key)
        if args.hold:
            execute(["wtype", "-P", key, "-s", str(args.hold), "-p", key])
        else:
            execute(["wtype", "-k", key])
    elif args.command == "move":
        execute([run / "pointer", "move", str(args.dx), str(args.dy)])
    else:
        if args.command == "point" or not args.here:
            window = report["window"]
            x = int(window["at"][0] + window["size"][0] * args.x)
            y = int(window["at"][1] + window["size"][1] * args.y)
            execute(["hyprctl", "dispatch", f"hl.dsp.cursor.move({{x={x},y={y}}})"])
            doctor(run, focused=True)

        if args.command == "point":
            command = ["scroll", "0"]  # Motion without a button or wheel tick.
        elif args.command == "click":
            command = ["click", "272" if args.button == "left" else "273"]
        elif args.command == "scroll":
            command = ["scroll", str(args.steps)]
        else:
            command = ["drag", str(args.dx), str(args.dy)]
        execute([run / "pointer", *command])
    record(run, "input-complete", action=args.command)


# scenario: (automation filter, evidence prefix). A filter without a leaf name (rules) runs
# every test beneath it in one editor process; every reported result must be Success.
SCENARIOS = {
    "rules": ("CoopRTS.Rules", "rules"),
    "orders": ("CoopRTS.Orders.ReplaceHoldRetreat", "regression"),
    "movement": ("CoopRTS.Movement.TwoGroups", "movement"),
    "combat": ("CoopRTS.Combat.Encounter", "combat"),
    "construction": ("CoopRTS.Construction.Lifecycle", "construction"),
    "production": ("CoopRTS.Construction.Production", "production"),
    "strategy": ("CoopRTS.Enemy.ConstructionEconomy", "strategy"),
    "match-win": ("CoopRTS.Match.VictoryRestart", "match-win"),
    "match-loss": ("CoopRTS.Match.DefeatRestart", "match-loss"),
    "doctrine-siege": ("CoopRTS.Doctrine.SiegeOptics", "doctrine-siege"),
    "doctrine-repairs": ("CoopRTS.Doctrine.FieldRepairs", "doctrine-repairs"),
    "doctrine-frontline": ("CoopRTS.Doctrine.EntrenchedFrontline", "doctrine-frontline"),
    "doctrine-restart": ("CoopRTS.Doctrine.Restart", "doctrine-restart"),
}


def completed_tests(text, test):
    """(result, path) for every automation completion at or beneath the requested filter."""
    return [(result, path) for result, path in re.findall(r"Test Completed\. Result=\{(\w+)\}.*?Path=\{([^}]*)\}", text)
            if path == test or path.startswith(test + ".")]


def regression(run, scenario):
    editor = editor_stamp()
    run.mkdir(parents=True, exist_ok=True)
    test, prefix = SCENARIOS[scenario]
    log = run / f"{prefix}.log"
    if log.exists():
        raise RuntimeError("Regression evidence already exists; use a fresh --run directory")
    engine = Path(os.environ.get("UE_ROOT", str(Path.home() / ".local/opt/unreal-engine/5.8.3")))
    command = [str(engine / "Engine/Binaries/Linux/UnrealEditor"), str(ROOT / "CoopRTS.uproject"),
               "/Game/Maps/Boot", "-game", "-nullrhi", "-nosound", "-unattended",
               f"-ExecCmds=Automation RunTests {test}; SoftQuit", f"-abslog={log}", "-stdout"]
    record(run, "regression", command=command, editor=editor)
    with (run / f"{prefix}-stdout.log").open("w") as out:
        result = subprocess.run(command, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT)
    if editor_stamp() != editor:
        raise RuntimeError("Editor module changed during regression; rerun after the build finishes in a fresh evidence directory.")
    text = log.read_text(errors="replace")
    completed = completed_tests(text, test)
    failed = sorted({path for outcome, path in completed if outcome != "Success"})
    passed = (result.returncode == 0 and bool(completed) and not failed
              and "**** TEST COMPLETE. EXIT CODE: 0 ****" in text)
    record(run, "regression-result", test=test, passed=passed, exit_code=result.returncode,
           completed=sorted({path for _, path in completed}), failed=failed)
    if not passed:
        raise RuntimeError(f"Regression did not report explicit Success for every test under {test}"
                           f" (completed {len(completed)}, failed {failed}): {log}")
    print(f"PASS: {test} ({len(completed)} test result(s)); evidence: {log}")


def fraction(value):
    number = float(value)
    if not .05 <= number <= .95:
        raise argparse.ArgumentTypeError("Use a window fraction between .05 and .95")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True, type=Path, help="Unique evidence directory, normally Saved/Verification/<name>")
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("launch", "doctor", "focus", "stop"):
        commands.add_parser(name)
    regression_parser = commands.add_parser(
        "regression", help="run one editor automation scenario headlessly (-nullrhi); evidence is the abslog",
        description="Editor-module automation. 'rules' runs every CoopRTS.Rules.* deterministic rule test in one "
                    "process and fails on any Result={Fail}; it proves rule precedence only, never navigation, "
                    "replication or rendering. World scenarios run one latent test each on /Game/Maps/Boot.")
    regression_parser.add_argument("--scenario", choices=list(SCENARIOS), default="construction",
                                   help="rules: all CoopRTS.Rules tests in one process; others: one world test (default construction)")
    snap = commands.add_parser("capture"); snap.add_argument("label")
    key = commands.add_parser("key"); key.add_argument("key", choices=["w", "a", "s", "d", "h", "r", "q", "tab", "space", "enter", "escape", "f4"])
    key.add_argument("--hold", type=int, choices=range(0, 2001), default=0, metavar="0..2000", help="Hold milliseconds, zero taps")
    for name in ("click", "scroll", "drag", "move", "point"):
        sub = commands.add_parser(name)
        if name != "move":
            sub.add_argument("--x", type=fraction, default=.5); sub.add_argument("--y", type=fraction, default=.5)
            if name != "point":
                sub.add_argument("--here", action="store_true", help="Use the current pointer position without warping")
        if name == "click":
            sub.add_argument("button", choices=["left", "right"])
        elif name == "scroll":
            sub.add_argument("steps", type=int, choices=range(-12, 13), metavar="-12..12", help="Negative zooms in, positive zooms out")
        elif name in ("drag", "move"):
            sub.add_argument("dx", type=int, choices=range(-200, 201), metavar="-200..200")
            sub.add_argument("dy", type=int, choices=range(-200, 201), metavar="-200..200")
    args = parser.parse_args()
    run = args.run.resolve()
    if args.command == "launch": launch(run)
    elif args.command == "doctor": print(json.dumps(doctor(run), indent=2))
    elif args.command == "focus":
        report = doctor(run)
        address = report["window"]["address"]
        execute(["hyprctl", "dispatch", f'hl.dsp.focus({{window="address:{address}"}})'])
        doctor(run, focused=True)
        record(run, "focus", address=address)
    elif args.command == "stop": stop(run)
    elif args.command == "capture": capture(run, args.label)
    elif args.command == "regression": regression(run, args.scenario)
    else: drive(run, args)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(f"Verification blocked: {error}", file=sys.stderr)
        sys.exit(1)
