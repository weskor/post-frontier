"""Real runner processes must yield scopes, reserve peers and protect modules."""

from collections.abc import Iterator
from dataclasses import replace
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from typing import Any

import pytest
from test_locks import Workers, eventually
from x import jsonio
from x.building import editor_module
from x.context import Context
from x.settings import load

DRIVER = """
from dataclasses import replace
from pathlib import Path
import sys, time
from x.context import Context
from x.runs import Run
from x.settings import load
from x.testing import run_scopes
from x.commands import gen, verify
import argparse

class ProbeContext(Context):
    def exec(self, argv, **kwargs):
        if kwargs.get('watch') is not None:
            argv = [sys.executable, root / 'editor-probe.py', *argv[1:]]
        elif kwargs['log'] == 'generate' or kwargs['log'].startswith('verify-'):
            argv = [sys.executable, root / 'peer-probe.py']
        return super().exec(argv, **kwargs)

repo, root, token, mode, slots, peers = sys.argv[1:]
repo, root = Path(repo), Path(root)
settings = replace(load(repo), lock_dir=root / 'locks', runs_root=root / 'runs',
                   engine_root=root / 'engine', headless_pool_size=int(slots))
record = Run(repo, settings.runs_root, mode, [mode])
ctx = ProbeContext(repo, settings, record, mode)
if mode in ('scopes', 'scripts'):
    names = ['one', 'two', 'three'] if mode == 'scopes' else ['script']
    code = 0 if run_scopes(ctx, names) else 1
elif mode == 'gen':
    code = gen.run(argparse.Namespace(name='generate-command-map', extra=[],
                                     list=False, describe=False), ctx)
elif mode == 'verify':
    code = verify.run(argparse.Namespace(harness='network', mode='editor', clients=1), ctx)
else:
    manager = ctx.locks.headless(int(peers)) if mode == 'headless' else getattr(ctx.locks, mode)()
    with manager:
        (root / (token + '.entered')).write_text(record.id)
        while not (root / (token + '.release')).exists():
            time.sleep(0.01)
    code = 0
record.finish(code)
(root / (token + '.finished')).write_text(record.id)
sys.exit(code)
"""

EDITOR = """
from pathlib import Path
import os, sys, time
root = Path(os.environ['X_PROBE_ROOT'])
command = next(arg for arg in sys.argv if arg.startswith('-ExecCmds='))
scope = command.split('RunTests Probe.')[1].split(';')[0]
token = os.environ['X_PROBE_TOKEN'] + '.' + scope
(root / (token + '.entered')).touch()
while not (root / (token + '.release')).exists():
    time.sleep(0.01)
log = Path(next(arg.removeprefix('-abslog=') for arg in sys.argv if arg.startswith('-abslog=')))
log.write_text('Bringing World /Game/Maps/Boot.Boot up for play\\n'
               + 'Test Completed. Result={Success} Path={Probe.' + scope + '}\\n'
               + '**** TEST COMPLETE. EXIT CODE: 0 ****\\n')
"""


class Probes(Workers):
    def start(
        self, token: str, mode: str, slots: int = 1, peers: int = 1
    ) -> subprocess.Popen[bytes]:
        env = {
            **os.environ,
            "PYTHONPATH": str(Path(__file__).parents[1]),
            "X_PROBE_ROOT": str(self.root),
            "X_PROBE_TOKEN": token,
        }
        with (self.root / f"{token}.log").open("wb") as log:
            child = subprocess.Popen(
                [
                    sys.executable,
                    "-c",
                    DRIVER,
                    str(self.repo),
                    str(self.root),
                    token,
                    mode,
                    str(slots),
                    str(peers),
                ],
                stdout=log,
                stderr=log,
                env=env,
            )
        self.children.append(child)
        return child

    def log(self, token: str) -> str:
        return super().log(token.split(".")[0])

    def queued_scopes(self, token: str, count: int) -> None:
        eventually(
            lambda: (
                sum(
                    line.startswith("waiting for")
                    and ("pool-" in line or "turnstile.lock" in line)
                    for line in self.log(token).splitlines()
                )
                >= count
            ),
            f"{token} did not queue scope {count + 1}",
        )

    def scope_release(self, token: str) -> None:
        (self.root / f"{token}.release").touch()

    def finish_scopes(
        self, token: str, child: subprocess.Popen[bytes]
    ) -> dict[str, Any]:
        assert child.wait(timeout=8) == 0, self.log(token)
        run_id = (self.root / f"{token}.finished").read_text()
        return jsonio.load(self.root / "runs" / run_id / "record.json")


