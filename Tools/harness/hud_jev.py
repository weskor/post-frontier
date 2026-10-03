"""JEV intent display captures: timeline bar, region badge and memo feed against the published plans."""

from __future__ import annotations

from collections.abc import Sequence

from harness.hud_setup import boot
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import NetworkRun, region, require
from harness.verify import JsonObject

# Mirrors JevIntent::MemoVisible; the world test owns the exact rows, this checks the rendered host.
MEMO_ROWS = 3


def rects_overlap(a: Sequence[float], b: Sequence[float]) -> bool:
    return not (
        a[0] + a[2] <= b[0]
        or b[0] + b[2] <= a[0]
        or a[1] + a[3] <= b[1]
        or b[1] + b[3] <= a[1]
    )


def check_entries(state: JsonObject, label: str) -> None:
    plans = {plan["ticket"]: plan for plan in state["jevPlans"]}
    entries = state["jevIntent"]["entries"]
    require(
        sorted(entry["ticket"] for entry in entries) == sorted(plans),
        f"{label}: timeline tickets {[e['ticket'] for e in entries]} differ from published {sorted(plans)}",
    )
    for entry in entries:
        plan = plans[entry["ticket"]]
        fields = ("target", "sizeBand", "escalated", "forceNumber")
        require(
            all(entry[name] == plan[name] for name in fields),
            f"{label}: timeline entry {entry} differs from plan {plan}",
        )
        require(
            0 <= entry["seconds"] <= plan["eta"],
            f"{label}: countdown {entry['seconds']} outside 0..ETA {plan['eta']}",
        )
    seconds = [entry["seconds"] for entry in entries]
    require(seconds == sorted(seconds), f"{label}: timeline is not soonest first")


def check_badges(state: JsonObject, label: str) -> None:
    plans = state["jevPlans"]
    badges = {badge["region"]: badge for badge in state["jevIntent"]["badges"]}
    require(
        set(badges) == {plan["target"] for plan in plans},
        f"{label}: badge regions {sorted(badges)} differ from plan targets",
    )
    for plan in plans:
        require(
            not plan["escalated"] or badges[plan["target"]]["escalated"],
            f"{label}: escalated plan {plan['ticket']} has an unescalated badge",
        )


def check_panels(state: JsonObject, label: str) -> None:
    """Timeline bar and memo rows sit inside the viewport and never overlap each other."""
    intent = state["jevIntent"]
    timeline = intent.get("timelineRect")
    require(timeline is not None, f"{label}: no timeline bar while plans are published")
    memos = intent["memos"]
    require(0 < len(memos) <= MEMO_ROWS, f"{label}: {len(memos)} memo rows")
    rows = [timeline, *(memo["rect"] for memo in memos)]
    width, height = state["viewportWidth"], state["viewportHeight"]
    for index, rect in enumerate(rows):
        require(
            rect[0] >= 0
            and rect[1] >= 0
            and rect[0] + rect[2] <= width
            and rect[1] + rect[3] <= height,
            f"{label}: JEV panel {rect} leaves the {width}x{height} viewport",
        )
        for other in rows[index + 1 :]:
            require(
                not rects_overlap(rect, other),
                f"{label}: JEV panels {rect} and {other} overlap",
            )


def check_memos(state: JsonObject, label: str) -> None:
    memos = state["jevIntent"]["memos"]
    published = {plan["memo"] for plan in state["jevPlans"]}
    require(
        memos[0]["text"] in published,
        f"{label}: newest memo {memos[0]['text']!r} is not a published memo",
    )
    require(
        all(memo["text"] and memo["alpha"] > 0 for memo in memos),
        f"{label}: memo rows carry empty or invisible text",
    )


def check_display(state: JsonObject, label: str) -> None:
    """The HUD's own model, read back from the host, equals the replicated plans."""
    check_entries(state, label)
    check_badges(state, label)
    check_panels(state, label)
    check_memos(state, label)


