#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = ["numpy==2.5.3", "scipy==1.18.1", "soundfile==0.14.0", "pedalboard==0.9.25", "pyloudnorm==0.2.0"]
# ///
"""Build sound sets from recorded layers, mixed and rendered in REAPER (Docs/Audio.md).

    ./x gen fetch-audio-sources                        # once: recordings into Saved/AudioSources/
    ./x gen generate-unit-audio                       # every recipe: render, write Art/Audio
    ./x gen generate-unit-audio -- --unit Human_Lancer --unit Machine_Scrambler

Recipes live in Build/unit_audio/<faction>_<role>.py (SOURCES + UNIT); shared code in unit_audio/core.py.
Lancer and Scrambler each supply Fire (4), Impact (3) and Death (3), like the ranged units.
Lancer also supplies faction-shared ShieldBreak (3); Scrambler supplies auto-cast Pulse (3).
The four keys are Human_Lancer, Machine_Lancer, Human_Scrambler and Machine_Scrambler.
Selection/acknowledgement cues remain in the shared UI recipe. Recipe discovery is automatic.
Three stages per unit:
1. Prepare: cut single events out of the source recordings (onset detection), convert to 48 kHz mono,
   apply the recipe's pitch/filter/trim and write one clip per layer to Saved/AudioLayers/<Unit>/.
2. Session: Art/Audio/Sessions/<Unit>.rpp holds one track per layer (the mix balance lives in the track
   faders) and one region per output sound. The script writes a session only when it is missing or with
   --new-session, so balance, timing and FX changed by hand in REAPER survive reruns.
3. Render: `reaper -renderproject` renders every region; one-shots are trimmed, limited and matched to
   their event's loudness target. Loops are crossfaded without limiting to preserve the wrap boundary.
   Outputs are 48 kHz, 24-bit mono WAVs with true peak <= -1 dBFS, plus a listening reel per sound set.

Deterministic: fixed seeds and fixed source events, so reruns of an unchanged session give identical files.
Separate units may render concurrently (one process per --unit).
"""

import argparse
import shutil

from unit_audio import recipes
from unit_audio.core import ROOT, SESSIONS, prepare, render, write_session


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--unit",
        action="append",
        metavar="FACTION_ROLE",
        help="only this unit, e.g. Human_Lancer or Machine_Scrambler (repeatable); default: every recipe",
    )
    parser.add_argument(
        "--new-session",
        action="store_true",
        help="rewrite the REAPER sessions from the recipes, discarding manual changes",
    )
    args = parser.parse_args()
    if not shutil.which("reaper"):
        raise SystemExit("reaper not found on PATH (sudo pacman -S reaper)")
    total = 0
    for unit in [module.UNIT for module in recipes(args.unit)]:
        placed = prepare(unit)
        session = SESSIONS / f"{unit.key}.rpp"
        if args.new_session or not session.exists():
            write_session(unit, placed, session)
            print(f"wrote session {session.relative_to(ROOT)}")
        total += render(unit, session)
    print(f"UNIT_AUDIO_RENDERED {total}")


if __name__ == "__main__":
    main()
