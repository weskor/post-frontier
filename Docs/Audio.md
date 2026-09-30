# Audio direction and sound spec

Sound design for **Post-Frontier** (CoopRTS). Tone, names and factions come from [World.md](World.md); rules and triggers come from `README.md` and source. StarCraft 2 is a **listening reference only**: no SC2 audio is extracted, sampled or imitated line-for-line.

Status: design spec plus the first unit. `Art/Audio/Human/Ranged/` holds the Offline Ranger's fire, impact and death one-shots, mixed from recorded layers (see "Sourcing and production pipeline"). There is no audio code or Unreal audio asset yet (`USound*`, `MetaSound`, `PlaySound` appear nowhere in `Source/`).

## What we take from SC2

1. **Faction identity by ear.** Close your eyes and you still know which side fired. Terran-style grit for the Offline, polished and synthetic for the Machine.
2. **Layered, compressed one-shots.** Every weapon or impact = transient (click/crack) + body (mid) + weight (sub thump, 40–80 Hz) + tail (short room/debris). Short, punchy and loud for their length.
3. **Readable chaos.** Hard voice limits per sound, priority for what matters (alerts > own abilities > own weapons > enemy weapons > ambience). A 30-unit fight should sound big, not like noise.
4. **Units talk back.** Short acknowledgement lines with a radio or processing chain do much of the character work.
5. **An announcer for state changes.** "Base under attack", "not enough minerals"; plain, rate-limited, never missed.

## Game-specific constraint

Players command **buildings and fronts, not units** (README "Construction and production"). So SC2's select/move acknowledgements map to:

| SC2 moment | Post-Frontier trigger | Who speaks |
| --- | --- | --- |
| Select unit | Select an owned completed barracks | One line from that barracks' locked unit type |
| Move / attack order | Assign **Secure / Defend / Fall Back** front | Unit-type line matching the order |
| Unit ready | A recruit physically deploys from the barracks | Unit-type "ready" line (rate-limited; see Alerts) |
| Train | **Start & Lock** accepted | Building confirmation + unit-type "ready" line |

## Faction palettes

### The Offline (players, team 0): night-shift industrial

- **Materials:** steel plate, bolts, hydraulics, diesel, relays, big switches, gunpowder, sparks, rattling prefab panels.
- **Processing:** tape/overdrive saturation, spring or small metal-room reverb, band-limited radio (≈300 Hz–3.5 kHz, light distortion, squelch in and out) on voices.
- **Musical colour:** low brass-like drones, analog synth pads, rhythm from machinery.
- **Voice:** dry, tired, practical night crew (World.md). Male and female mix, no heroics.

### The Machine (enemy, team 5): product launch that wants you dead

- **Materials:** glass, ceramic, clean sine/FM tones, filtered noise, servo whine without friction, crystalline resonances.
- **Processing:** pristine, wide, bright, bit-perfect; occasional deliberate digital artefacts (stutter, granular smear, bit crush) as the "wrong" moments. Big clean reverb tails.
- **Signature motif:** a pleasant 2–3 note **notification chime**, reused and warped for weapons, alerts and deaths. The red lens (HAL nod) gets a low sustained sine hum on idle and aggro.
- **Voice:** a polite corporate assistant (World.md). Calm, warm, synthesized, never shouts. Deaths are cheerful system messages.

## Unit sound sheets

Names are from World.md "Unit roster". Variant counts are minimums; the sound randomizes between them with small pitch and volume jitter.

### Offline

| Unit | Weapon / attack (4 var.) | Impact (3) | Death (3) | Movement / idle | Voice cues (3 each) |
| --- | --- | --- | --- | --- | --- |
| **Luddite** (Frontline, sledgehammer + riot shield) | Servo wind-up tick + metal hammer strike + low thud | Dull clang on shield, crunch on flesh/armour | Armour collapse, hydraulic hiss, shield drop | Heavy armoured footsteps, servo creak | Ready, Secure, Defend, Fall Back |
| **Offline Ranger** (Ranged, long rifle, antenna pack) | Dark and sci-fi: bolt-action rifle shot pitched down and rolled off + low rifle body + designed sci-fi shot + short sci-fi punch + electric-arc and outdoor tails, then bolt cycle with servo and casing drop | Bullet hit + sci-fi hit + armour plate ring + energy crackle + debris | Gasp, suit power-down, gear rattle, armoured body drop + body fall | Light gear rattle, antenna squelch | Ready, Secure, Defend, Fall Back |
| **Unplugger** (Siege, mech with bolt-cutter jaws) | Hydraulic ram charge + huge shear crunch + electric arc | Heavy crunch, sparks, power cut "thunk" | Engine stall, metal groan, collapse | Diesel idle loop, heavy mech steps | Ready, Secure, Defend, Fall Back |

