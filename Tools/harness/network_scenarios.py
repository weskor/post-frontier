"""Independent network slices and the continuous construction acceptance chain."""

from __future__ import annotations

from harness.network import NetworkRun
from harness.network_economy import expand_and_research
from harness.network_outcomes import finish, research, restart_and_converge
from harness.network_ownership import reject_locked_commands
from harness.network_pause import pause_scenario
from harness.network_pause_hud import pause_hud_scenario
from harness.network_production import produce_and_replace
from harness.network_session import build_barracks, configure_siege, connect, recruit


def ownership_scenario(run: NetworkRun) -> None:
    s = connect(run)
    index = build_barracks(run, s)
    reject_locked_commands(run, s, index, configure_siege(run, s, index))


def production_scenario(run: NetworkRun) -> None:
    s = connect(run)
    index = build_barracks(run, s)
    produce_and_replace(run, s, index, configure_siege(run, s, index))


def economy_scenario(run: NetworkRun) -> None:
    s = connect(run)
    index = build_barracks(run, s)
    squad = configure_siege(run, s, index)
    recruit(run, s, index, 1, "first siege recruit joins on all peers")
    recruit(run, s, index, 2, "second siege recruit joins on all peers")
    expand_and_research(run, s, index, squad)


def restart_scenario(run: NetworkRun) -> None:
    s = connect(run)
    index = build_barracks(run, s)
    squad = configure_siege(run, s, index)
    recruit(run, s, index, 1, "one siege recruit joins on all peers")
    research(run, s)
    restart_and_converge(
        run, s, finish(run, s, squad, "fixture victory replicates before restart")
    )


def construction_scenario(run: NetworkRun) -> None:
    """Acceptance chain: every slice in one connected session and one continuous world."""
    s = connect(run)
    index = build_barracks(run, s)
    squad = configure_siege(run, s, index)
    reject_locked_commands(run, s, index, squad)
    produce_and_replace(run, s, index, squad)
    restart_and_converge(run, s, expand_and_research(run, s, index, squad))


SCENARIOS = {
    "pause": (
        pause_scenario,
        "remote P freezes simulation, shared countdown, host early resume, spent pause rejection",
    ),
    "pause-hud": (
        pause_hud_scenario,
        "offscreen paused HUD, shared countdown progression and spent action captures",
    ),
    "ownership": (
        ownership_scenario,
        "paid placement, rejected duplicate/foreign commands, siege fee, locked-type rejections",
    ),
    "production": (
        production_scenario,
        "per-unit debit, physical travel/arrival, second force, casualty replacement",
    ),
    "economy": (
        economy_scenario,
        "polygon capture, builder/JEV-only finite extractors, depletion/freeing, research, HQ damage and victory",
    ),
    "restart": (
        restart_scenario,
        "connected restart converging on a fully reset world with preserved sockets",
    ),
    "construction": (
        construction_scenario,
        "acceptance chain: ownership, production, economy and restart in one world",
    ),
}
