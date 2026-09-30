#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "scipy", "soundfile", "pedalboard", "pyloudnorm"]
# ///
"""Build unit one-shots from recorded layers, mixed and rendered in REAPER (Docs/Audio.md).

    uv run Build/FetchAudioSources.py                   # once: recorded sources into Saved/AudioSources/
    uv run Build/GenerateUnitAudio.py                   # every unit: prepare layers, render, write Art/Audio
    uv run Build/GenerateUnitAudio.py --unit Human_Ranged --new-session

Recipes live in Build/unit_audio/<faction>_<role>.py (SOURCES + UNIT); shared code in unit_audio/core.py.
Three stages per unit:
1. Prepare: cut single events out of the source recordings (onset detection), convert to 48 kHz mono,
   apply the recipe's pitch/filter/trim and write one clip per layer to Saved/AudioLayers/<Unit>/.
2. Session: Art/Audio/Sessions/<Unit>.rpp holds one track per layer (the mix balance lives in the track
   faders) and one region per output sound. The script writes a session only when it is missing or with
   --new-session, so balance, timing and FX changed by hand in REAPER survive reruns.
3. Render: `reaper -renderproject` renders every region; each render is trimmed, limited and matched to
   its event's loudness target, then written to Art/Audio/<Faction>/<Role>/<Region>.wav (48 kHz, 24-bit,
   mono, true peak <= -1 dBFS). A listening reel per unit goes to Saved/AudioPreview/<Unit>.wav.

Deterministic: fixed seeds and fixed source events, so reruns of an unchanged session give identical files.
Separate units may render concurrently (one process per --unit).
"""
import argparse
import shutil

from unit_audio import recipes
from unit_audio.core import SESSIONS, ROOT, prepare, render, write_session


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--unit", action="append", metavar="FACTION_ROLE",
                        help="only this unit, e.g. Human_Ranged (repeatable); default: every recipe")
    parser.add_argument("--new-session", action="store_true",
                        help="rewrite the REAPER sessions from the recipes, discarding manual changes")
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