### Machine

| Unit | Weapon / attack (4 var.) | Impact (3) | Death (3) | Movement / idle | Voice cues (3 each) |
| --- | --- | --- | --- | --- | --- |
| **SOL 6000** (Frontline) | Rising sine sweep + glassy strike + sub pulse | Crystalline ring, energy crackle | Chime played backwards and pitched down, shell segments falling | Smooth servo glide, lens hum | Aggro line on engage only (enemy units never take orders from the player) |
| **Autocomplete Drone** (Ranged, hovers) | Keyboard-click tick (the "typing" transient) + clean laser zap | Short fizz, data-glitch stutter | Stutter + power-down sweep | Hover drone loop, soft keystroke ticks | Aggro line |
| **Hallucinator** (Siege artillery) | Charged glass bloom + launch; detuned granular shimmer ("confidently wrong") | Big clean bloom, sub drop, bright debris | Cheerful "task failed" chime, crash | Low hum, hover shimmer | Aggro line |

Machine units do not answer the player. They get **one-shot aggro lines** when they first engage (rate-limited) and the commander's "Thinking…" lines (below).

## Buildings, sectors and HQs

| Event | Code trigger (client-observable) | Offline | Machine |
| --- | --- | --- | --- |
| Placement preview valid / invalid | Controller placement mode | UI tick / low buzz | — |
| Placement accepted | `ServerPlaceBuilding` success → new building replicates | Crate drop, bolt gun burst | Deploy chime + materialise shimmer |
| Construction loop | `ConstructionProgress` 0 < p < 1 | Welding, rivet gun, ratchets (loop) | Printing/rendering hum (loop) |
| Construction complete | `ConstructionProgress` reaches 1 (`OnRep_Appearance`) | Power-up relay clunk + floodlight hum | Chime + power-on sweep |
| Construction cancelled | Building destroyed before complete | Panel rattle, collapse | Reverse chime |
| Building under attack | `Health` decreases (`OnRep_Appearance`) | Impact per hit + announcer (rate-limited) | Impact per hit |
| Building destroyed | `Health` reaches 0 / `EndPlay` | Large explosion + debris tail | Glass shatter + power-down + debris |
| Barracks recruit deploys | New `AArmyUnit` with the building's force | Blast door + unit "ready" line | Portal/print sound |
| Research bought | Workshop research accepted | Toolbox slam + drill | — |
| Sector capture progress | `ACapturePoint::OnRep_Capture` | Rising tone per 25% | Same, Machine-coloured |
| Sector captured / lost | Capture owner change | Announcer + sting | Announcer + sting |
| HQ damaged / destroyed | `AHeadquarters` `OnRep_Appearance` / `Health` 0 | Alarm klaxon; huge explosion | Siren-chime; collapse |

`FireAt` / `ReceiveAttack` run on the server only. Clients must play sound from replicated state: unit attacks from `AArmyUnit::OnRep_Attack` (the `AttackCount` counter already drives the attack flashes there), damage and death from `Health` changes in `OnRep_Appearance`. The listen host plays through the same paths so that host and clients hear the same thing.

## Announcer

Two voices. The **player announcer** is an Offline dispatcher on a radio. The **Machine commander** speaks the intercepted `Thinking…` lines from World.md ("Enemy commander voice", "Enemy lines for construction") in the corporate-assistant voice, with a soft notification chime before each. The function must stay clear: per World.md tone rule 2, readability beats jokes.

| Event | Player announcer line (draft) | Cooldown |
| --- | --- | --- |
| Match start | "Session started. Your conversation may be used to improve our models." (Machine voice, World.md match text) | — |
| Building under attack | "Drill Shed's taking fire." / "Structure under attack." | 10 s per building, 5 s global |
| HQ under attack | "The Bunker is under attack." | 10 s |
| Units under attack (away from camera) | "Our crew's taking fire." | 10 s |
| Not enough power | "Not enough power." | 2 s |
| Invalid placement | "Can't build there." | 1 s |
| Construction complete | "Drill Shed's up." / "Tap Point's online." | per building |
| Research complete | "Garage says it works. Probably." | — |
| Sector captured | "We've got the substation." | per sector |
| Sector lost | "Lost the substation." | per sector |
| Force wiped | "Drill Shed's crew is gone." | per force |
| Victory | "Model deprecated." | — |
| Defeat | "Your session has expired. Humanity has been sunset." (Machine voice) | — |
| Restart prompt | "Regenerate response?" (Machine voice) | — |

