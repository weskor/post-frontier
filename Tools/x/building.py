"""The sole editor-target build path for all Unreal-launching commands."""

from pathlib import Path

from x.context import Context


def editor_module(ctx: Context) -> Path:
    return (
        ctx.repo
        / "Binaries/Linux"
        / f"libUnrealEditor-{Path(ctx.settings.project).stem}.so"
    )


def ensure_editor(ctx: Context) -> bool:
    if ctx.run is None:
        raise RuntimeError("editor builds require a recorded command")
    with ctx.locks.headless():
        if editor_module(ctx).is_file() and ctx.freshness.is_fresh("editor"):
            print("editor up to date", flush=True)
            return True
        before = ctx.freshness.current_hash("editor")
        code = ctx.exec(
            [
                ctx.settings.engine_root / "Engine/Build/BatchFiles/Linux/Build.sh",
                ctx.settings.editor_target,
                "Linux",
                "Development",
                f"-Project={ctx.repo / ctx.settings.project}",
                "-WaitMutex",
            ],
            log="build-editor",
        )
        ok = code == 0 and editor_module(ctx).is_file()
        if ctx.freshness.current_hash("editor") != before:
            ok = False
            print("editor inputs changed during build; no freshness stamp written")
        if ok:
            ctx.freshness.stamp("editor", ctx.run.id, before)
        elif code != 0 or not editor_module(ctx).is_file():
            print(f"editor build failed; inspect {ctx.run.dir / 'build-editor.log'}")
        ctx.run.add_result("build-editor", ok)
        return ok
