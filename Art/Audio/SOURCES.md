# Audio sources

`./x gen generate-unit-audio` mixes game sounds in `Art/Audio/<Faction>/<Role>/` from recorded layers, with one editable REAPER session per sound set in `Art/Audio/Sessions/`. `./x gen fetch-audio-sources` fetches the Sonniss #GameAudioGDC recordings selected by each recipe's authoritative `SOURCES` list (`Build/unit_audio/<faction>_<role>.py`). Fetching, indexing, sound-set selection and rendering procedures: [`./x help gen`](../../x).

## Licence: Sonniss #GameAudioGDC bundle licence

https://sonniss.com/gdc-bundle-license/ (version 2.0 when fetched, 2026-09-30). Royalty-free, no attribution, unlimited projects. Restrictions that matter here:

- Finished game builds may be shared and sold normally.
- The sounds must **not** be supplied to anyone as sound effects, raw or re-designed. Sharing within the team is allowed. Therefore raw sources and prepared layers stay in Git-ignored local storage. **Do not publish this repository with `Art/Audio` WAVs in it** (for example as a public GitHub repo) without checking the licence again; the rendered sounds are "re-designed" licensed material.
- No use for training AI models.

No StarCraft or other game audio is used.

## Objective dispatcher: Piper / LJ Speech

The announcer WAVs are synthesized from the project's own [line script](../../Build/Audio/announcer_lines.json), not sampled game dialogue. The selected voice is **en_US-ljspeech-high** (single US-English female speaker, 22,050 Hz native), trained from scratch on **LJ Speech**, read by Linda Johnson and annotated by Keith Ito. Its measured delivery is the intended human-operator contrast to JEV, with light radio filtering; subjective listening and in-game balance need owner approval.