Rate limiting follows SC2: the same alert never repeats inside its cooldown, and alerts for events on screen are quieter or skipped.

## UI

Players are the Offline, so the HUD sounds analog: chunky toggle switches for buttons, relay click for selection, rotary click for front choice, low buzz for rejected actions, CRT/phosphor tick for hover. Short (≤150 ms), dry, stereo, never spatialized.

## Music

- **Menu / restart:** slow analog synth drone with distant machinery.
- **In match:** low-intensity industrial bed; intensity layer added while friendly units or buildings are in combat (crossfade over 2–4 s), removed after 10 s of calm.
- **Machine stings:** chime motif on major Machine actions (assault plan, HQ damage).
- **Victory / defeat stings:** Offline brass-drone swell / Machine chime slowing and detuning.

## Unreal mix architecture

All assets live under `/Game/Audio`. Naming follows Unreal convention: `SC_` Sound Class, `SMX_` Submix, `SCON_` Concurrency, `ATT_` Attenuation, `MIX_` Sound Mix, `MS_` MetaSound Source, `SW_` Sound Wave.

### Sound Classes

```
SC_Master
├─ SC_World
│  ├─ SC_Weapons      (Offline and Machine children for independent balance)
│  ├─ SC_Impacts
│  ├─ SC_Deaths
│  ├─ SC_Buildings    (construction loops, complete, destroyed)
│  └─ SC_Ambience
├─ SC_Voice
│  ├─ SC_UnitVoice
│  └─ SC_Announcer
├─ SC_UI
└─ SC_Music
```

Submixes mirror the top level (`SMX_World`, `SMX_Voice`, `SMX_UI`, `SMX_Music`) so each can carry its own effects: a bus compressor/limiter on `SMX_World`, radio EQ on Offline voice, a reverb send for world sounds.

### Ducking

- `MIX_AnnouncerDuck`: passive mix on `SC_Announcer`, lowers `SC_World` by 4 dB and `SC_Music` by 6 dB while a line plays (fade 0.1 s in, 0.4 s out).
- `MIX_VoiceDuck`: passive mix on `SC_UnitVoice`, lowers `SC_Weapons` by 2 dB.

### Concurrency (starting values, tune in playtest)

| Asset | Applies to | Max | Resolution |
| --- | --- | ---: | --- |
| `SCON_WeaponPerType` | One weapon sound type (e.g. Ranger rifle) | 6 | Stop farthest then oldest |
| `SCON_WeaponsTotal` | All weapons | 24 | Stop lowest priority then quietest |
| `SCON_Impacts` | All impacts | 16 | Stop farthest then oldest |
| `SCON_Deaths` | All deaths | 8 | Stop oldest |
| `SCON_BuildingLoops` | Construction loops | 4 | Stop farthest |
| `SCON_UnitVoice` | Unit acknowledgements | 1 | Prevent new (no talking over each other) |
| `SCON_Announcer` | Announcer | 1 | Queue in code; prevent new in engine |

Priority: Announcer 4.0 > UI 3.0 > unit voice 2.5 > own weapons/impacts 1.5 > enemy weapons 1.0 > ambience 0.5. "Own" means the local commander's units (`CommanderIndex` matches the local player state).

### Listener and attenuation (top-down camera)

- The camera is high above the battlefield, so distance from the camera flattens everything. Put the **attenuation listener on the ground point under the screen centre** (`APlayerController::SetAudioListenerAttenuationOverride`), and keep panning from the camera's orientation.
- `ATT_Unit`: falloff starts at roughly one screen half-width and ends at 1.5 screen widths, so off-screen fights are faint but audible. Units stay mono and spatialized.
- `ATT_Building`: larger inner radius; explosions carry further (`ATT_Explosion`).
- Voices and the announcer are 2D (no attenuation) so orders are always heard.
- Low-pass on distance for world sounds, so far fights sound muffled rather than just quieter.

### Variation (MetaSound Sources)

One `MS_` per event with a random wave player: 3–5 variants, no immediate repeats, pitch ±4%, volume ±1.5 dB. Weapon MetaSounds expose a `Faction` or `Role` parameter only if a single graph genuinely serves both; otherwise keep one source per unit per event.

### Source format and loudness

