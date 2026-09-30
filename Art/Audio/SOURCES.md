# Audio sources

Game sounds in `Art/Audio/<Faction>/<Role>/` are mixed from recorded layers by `uv run Build/GenerateUnitAudio.py` (REAPER session per unit in `Art/Audio/Sessions/`). The raw recordings come from the Sonniss #GameAudioGDC bundles and are fetched by `uv run Build/FetchAudioSources.py` into `Saved/AudioSources/Sonniss/`; that script's `FILES` list is the authoritative record of which bundle and file each layer comes from.

## Licence: Sonniss #GameAudioGDC bundle licence

https://sonniss.com/gdc-bundle-license/ (version 2.0 when fetched, 2026-09-30). Royalty-free, no attribution, unlimited projects. Restrictions that matter here:

- Finished game builds may be shared and sold normally.
- The sounds must **not** be supplied to anyone as sound effects, raw or re-designed. Sharing within the team is allowed. Therefore raw sources and prepared layers stay in Git-ignored `Saved/`. **Do not publish this repository with `Art/Audio` WAVs in it** (for example as a public GitHub repo) without checking the licence again; the rendered sounds are "re-designed" licensed material.
- No use for training AI models.

No StarCraft or other game audio is used.

## Libraries used

| Library (vendor) | Bundle | Used for |
| --- | --- | --- |
| Pole Position – Springfield 1903A3 bolt-action rifle | GDC 2020 | Human Ranged fire: main rifle shot, bolt cycle |
| Pole Position – Mauser K98k bolt-action rifle | GDC 2020 | Human Ranged fire: low body layer |
| Pole Position – The Outdoor Gun Acoustics Library | GDC 2018 | Human Ranged fire: outdoor tail |
| SoundMorph – Future Weapons, Future Weapons 3 | first GDC bundle, GDC 2018 | Human Ranged fire: sci-fi punch |
| David Dumais Audio – Futuristic Guns Sound FX Pack 3 | GDC 2020 | Human Ranged fire: sci-fi punch |
| PMSFX – SCI-FI Gunnery | GDC 2020 | Human Ranged fire: designed sci-fi shot |
| Sound Spark LLC – Electric Arcs and Energy | GDC 2020 | Human Ranged fire: energy tail; impact: energy crackle |
| PMSFX – Foundation Series SCI-FI vol 2 | GDC 2020 | Human Ranged fire: servo on the bolt cycle |
| RYK-Sounds – Laser Guns | GDC 2023 | Human Ranged impact: sci-fi hit |
| Stuart Duffield – Bullet SFX | GDC 2016 | Human Ranged fire: casing drop |
| Olivier Girardot – Guns & Explosions, Hand Guns, Natural Disasters | GDC 2018–2020 | Human Ranged impact: hits and debris |
| SoundMorph – Future Weapons 3 (Grenade Launcher hit) | GDC 2018 | Human Ranged impact: sci-fi hit |
| SoundMorph – Robotic Lifeforms 2 | GDC 2019 | Human Ranged death: suit power-down |
| PMSFX – Bullet Bys & Impacts | GDC 2020 | Human Ranged impact: dirt hit |
| Double Trouble Audio – Medieval Armor and Impacts | GDC 2017 | Human Ranged impact: armour plate ring |
| Epic Stock Media – AAA Game Character Police Officer | GDC 2026 | Human Ranged death: gasp |
| SmartSoundFX – Medieval | GDC 2020 | Human Ranged death: armoured body drop |
| Red Libraries – Bodyfall | GDC 2019 | Human Ranged death: body fall |
| Gamemaster Audio – Footstep and Foley Sounds | GDC 2018 | Human Ranged death: gear rattle |
