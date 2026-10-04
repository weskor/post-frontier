"""Rendered Team panel: opener, rows, toggle, presets, Send, a refusal, the log and the teammate card's Gift entry."""

from __future__ import annotations

from collections.abc import Sequence
from typing import cast

from harness.hud_force_bar import inspected_teammate
from harness.hud_force_bar_many import deselect
from harness.hud_setup import boot
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import NetworkRun, require
from harness.verify import JsonObject

ROW0 = 62
POWER = 65
DATA = 66
PRESET_FIRST = 67
PRESET_THIRD = 69
PRESET_ALL = 70
STEP_UP = 72
SEND = 73
LOG_DOWN = 75


def team(state: JsonObject) -> JsonObject:
    return cast(JsonObject, state["team"])


def viewport(run: NetworkRun, capture: Capture, width: int, height: int) -> None:
    run.request("host", "resolution", width=width, height=height)
    capture.wait(
        lambda s: (s["viewportWidth"], s["viewportHeight"]) == (width, height),
        f"Team panel viewport {width}x{height}",
    )
    # Resizing an offscreen viewport can strand its cursor on an edge and engage real edge-pan.
    run.request("host", "cursor", x=width / 2, y=height / 2)


def set_open(capture: Capture, wanted: bool) -> None:
    if team(capture.state())["open"] != wanted:
        capture.key("Tab")
    capture.wait(lambda s: team(s)["open"] == wanted, "Tab toggles the Team panel")


def teammates(state: JsonObject) -> list[int]:
    own = int(state["localIndex"])
    rows = team(state)["commanders"]
    return [int(c["index"]) for c in rows if c["team"] == 0 and c["index"] != own]


def gift_card(
    run: NetworkRun, capture: Capture, group: JsonObject, width: int, height: int
) -> None:
    """The teammate's read-only card offers Gift...; clicking it opens the panel with them chosen."""
    owner, number = group["owner"], group["forceNumber"]
    run.request("host", "select", target="force", owner=owner, number=number)
    capture.wait(
        lambda s: not s["selectedForces"],
        "the teammate force is inspected, not selected",
    )
    capture.shot(f"team-gift-card-{width}x{height}")
    run.request("host", "forceCard", owner=owner, number=number, control="gift")
    capture.wait(
        lambda s: team(s)["open"] and team(s)["teammate"] == owner,
        "Gift... opens the panel with the card's owner chosen",
    )
    capture.shot(f"team-gift-card-opens-panel-{width}x{height}")
    set_open(capture, False)
    deselect(run, capture)


def gift(run: NetworkRun, giver: int, to: int, resource: str, amount: int) -> None:
    """An authority gift between two commanders, funding the giver first."""
    run.request(
        "host",
        "giftHudGift",
        giver=giver,
        to=to,
        resource=resource,
        amount=amount,
        fund=True,
    )


def flow(run: NetworkRun, capture: Capture, own: int, width: int, height: int) -> None:
    suffix = f"{width}x{height}"
    mates = teammates(capture.state())
    require(len(mates) == 3, f"Team panel capture needs three teammates, found {mates}")
    run.request("host", "giftHudClear")
    run.request("host", "giftFund", owner=own, power=340, data=20)
    gift(run, mates[0], own, "power", 100)
    capture.wait(
        lambda s: team(s)["unseen"], "a gift to the commander marks the opener"
    )
    capture.shot(f"team-opener-unseen-dot-{suffix}")
    set_open(capture, True)
    run.request("host", "giftHudLayout")
    capture.shot(f"team-panel-rows-{suffix}")
    capture.hud(ROW0, "teammate row chooses C2")
    capture.wait(
        lambda s: team(s)["teammate"] == mates[0], "row click chooses the teammate"
    )
    capture.hud(DATA, "DATA toggle")
    capture.hud(PRESET_THIRD, "third Data preset, 50")
    capture.shot(f"team-panel-data-preset-{suffix}")
    capture.hud(SEND, "Send more Data than the wallet holds")
    capture.wait(
        lambda s: team(s)["refusal"] == "Not enough Data: you have 20",
        "a refused Send explains itself under the button",
    )
    capture.shot(f"team-panel-rejection-{suffix}")
    capture.hud(PRESET_FIRST, "first Data preset, 10")
    capture.hud(STEP_UP, "+ stepper")
    capture.wait(lambda s: team(s)["amount"] == 15, "Data steps by 5")
    capture.shot(f"team-panel-send-ready-{suffix}")
    capture.hud(SEND, "Send 15 Data")
    capture.wait(
        lambda s: len(team(s)["log"]) == 2 and team(s)["refusal"] == "",
        "Send moves the Data and logs the gift",
    )
    log_flow(run, capture, mates, own, suffix)


def log_flow(
    run: NetworkRun, capture: Capture, mates: list[int], own: int, suffix: str
) -> None:
    capture.hud(POWER, "POWER toggle")
    capture.hud(PRESET_THIRD, "Power 200")
    capture.hud(SEND, "Send 200 Power")
    capture.wait(lambda s: len(team(s)["log"]) == 3, "Send moves the Power")
    for sender, receiver in ((mates[1], mates[2]), (mates[2], own)):
        gift(run, sender, receiver, "data", 20)
    capture.wait(lambda s: len(team(s)["log"]) == 5, "teammate gifts reach the log")
    capture.shot(f"team-panel-log-{suffix}")
    capture.hud(LOG_DOWN, "older gifts")
    capture.wait(lambda s: team(s)["scroll"] == 1, "the down arrow scrolls the log")
    capture.shot(f"team-panel-log-scrolled-{suffix}")
    capture.key("Escape")
    capture.wait(lambda s: not team(s)["open"], "Esc closes the panel before the menu")
    capture.shot(f"team-panel-closed-{suffix}")


def scenario(run: NetworkRun, resolutions: Sequence[tuple[int, int]]) -> None:
    capture = Capture(run)
    pid, state = boot(run, capture, resolutions[0])
    own = int(state["localIndex"])
    run.request("host", "income", paused=True)
    # The inspected teammate fixture first: it takes the first free slot, the Team fixture then adds the rest.
    group = inspected_teammate(run.request("host", "forceCardTeammate"))
    deselect(run, capture)
    for width, height in resolutions:
        viewport(run, capture, width, height)
        run.request("host", "giftHudTeammates", count=3, power=240, data=20)
        capture.wait(
            lambda s: len(teammates(s)) == 3, "three teammates join the roster"
        )
        gift_card(run, capture, group, width, height)
        flow(run, capture, own, width, height)
    no_compositor_windows(run, pid)
    run.event("PASS", captures=capture.count, resolutions=resolutions, scenario="team")
