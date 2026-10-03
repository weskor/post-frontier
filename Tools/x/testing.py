"""Run every selected scope and require explicit automation evidence."""

from collections.abc import Sequence
from concurrent.futures import Future, ThreadPoolExecutor, wait
from dataclasses import dataclass
from pathlib import Path
import re
import time

from x.building import editor_module, ensure_editor
from x.context import HEADLESS_UNREAL_ENV, Context
from x.process import CANCELLED
from x.scopes import Scope, ScopeMap, load

COMPLETE = "**** TEST COMPLETE. EXIT CODE: 0 ****"


@dataclass(frozen=True)
class AutomationResult:
    ok: bool
    completed: tuple[str, ...]
    failed: tuple[str, ...]
    details: str


def parse_automation(
    text: str, test: str, map_path: str, exit_code: int
) -> AutomationResult:
    results = [
        (outcome, path)
        for outcome, path in re.findall(
            r"Test Completed\. Result=\{(\w+)\}[^\n]*?Path=\{([^}]*)\}", text
        )
        if path == test or path.startswith(test + ".")
    ]
    completed = tuple(sorted({path for _, path in results}))
    failed = tuple(sorted({path for outcome, path in results if outcome != "Success"}))
    problems = []
    if exit_code != 0:
        problems.append(f"process exit {exit_code}")
    world = f"{map_path}.{map_path.rsplit('/', 1)[1]}"
    if f"Bringing World {world} up for play" not in text:
        problems.append(f"requested map {map_path} did not start")
    if not results:
        problems.append(f"zero tests completed under {test}")
    if failed:
        problems.append(f"non-Success results: {', '.join(failed)}")
    if COMPLETE not in text:
        problems.append("missing zero-exit completion marker")
    details = (
        "; ".join(problems) if problems else f"{test}: {len(completed)} tests Success"
    )
    return AutomationResult(not problems, completed, failed, details)


def _automation(
    ctx: Context, name: str, scope: Scope, map_override: str | None = None
) -> tuple[bool, str]:
    if ctx.run is None:
        raise RuntimeError("automation requires a recorded command")
    map_path = map_override or (
        ctx.settings.default_map if scope.map == "default" else scope.map
    )
    log = ctx.run.dir / f"{name}-unreal.log"
    with ctx.locks.module(), ctx.locks.headless(label=f"scope:{name}:headless"):
        before = editor_module(ctx).stat()
        inputs = ctx.freshness.current_hash("editor")
        if not ctx.freshness.is_fresh("editor"):
            return False, "editor inputs changed after build; rerun tests"
        code = ctx.exec(
            [
                ctx.settings.engine_root / "Engine/Binaries/Linux/UnrealEditor",
                ctx.repo / ctx.settings.project,
                map_path,
                "-game",
                "-nullrhi",
                # Null RHI assertions need neither shader compilation nor loading.
                "-NoShaderCompile",
                # Editor Python start-up scripts are outside C++ automation.
                "-DisablePython",
                "-nosound",
                "-unattended",
                # Uncapped 1/60 s fixed step: worlds simulate faster than real
                # time. Scenario waits use the world clock; deadlines stay wall.
                "-UseFixedTimeStep",
                "-FPS=60",
                # UE's Now command clears the 5 s discovery delay, not readiness.
                f"-ExecCmds=Automation Now; RunTests {scope.filter}; SoftQuit",
                f"-abslog={log}",
                "-stdout",
            ],
            log=f"{name}-stdout",
            env=HEADLESS_UNREAL_ENV,
            watch=log,
        )
        inputs_after = ctx.freshness.current_hash("editor")
        ctx.run.add_exec_inputs(f"{name}-stdout", "editor", inputs, inputs_after)
        text = log.read_text(errors="replace") if log.exists() else ""
        result = parse_automation(text, scope.filter, map_path, code)
        ctx.run.add_artifact(log, f"{name} automation log")
        after = editor_module(ctx).stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            return False, "editor module changed during automation"
        if inputs != inputs_after:
            return False, "editor inputs changed during automation"
        return result.ok, result.details