def pan_to(capture: Capture, state: JsonObject, target: int) -> None:
    """Minimap click on the target region's anchor, then wait until its badge is on screen."""
    anchor = region(state, target)["anchor"]
    half = state["arenaHalfExtent"]
    capture.minimap(
        (anchor[1] + half[1]) / (2 * half[1]), (half[0] - anchor[0]) / (2 * half[0])
    )
    capture.wait(
        lambda s: any(
            b["region"] == target and b["onScreen"] for b in s["jevIntent"]["badges"]
        ),
        f"badge for region {target} on screen",
    )


def stage(run: NetworkRun, name: str, description: str) -> None:
    run.request("host", "jevPlans", stage=name)
    run.phase(f"JEV plan fixture: {description}")


def created(run: NetworkRun, capture: Capture) -> None:
    stage(run, "create", "force 1 attacks, force 2 moves and holds")
    state = capture.wait(
        lambda s: (
            len(s["jevIntent"]["entries"]) == 2 and len(s["jevIntent"]["memos"]) == 2
        ),
        "timeline and memo feed show both created plans",
    )
    check_display(state, "created")
    attack = next(plan for plan in state["jevPlans"] if plan["verb"] == 1)
    pan_to(capture, state, attack["target"])
    check_display(capture.state(), "created, panned")
    capture.shot("jev-intent-created")


def escalated(run: NetworkRun, capture: Capture) -> None:
    stage(run, "escalate", "force 2 defends its region")
    state = capture.wait(
        lambda s: (
            any(plan["escalated"] for plan in s["jevPlans"])
            and any(entry["escalated"] for entry in s["jevIntent"]["entries"])
        ),
        "timeline shows the escalated plan",
    )
    plan = next(plan for plan in state["jevPlans"] if plan["escalated"])
    require(
        "Escalated: defending" in plan["memo"],
        f"escalation memo does not name the defense: {plan['memo']!r}",
    )
    pan_to(capture, state, plan["target"])
    state = capture.state()
    check_display(state, "escalated")
    badge = next(
        b for b in state["jevIntent"]["badges"] if b["region"] == plan["target"]
    )
    require(
        badge["label"].startswith("JEV  Escalated: defending "),
        f"escalated badge reads {badge['label']!r}",
    )
    require(
        state["jevIntent"]["memos"][0]["text"] == plan["memo"],
        "escalation memo is not the newest row",
    )
    capture.shot("jev-intent-escalated")


def replaced(run: NetworkRun, capture: Capture) -> None:
    before = {plan["ticket"] for plan in capture.state()["jevPlans"]}
    stage(run, "replace", "force 1 is replaced by a new ticket on another region")
    state = capture.wait(
        lambda s: (
            {plan["ticket"] for plan in s["jevPlans"]} != before
            and {e["ticket"] for e in s["jevIntent"]["entries"]}
            == {plan["ticket"] for plan in s["jevPlans"]}
        ),
        "timeline follows the replacement ticket",
    )
    plan = next(plan for plan in state["jevPlans"] if plan["ticket"] not in before)
    pan_to(capture, state, plan["target"])
    state = capture.state()
    check_display(state, "replaced")
    require(
        state["jevIntent"]["memos"][0]["text"] == plan["memo"],
        "replacement memo is not the newest row",
    )
    capture.shot("jev-intent-replaced")


def jev_intent_scenario(run: NetworkRun, resolution: tuple[int, int]) -> None:
    capture = Capture(run)
    pid, _ = boot(run, capture, resolution)
    empty = capture.state()
    require(
        not empty["jevPlans"]
        and not empty["jevIntent"]["entries"]
        and "timelineRect" not in empty["jevIntent"],
        "JEV display shows entries before any plan is published",
    )
    capture.shot("jev-intent-empty")
    created(run, capture)
    escalated(run, capture)
    replaced(run, capture)
    no_compositor_windows(run, pid)
    run.event(
        "PASS", captures=capture.count, resolutions=[resolution], quick="jev-intent"
    )