- Model repository declares **MIT**: [repository metadata](https://huggingface.co/rhasspy/piper-voices/blob/c10ece1aade47bb51c153c893d14e5bf8e5b7117/README.md).
- The selected voice's [model card](https://huggingface.co/rhasspy/piper-voices/blob/c10ece1aade47bb51c153c893d14e5bf8e5b7117/en/en_US/ljspeech/high/MODEL_CARD) declares the training data **public domain**. [LJ Speech licence](https://keithito.com/LJ-Speech-Dataset/): text, audio and annotations are public domain in the US; no restrictions on use or required attribution. These terms permit commercial game use. The dataset publisher qualifies non-US public-domain status; check distribution jurisdictions before release.
- Model and phoneme config use the immutable revision `c10ece1aade47bb51c153c893d14e5bf8e5b7117`; their exact download URLs and SHA-256 digests are authoritative in [announcer_voice.json](../../Build/Audio/announcer_voice.json). Both are verified on every cache use and download. Model files stay in ignored local storage and are not shipped in the game.
- Build-time **piper-tts 1.3.0** is [GPL-3.0](https://github.com/OHF-Voice/piper1-gpl/blob/v1.3.0/COPYING), separate from the voice/data licence. It runs offline as a subprocess; neither Piper nor its dependencies are linked into or shipped with the game. [GPL section 2](https://www.gnu.org/licenses/gpl-3.0.html) covers output only if the output itself constitutes a covered work; these WAVs contain synthesized project-authored dialogue, not Piper code. Distributing the build tool/runtime still requires its GPL compliance.

The model, native config and synthesis settings are pinned; package versions live in [RenderAnnouncerVoice.py](../../Build/RenderAnnouncerVoice.py). Target format and radio/loudness settings live in [Audio.md](../../Docs/Audio.md). Generation, isolated import and audition options: [`./x help gen`](../../x).

## Libraries used

146 libraries across 10 sound sets (six combat units, two structure sets, player UI/sectors and one Offline ambience bed), derived from the recipes' `SOURCES`. The per-layer purpose of each file is in the recipe code and its comments. "GDC bundle" is the year of the bundle ZIP the file is fetched from.

| Library (vendor – title) | GDC bundle | Used by |
| --- | --- | --- |
| 2496SoundEffects – Super Heroes Sound Design | 2019 | Human Frontline |
| 344 Audio – Bass Drops & Downers Vol. 2 | 2026 | Machine Siege |
| 344 Audio – Nuts And Bolts | 2023 | Human Structure |
| 3maze –  Buttons and Switches | 2016 | Human UI |
| 3maze – IMPACTUS | 2019 | Machine Frontline |
| Airborne Sound – Battlefield Howitzers | 2019 | Human Siege |
| Airborne Sound – Eclectic Whooshes | 2018 | Machine Frontline, Machine Ranged |
| Airborne Sound – Elements Glass | 2019 | Machine Frontline, Machine Structure |
| Airborne Sound – Tools | 2019 | Human Siege |
| Airborne Sound – Variety 1 Sound Effects | 2018 | Human Siege |
| Alexander Kopeikin – Black Metal | 2016 | Human Siege |
| Alexander Kopeikin – Sci-Fi Metal Elements HD | 2016 | Human Siege |
| Articulated Sounds – Fight Vocalizations | 2019 | Human Frontline |
| Articulated Sounds – Magic Elements vol.2 | 2020 | Machine Ranged, Machine Siege |
| Async Audio – Sci-Fi Blaster | 2020 | Machine Ranged, Machine Siege |
| Async Audio – Trailer Tools | 2020 | Machine Ranged |
| BlueZone – Heavy Metal Impact Sound Effects | 2019 | Human Siege |
| Bluezone – Tank – Explosion Sound Effects | 2020 | Human Siege, Machine Siege |
| Bluezone Corporation – Metal Debris | 2018 | Human Siege, Machine Siege |
| BluezoneCorp – Alien Tripod | 2024 | Machine Siege |
| BluezoneCorp – Broken Glass | 2023 | Machine Frontline, Machine Siege, Machine Structure |
| BluezoneCorp – Building Collapse | 2023 | Human Structure, Machine Siege, Machine Structure |
| BluezoneCorp – Demolisher – Robot | 2023 | Human Frontline, Machine Frontline |
| BluezoneCorp – Detonation – Explosion | 2023 | Human Structure, Machine Siege |
| BluezoneCorp – High Voltage | 2024 | Human UI, Machine Frontline, Machine Siege, Machine Structure |
| BluezoneCorp – Industrial Lever Switch | 2024 | Human Structure, Human UI |
| BluezoneCorp – Sci Fi Weapon | 2024 | Machine Siege |
| Bottle Rocket Fx – Scream | 2016 | Human Frontline |
| CB Sound Design – Cyberdeck – Hologram & Transmissions Sound Effects | 2023 | Machine Structure |
| CB Sound Design – Defect – Hum, Noise And Glitches | 2023 | Machine Frontline |
| Cfry – Hand Tools | 2019 | Human Structure |
| Chris Skyes – Shards Broken Glass | 2017 | Human Frontline, Machine Frontline, Machine Ranged, Machine Siege, Machine Structure |
| Christophe Davaille – The Gym – Sounds Of Bodybuilding | 2018 | Human Frontline |
| Cinematic Sound Design – Interface & Infographics | 2026 | Machine Frontline, Machine Ranged |
| Coll Anderson – Car Destruction | first bundle | Human Frontline |
| David Dumais Audio – Electricity Magic 1 | 2020 | Machine Ranged |
| David Dumais Audio – Futuristic Guns Sound FX Pack 3 | 2020 | Human Ranged |
| David Dumais Audio – Sci-Fi Weapons Pack 1 | 2023 | Human Siege, Machine Frontline, Machine Siege |
| DavidDumais – Explosion SFX Pack | 2024 | Human Structure, Machine Siege, Machine Structure |
| Detunized – HumBuzz | first bundle | Human UI |
| Digital Rain Lab – Lethal Energies | 2016 | Machine Ranged |
| Double Trouble Audio – Junk and Debris | 2017 | Human Frontline, Human Siege |
| Double Trouble Audio – Medieval Armor and Impacts | 2017 | Human Ranged |
| Double Trouble Audio – Shards | 2017 | Machine Frontline, Machine Structure |
| Double Trouble Audio – Tanks and Jet | 2017 | Human Siege |
| Double Trouble Audio – Tools and Wood | 2017 | Human Structure |
| Dramatic Cat – Claas Dominator – Combine Harvester 88 SL Maxi | 2023 | Human Siege |
| Effectsworks – Tech Future | 2020 | Machine Structure |
| Eiravaein Works – 48Kilos | 2020 | Machine Ranged |
| Eneas Mentzel – Debris & Rubble | 2023 | Machine Frontline, Machine Structure |
| Epic Stock Media – AAA Game Character Police Officer | 2026 | Human Ranged |
| Epic Stock Media – Elemental Mutation Whooshes and Impacts | 2026 | Machine Structure |
| Epic Stock Media – Tower Defense Game | 2026 | Human Frontline, Machine Structure |
| Fascinated Sound – Scene Shop Tool Box | 2017 | Human Siege, Human Structure |
| Fascinated Sound – The Gun Locker SFX Pack | 2016 | Human Siege |
| Fox Audio Post-Production – Space Racer 2 – Turbines & Engines | 2018 | Machine Frontline |
| Game Audio Factory – GAFSFX01 – Dark Materials | 2016 | Human Frontline |
| Gamemaster Audio –  Explosion Sound Pack | 2017 | Human Siege |
| Gamemaster Audio –  Human Vocalizations | 2017 | Human Frontline |
| Gamemaster Audio – Footstep and Foley Sounds | 2018 | Human Ranged |
| Gamemaster Audio – Magic and Spell Sounds | 2018 | Human Siege, Machine Frontline |
| Gamemaster Audio – Punch and Combat Sounds | 2018 | Human Frontline |
| Gamemaster Audio – Sci-Fi Sounds and Sci-Fi Weapons | 2018 | Human Siege |
| George Karagioules – The Source Collection SFX Pack | 2016 | Machine Frontline, Machine Ranged, Machine Structure |
| Glitchedtones – Data Disruption | 2018 | Machine Frontline, Machine Ranged |
| Glitchedtones – Granular Textures | 2017 | Machine Structure |
| Glitchedtones – Impact | 2018 | Human Siege, Machine Frontline, Machine Ranged |
| Hammer & Spark Audio Production – Warrior IFV Custom Vehicle Recording | 2017 | Human Structure |
| Hear and Now Sound – High Voltage Electricity – The Essential Collection | 2018 | Human Frontline, Human Siege |
| Hear and Now Sound – Office Equipment and Supplies | 2018 | Machine Ranged |
| Hzandbits – Hvac Elements | 2017 | Human Ambience |
| Hzandbits – Urban Winds II | 2017 | Human Ambience |
| InMotionAudio – USA Hotel | 2026 | Human UI |
| Ivo Vicic – Structure borne sound – metal resonances and textures | 2019 | Human Ambience |
| Ivo Vicic – Studer A80 Master recorder analog tape machine | 2020 | Human Structure |
| Jake Fielding – Drill – Foley & Bursts | 2024 | Human Structure |
| Justsoundeffects – Industrial Robot | 2023 | Human Frontline |
| Levan Nadashvili – Soldier Footsteps | 2016 | Human Structure |
| MatiasMacSD – NOISE | 2016 | Human UI |
| MatiasMacSD – THE MACHINES – ROBOTIC SOUNDS | 2016 | Human Frontline |
| MatiasMacSD – THE MACHINES 2_ROBOTIC SOUNDS | 2017 | Machine Structure |
| MatiasMacSD – THE WEAPONS – SCI-FI WEAPONS | 2017 | Human Siege, Machine Ranged, Machine Siege |
| MattLightbound – 3D Printer – Lulzbot mini | 2018 | Machine Structure |
| Mechanical Wave – Foley Session 01 | first bundle | Human Structure |
| Mechanical Wave – Glass | 2024 | Machine Frontline, Machine Siege, Machine Structure |
| Mechanical Wave – Hits Whoosh | first bundle | Human Frontline, Human Siege, Machine Structure |
| Mechanical Wave – Sci-Fi-Invaders | first bundle | Human Siege |
| Mechanical Wave – Undoing-Computer | first bundle | Human Frontline, Human Siege, Machine Ranged |
| Mononeshot – MEDIEVAL, Smithy | 2017 | Human UI |
| Olivier Girardot – Guns & Explosions | 2018 | Human Ranged |
| Olivier Girardot – Hand Guns Sound Effects Pack | 2020 | Human Ranged |
| Olivier Girardot – Natural Disasters | 2019 | Human Ranged |
| Omar Alvarado – Household, kitchen and hotel sounds | 2018 | Human UI |
| PMSFX – Bullet Bys &Impacts | 2020 | Human Ranged, Machine Frontline |
| PMSFX – Foundation Series SCI-FI vol 2 | 2020 | Human Ranged |
| PMSFX – Glitchy Circuits | 2020 | Machine Ranged |
| PMSFX – Mechanical Morphs And Mutations | 2020 | Machine Siege |
| PMSFX – NANOTECH | 2020 | Machine Structure |
| PMSFX – SCI-FI GunnerySCI-FI Gunnery | 2020 | Human Ranged |
| Pole Position – Car Debris, Impacts & Crashes | 2017 | Human Siege, Machine Siege |
| Pole Position – Car Destruction | 2020 | Human Siege |
| Pole Position – Drone DJI Spreading Wings S900 | 2018 | Machine Ranged |
| Pole Position – Hydraulics & Pneumatics Library | 2017 | Human Structure |
| Pole Position – Mauser Karabiner 98 kurz K98k bolt-action rifle | 2020 | Human Ranged |
| Pole Position – Narrowboat Gardner 4LW 1960 | 2019 | Human Structure |
| Pole Position – Sherman M4A1 Medium Tank | 2018 | Human Structure |
| Pole Position – Springfield 1903A3 bolt-action rifle | 2020 | Human Ranged |
| Pole Position – The Metal Hit Sweeteners Library | 2024 | Human Frontline |
| Pole Position – The Outdoor Gun Acoustics Library | 2018 | Human Ranged |
| Pole Position – The Vehicle Doors and More Library | 2024 | Human UI |
| RDGSFX008 – The Metal Shelf | first bundle | Human Structure |
| RYK-Sounds – Laser Guns | 2023 | Human Ranged |
| Red Libraries – Bodyfall | 2019 | Human Frontline, Human Ranged |
| Rescopic Sound – Parallax | 2024 | Machine Ranged |
| Rescopic Sound – Sci-Fi Energy Weapons | 2024 | Machine Ranged |
| Rogue Waves – Anime Studio | 2024 | Machine Frontline |
| Rogue Waves – Glitch Grains | 2023 | Machine Siege, Machine Structure |
| SanojSounds – Essentials Vol.1 | 2023 | Human Structure |
| Secret Source – 003 Jaws Of Life | first bundle | Machine Frontline |
| Sergey Eybog – Handheld Tranceivers | 2016 | Human Structure, Human UI |
| Shapeforms Audio – Sci-Fi Weapons Cyberpunk Arsenal | 2023 | Machine Ranged |
| SmartSoundFX – Futuristic | 2020 | Human Siege, Machine Ranged, Machine Siege |
| SmartSoundFX – Medieval | 2020 | Human Ranged |
| Sound Ex Machina – UI SOUNDS – MUSICAL | 2017 | Machine Frontline, Machine Ranged, Machine Siege, Machine Structure |
| Sound Sower – Electric Field | 2020 | Human Structure |
| Sound Spark LLC – Broken Robot | 2020 | Human Frontline, Human Siege, Machine Ranged, Machine Siege, Machine Structure |
| Sound Spark LLC – Electric Arcs and Energy | 2020 | Human Ranged, Human Siege, Machine Frontline |
| Sound Spark LLC – GLITCH FACTORY 1- BOOM, CRACKLE AND SCREAM | 2019 | Machine Siege, Machine Structure |
| Sound Spark LLC – Metal Hits, Scrapes and Squeaks | 2019 | Human Frontline, Human Structure |
| SoundHolder –  Tractor Case II CX90 | 2017 | Human Siege |
| SoundMorph – ENERGY | 2017 | Human Siege, Human UI |
| SoundMorph – FUTURE WEAPONS 3 | 2018 | Human Ranged |
| SoundMorph – Future Weapons | first bundle | Human Ranged |
| SoundMorph – MECHANISM | 2017 | Human Siege, Human UI |
| SoundMorph – Robotic Lifeforms | first bundle | Human Siege, Machine Siege |
| SoundMorph – Robotic Lifeforms 2 | 2019 | Human Frontline, Human Ranged, Human Siege, Human UI, Machine Structure |
| Soundholder – Renault Master 2.3 dci | 2020 | Human UI |
| Soundopolis – Blades | first bundle | Machine Frontline |
| Stuart Duffield – Bullet SFX | 2016 | Human Ranged |
| The Sound Pack Tree – Construction & Tools | 2020 | Human Structure |
| The Sound Pack Tree – Trains | 2019 | Human Siege |
| TheLibrarybyEmptySea – Robobiotics | first bundle | Machine Ranged |
| Tone Manufacture – Glitch UI | 2018 | Human UI, Machine Frontline |
| UberDuo – The Cabin Audio Playset | 2018 | Human Siege, Human UI |
| UberDuo – The Home Barista | 2018 | Human UI |
| Wakerone – Electric Deluxe | 2018 | Human Structure |

## Offline night ambience

`Build/unit_audio/human_ambience.py` authors the single `NightLoop` event in the editable `Art/Audio/Sessions/Human_Ambience.rpp`. `./x gen fetch-audio-sources` supplies the source index and the recipe-selected recordings; indexing/subset procedures are in [`./x help gen`](../../x).

| Source filename | Library / GDC bundle | Layer and source window |
| --- | --- | --- |
| `Hvac,Ventilation,Exhaust,Industrial,Drone,Slight rumble,Loop.wav` | Hzandbits – Hvac Elements / 2017 Part 4of9 | Distant machinery, 10–44 s; 65 Hz high-pass, 950 Hz low-pass |
| `Wind,Ext,Alley,Whoosh,Fluctuating slightly,Wire flapping.wav` | Hzandbits – Urban Winds II / 2017 Part 4of9 | Wind bed, 8–42 s; 180 Hz high-pass, 2300 Hz low-pass |
| `08 Overhead power line in wind.wav` | Ivo Vicic – Structure borne sound – metal resonances and textures / 2019 Part 2of8 | Far power-line resonance, 4–38 s; 110 Hz high-pass, 1600 Hz low-pass |

All three were fetched on 2026-09-30 and use the [Sonniss #GameAudioGDC bundle licence, version 2.0](https://sonniss.com/gdc-bundle-license/), effective 27 August 2026: commercial game synchronization and team sharing are permitted; supplying raw or re-designed sounds as sound effects and AI training are prohibited. The repository publication restriction above applies to this WAV too.

`./x gen generate-unit-audio` renders the ambience recipe ([`./x help gen`](../../x)). Recipe discovery is automatic. Prepared local layers restore the session's relative media paths; existing sessions are preserved unless explicitly replaced through the documented generator option.

The 34-second region uses a 2-second equal-power wrap overlap, yielding exactly one 32-second mono 48 kHz 24-bit WAV: `Art/Audio/Human/Ambience/SW_Human_Ambience_NightLoop_01.wav`. Its initial track faders are machinery −15 dB, wind −8 dB and power lines −23 dB; filters run through two seconds of pre-roll before the window is taken. The loop is finished with constant gain to −27 LUFS, without limiting.

Measured after the actual headless REAPER render: −27.0000 LUFS, −11.8906 dBFS 4× true peak, −11.8961 dBFS sample peak. Independent FFmpeg EBU R128 measurement reports −27.0 LUFS, −11.9 dBFS true peak and 3.5 LU loudness range. The wrap's adjacent-sample step is 0.00303149 (−50.37 dBFS), at the 50.25th percentile of internal sample steps and below the internal 99th-percentile step of 0.01328779; it is not an anomalous discontinuity. The 200 ms seam-window RMS is −27.40 dBFS (neighbouring windows −28.57 / −27.65 dBFS).

The generated ambience listening reel is 64.5 seconds: two sample-identical repetitions followed by 0.5 seconds of silence. Listen before import; the run record identifies the output. These measurements do not establish subjective listening approval or the in-game mix.