@pytest.fixture
def probes(repo: Path, tmp_path: Path) -> Iterator[Probes]:
    ctx = Context(repo, replace(load(repo), lock_dir=tmp_path / "locks"))
    module = editor_module(ctx)
    module.parent.mkdir(parents=True)
    module.write_bytes(b"isolated module")
    ctx.freshness.stamp("editor", "probe")
    (repo / "Tools/x/scopes.toml").write_text(
        "".join(
            f'[scopes.{name}]\nkind="automation"\nfilter="Probe.{name}"\nmap="default"\n'
            for name in ("one", "two", "three")
        )
        + "[paths]\n"
    )
    (tmp_path / "editor-probe.py").write_text(EDITOR)
    (tmp_path / "peer-probe.py").write_text(
        "from pathlib import Path\n"
        "import os, time\n"
        "root = Path(os.environ['X_PROBE_ROOT'])\n"
        "token = os.environ['X_PROBE_TOKEN']\n"
        "(root / (token + '.entered')).touch()\n"
        "while not (root / (token + '.release')).exists():\n"
        "    time.sleep(0.01)\n"
        "(Path(os.environ['X_RUN_DIR']) / 'unreal.log').write_text('completed')\n"
    )
    pool = Probes(repo, tmp_path)
    yield pool
    for child in pool.children:
        if child.poll() is None:
            # Runner children are owned process groups; release their barriers first.
            for token in ("a", "b"):
                for scope in ("one", "two", "three"):
                    pool.scope_release(f"{token}.{scope}")
            try:
                child.wait(timeout=8)
            except subprocess.TimeoutExpired:
                child.kill()
        child.wait(timeout=8)


def test_multi_scope_invocations_interleave_and_record_each_lease(
    probes: Probes,
) -> None:
    a = probes.start("a", "scopes")
    probes.await_entry("a.one")
    b = probes.start("b", "scopes")
    probes.waiting("b", "pool-0.lock")
    probes.scope_release("a.one")
    probes.await_entry("b.one")
    probes.queued_scopes("a", 1)
    blocked_at = time.monotonic()
    time.sleep(0.05)
    blocked_until = time.monotonic()
    probes.scope_release("b.one")
    probes.await_entry("a.two")
    probes.queued_scopes("b", 2)
    probes.scope_release("a.two")
    probes.await_entry("b.two")
    probes.queued_scopes("a", 2)
    probes.scope_release("b.two")
    probes.await_entry("a.three")
    probes.queued_scopes("b", 3)
    probes.scope_release("a.three")
    probes.await_entry("b.three")
    probes.scope_release("b.three")
    second_scope_wait = next(
        row["duration_s"]
        for row in probes.finish_scopes("a", a)["lock_waits"]
        if row["lock"] == "scope:two:headless"
    )
    assert second_scope_wait >= blocked_until - blocked_at
    for token, child in [("a", a), ("b", b)]:
        record = probes.finish_scopes(token, child)
        leases = [
            row for row in record["lock_waits"] if row["lock"].startswith("scope:")
        ]
        assert [row["lock"] for row in leases] == [
            f"scope:{name}:headless" for name in ("one", "two", "three")
        ]


def test_exclusive_waiter_enters_between_scopes(probes: Probes) -> None:
    a = probes.start("a", "scopes")
    probes.await_entry("a.one")
    writer = probes.start("writer", "exclusive")
    probes.waiting("writer", "ue.lock")
    probes.scope_release("a.one")
    probes.await_entry("writer")
    assert not probes.entered("a.two")
    probes.release("writer", writer)
    for name in ("two", "three"):
        probes.await_entry(f"a.{name}")
        probes.scope_release(f"a.{name}")
    probes.finish_scopes("a", a)


@pytest.mark.parametrize("peers", [2, 5])
def test_peer_slots_acquired_together_without_partial_reservation(
    probes: Probes, peers: int
) -> None:
    reader = probes.start("reader", "headless", slots=2)
    probes.await_entry("reader")
    network = probes.start("network", "headless", slots=2, peers=peers)
    probes.waiting("network", "pool-0.lock, pool-1.lock")
    assert len(list((probes.root / "locks").glob("pool-*.holder.json"))) == 1
    # A partial reservation must not retain the free slot while waiting for reader.
    assert (
        subprocess.run(
            ["flock", "-w", "2", str(probes.root / "locks/pool-1.lock"), "true"],
            check=False,
        ).returncode
        == 0
    )
    later = probes.start("later", "headless", slots=2)
    probes.waiting("later", "turnstile.lock")
    probes.release("reader", reader)
    probes.await_entry("network")
    assert len(list((probes.root / "locks").glob("pool-*.holder.json"))) == 2
    for index in range(2):
        assert (
            subprocess.run(
                ["flock", "-n", str(probes.root / f"locks/pool-{index}.lock"), "true"],
                check=False,
            ).returncode
            == 1
        )
    assert not probes.entered("later")
    probes.release("network", network)
    probes.await_entry("later")
    probes.release("later", later)