- 48 kHz, 24-bit WAV; world one-shots mono, UI and music stereo.
- Every variant of an event is matched to that event's integrated loudness, so round-robin variants sit at the same level: weapons -20 LUFS, impacts -22, deaths -21. True peak stays at or below -1 dBFS (a limiter only acts when the loudness target would exceed it). Voice lines about -18 LUFS; music around -20 LUFS. Final balance happens in Sound Class volumes, not by re-exporting.

## Asset budget (first pass)

| Group | Count |
| --- | ---: |
| Unit weapons, impacts, deaths (6 units × 3 events × 3–4 variants) | ~60 |
| Unit movement/idle loops | 6 |
| Offline unit voice (3 units × 4 cues × 3 variants) | 36 |
| Machine aggro lines (3 units × 3) | 9 |
| Buildings (both factions × place, loop, complete, destroyed; plus research, deploy) | ~20 |
| Sectors and HQ | ~10 |
| Announcer lines (player + Machine commander, World.md tables) | ~40 |
| UI | ~10 |
| Music (menu, match bed + intensity layer, 4 stings) | 7 |

About 200 files. Units and buildings come first, because the fight is what makes the game read.

## Sourcing and production pipeline

1. **Raw material:** recordings from the Sonniss #GameAudioGDC bundles. `uv run Build/FetchAudioSources.py` pulls only the listed files out of the multi-GB bundle ZIPs (HTTP range requests) into the Git-ignored `Saved/AudioSources/Sonniss/`. Libraries and licence limits are in `Art/Audio/SOURCES.md`: the raw and re-designed sounds must not be passed on as sound effects, so this repository must not be published with them. Pure synthesis was tried first and sounded thin and "blippy"; recorded material is the baseline.
2. **Layering (REAPER):** `uv run Build/GenerateUnitAudio.py` cuts single events out of the recordings (onset detection), applies each recipe's pitch/filter/timing, and writes one clip per layer to `Saved/AudioLayers/<Unit>/`. `Art/Audio/Sessions/<Faction>_<Role>.rpp` is the REAPER session: one track per layer (track faders hold the mix balance) and one region per output sound. The script creates a session only when missing (`--new-session` rebuilds it from the recipes), so open it in REAPER, change faders, timing, items or FX by ear, save, and rerun the script to render. Region names are the output file names.
3. **Render:** the script renders every region headlessly (`reaper -renderproject`), trims the silent end, matches loudness and writes `Art/Audio/<Faction>/<Role>/SW_<Faction>_<Role>_<Event>_NN.wav`. `<Faction>`/`<Role>` follow the mesh names (`SM_Human_Ranged`). Unchanged sessions render identical files. A listening reel per unit goes to `Saved/AudioPreview/<Faction>_<Role>.wav`.
4. **Voice:** local TTS (e.g. Piper) for drafts; a real voice actor for the final Offline crew is recommended. The Machine voice can stay synthesized; that fits the faction.
5. **Import:** a `Build/ImportAudio.py` editor script, matching the existing art pipeline (`Build/Import*.py`), imports waves, builds the `MS_` sources and assigns classes, concurrency and attenuation.
6. **Never** use extracted or re-recorded SC2 audio, or real product sounds (World.md tone rule 4 applies to audio too).

## Proposed code integration

This follows the restructure contract's goal "adding a unit, building or map requires assets, not C++":

- `UArmyUnitDefinition` gains per-faction sound references beside `HumanMesh`/`MachineMesh`: fire, impact, death, deploy, and a voice set (ready / secure / defend / fall back / aggro). `UBuildingDefinition` gains place, construction loop, complete and destroyed.
- A client-side `UWorldSubsystem` (`UCoopAudioSubsystem`) plays world sounds from replicated-state hooks (`AArmyUnit::OnRep_Attack`, `OnRep_Appearance` health deltas, `ACommandBuilding::OnRep_Appearance`, `ACapturePoint::OnRep_Capture`, `AHeadquarters::OnRep_Appearance`, match result) and owns the announcer queue and cooldowns. Returns immediately on dedicated servers.
- The HUD and controller call the subsystem for UI sounds and front-order acknowledgements (local only, never replicated).
- The controller sets the listener attenuation override each tick from the camera ground focus.

Those files belong to active restructure lanes (Content: `UnitDefinition.h`, `ArmyUnit.*`, `CommandBuilding.cpp`; Presentation: `CommandHUD.*`, controller HUD mapping). Integrate audio after that wave lands, or through the orchestrator.