def _image_stamps(directory: Path) -> dict[Path, tuple[int, int, int]]:
    stamps = {}
    for path in directory.rglob("*"):
        if path.suffix in (".png", ".svg") and path.is_file():
            stat = path.stat()
            stamps[path] = (stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns)
    return stamps


def _scripts(ctx: Context, name: str, scope: Scope) -> tuple[bool, str]:
    if ctx.run is None:
        raise RuntimeError("validators require a recorded command")
    before = _image_stamps(ctx.run.dir)
    failures = []
    for index, command in enumerate(scope.commands):
        argv = [argument.format(run=ctx.run.dir) for argument in command]
        label = f"{name}-{index}-{Path(argv[1]).stem}"
        code = ctx.exec(argv, log=label, env={"PYTHONDONTWRITEBYTECODE": "1"})
        if code:
            failures.append(
                f"{argv[1]}: exit {code}; inspect {ctx.run.dir / (label + '.log')}"
            )
    for artifact, stamp in sorted(_image_stamps(ctx.run.dir).items()):
        if before.get(artifact) != stamp:
            ctx.run.add_artifact(artifact, f"{name} validator output")
    return not failures, "; ".join(failures) or "all map validators passed"


def _run_scope(
    ctx: Context,
    mapping: ScopeMap,
    name: str,
    editor_ok: bool,
    map_path: str | None,
) -> bool:
    if ctx.run is None:
        raise RuntimeError("tests require a recorded command")
    started = time.monotonic()
    scope = mapping.definitions[name]
    source_before = ctx.run.snapshot(f"scope:{name}:before")
    with ctx.run.collecting_execs() as execs:
        if scope.kind == "automation":
            ok, details = (
                _automation(ctx, name, scope, map_path)
                if editor_ok
                else (False, "editor build failed")
            )
        elif scope.kind == "pytest":
            code = ctx.exec(
                ["uv", "run", "--locked", "pytest", "-n", "auto", *scope.paths],
                log=name,
            )
            ok, details = code == 0, f"pytest exit {code}"
        elif scope.kind == "script":
            ok, details = _scripts(ctx, name, scope)
        else:
            ok, details = True, "no tests; lint is enforced by ./x check"
    source_after = ctx.run.snapshot(f"scope:{name}:after")
    provenance = ctx.run.source_interval(source_before, source_after)
    provenance["execs"] = execs
    ctx.run.add_result(
        name, ok, details, time.monotonic() - started, provenance=provenance
    )
    print(f"{'PASS' if ok else 'FAIL'} {name}: {details}", flush=True)
    return ok


def run_scopes(
    ctx: Context, scopes: Sequence[str], *, map_path: str | None = None
) -> bool:
    """Run scopes on one worker thread per headless slot; each scope leases its own.

    Each worker takes its next scope only after finishing one, so other runs'
    FIFO tickets interleave between this run's scopes.
    """
    if ctx.run is None:
        raise RuntimeError("tests require a recorded command")
    mapping = load(ctx.repo)
    names = list(dict.fromkeys(scopes))
    unknown = sorted(set(names) - set(mapping.names()))
    if unknown:
        raise ValueError(f"unknown scopes: {', '.join(unknown)}; use ./x test --list")
    editor_ok = True
    if any(mapping.definitions[name].kind == "automation" for name in names):
        editor_ok = ensure_editor(ctx)
    futures: list[Future[bool]] = []
    try:
        with ThreadPoolExecutor(max_workers=ctx.settings.headless_pool_size) as pool:
            futures = [
                pool.submit(_run_scope, ctx, mapping, name, editor_ok, map_path)
                for name in names
            ]
            try:
                return all([future.result() for future in futures])
            except BaseException:
                # Stop queued scopes and make running children and lock waits
                # unwind as interrupted before re-raising the first failure.
                CANCELLED.set()
                for future in futures:
                    future.cancel()
                wait(futures)
                raise
    finally:
        CANCELLED.clear()