def test_fifo_waiters_and_killed_queue_head(probes: Probes) -> None:
    holder = probes.start("holder", "exclusive")
    probes.await_entry("holder")
    first = probes.start("first", "headless")
    probes.waiting("first", "ue.lock")
    dead = probes.start("dead", "exclusive")
    probes.waiting("dead", "turnstile.lock")
    second = probes.start("second", "exclusive")
    probes.waiting("second", "turnstile.lock")
    third = probes.start("third", "headless")
    probes.waiting("third", "turnstile.lock")
    dead.kill()
    dead.wait(timeout=8)
    probes.release("holder", holder)
    probes.await_entry("first")
    assert not probes.entered("second") and not probes.entered("third")
    probes.release("first", first)
    probes.await_entry("second")
    assert not probes.entered("third")
    probes.release("second", second)
    probes.await_entry("third")
    probes.release("third", third)
    assert not list((probes.root / "locks").glob("queue-*/*.ticket"))


def test_same_worktree_build_cannot_replace_running_scope_module(
    probes: Probes,
) -> None:
    a = probes.start("a", "scopes")
    probes.await_entry("a.one")
    builder = probes.start("builder", "build", slots=2)
    eventually(
        lambda: "waiting for module-" in probes.log("builder"),
        "same-worktree build did not wait for the running scope",
    )
    assert not probes.entered("builder")
    probes.scope_release("a.one")
    probes.await_entry("builder")
    assert not probes.entered("a.two")
    probes.release("builder", builder)
    for name in ("two", "three"):
        probes.await_entry(f"a.{name}")
        probes.scope_release(f"a.{name}")
    probes.finish_scopes("a", a)


def test_non_unreal_scope_runs_while_exclusive_is_held(probes: Probes) -> None:
    scopes = probes.repo / "Tools/x/scopes.toml"
    with scopes.open("a") as stream:
        stream.write(
            '\n[scopes.script]\nkind="script"\ncommands=[['
            + ",".join(
                json.dumps(argument)
                for argument in (
                    sys.executable,
                    "-c",
                    'from pathlib import Path; import sys; Path(sys.argv[1]).write_text("ran")',
                    str(probes.root / "script-proof"),
                )
            )
            + "]]\n"
        )
    holder = probes.start("holder", "exclusive")
    probes.await_entry("holder")
    child = probes.start("script", "scripts")
    record = probes.finish_scopes("script", child)
    assert (probes.root / "script-proof").read_text() == "ran"
    assert record["lock_waits"] == []
    assert holder.poll() is None
    probes.release("holder", holder)


def test_same_worktree_module_wait_does_not_block_other_worktree_build(
    probes: Probes,
) -> None:
    other_repo = probes.root / "other-worktree"
    shutil.copytree(probes.repo, other_repo)
    other = Probes(other_repo, probes.root, probes.children)
    a = probes.start("a", "scopes", slots=2)
    probes.await_entry("a.one")
    local_build = probes.start("local", "build", slots=2)
    eventually(
        lambda: "waiting for module-" in probes.log("local"),
        "local build did not wait for the active scope",
    )
    foreign_build = other.start("foreign", "build", slots=2)
    probes.await_entry("foreign")
    assert not probes.entered("local")
    probes.release("foreign", foreign_build)
    probes.scope_release("a.one")
    probes.await_entry("local")
    probes.release("local", local_build)
    for name in ("two", "three"):
        probes.await_entry(f"a.{name}")
        probes.scope_release(f"a.{name}")
    probes.finish_scopes("a", a)


@pytest.mark.parametrize("mode", ["gen", "verify"])
def test_editor_consumers_exclude_same_worktree_build(
    probes: Probes, mode: str
) -> None:
    consumer = probes.start("consumer", mode, slots=2)
    probes.await_entry("consumer")
    builder = probes.start("builder", "build", slots=2)
    eventually(
        lambda: "waiting for module-" in probes.log("builder"),
        f"build did not wait for {mode}'s module reader",
    )
    assert not probes.entered("builder")
    probes.release("consumer", consumer)
    probes.await_entry("builder")
    probes.release("builder", builder)
