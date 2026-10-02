# Audio direction and sound spec

Sound design for **Post-Frontier** (CoopRTS). Tone, names and factions come from [World.md](World.md); rules and triggers come from `README.md` and source. StarCraft 2 is a **listening reference only**: no SC2 audio is extracted, sampled or imitated line-for-line.

Status: **115 rendered WAVs** from recorded/designed library layers: 60 attack/impact/death sounds for six combat units, 18 Unindexed structure/HQ sounds, 18 Machine structure/HQ/notification sounds, 18 player UI/sector sounds, and one Unindexed industrial-night ambience loop. Files are in `Art/Audio/<Faction>/<Role>/`, with editable REAPER sessions in `Art/Audio/Sessions/`. Unit movement/idle loops, voice, announcer and music remain planned. The importer and runtime integration below include the ambience bed, distance filtering and shared reverb; their fresh Unreal import/build/runtime verification is scheduled separately.

## What we take from SC2

1. **Faction identity by ear.** Close your eyes and you still know which side fired. Terran-style grit for the Unindexed, polished and synthetic for the Machine.
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

### The Unindexed (players, team 0): night-shift industrial

- **Materials:** steel plate, bolts, hydraulics, diesel, relays, big switches, gunpowder, sparks, rattling prefab panels.
- **Processing:** tape/overdrive saturation, spring or small metal-room reverb, band-limited radio (≈300 Hz–3.5 kHz, light distortion, squelch in and out) on voices.
- **Musical colour:** low brass-like drones, analog synth pads, rhythm from machinery.
- **Voice:** dry, tired, practical night crew (World.md). Male and female mix, no heroics.

### The Machine (enemy, team 5): product launch that wants you dead

- **Materials:** glass, ceramic, clean sine/FM tones, filtered noise, servo whine without friction, crystalline resonances.
- **Processing:** pristine, wide, bright, bit-perfect; occasional deliberate digital artefacts (stutter, granular smear, bit crush) as the "wrong" moments. Big clean reverb tails.
- **Signature motif:** a pleasant 2–3 note **notification chime**, reused and warped for weapons, alerts and deaths. The source is Sound Ex Machina "UI Sounds – Musical" `Notification_Bridge Operation 01.wav`; every Machine death uses it (reversed, stuttered or sliding down). Use the same file for Machine alerts and UI so the faction keeps one signature. The red lens (HAL nod) gets a low sustained hum on idle and aggro.
- **Voice:** a polite corporate assistant (World.md). Calm, warm, synthesized, never shouts. Deaths are cheerful system messages.

## Unit sound sheets

Names are from World.md "Unit roster". Variant counts are minimums; the sound randomizes between them with small pitch and volume jitter. The attack, impact and death columns describe the rendered sounds (direction: dark and sci-fi); movement and voice columns are still plans. Every attack ends inside its unit's attack interval (Frontline 0.7 s, Ranged 1.15 s, Siege 2.6 s), and low end decays quickly so fights stay readable.

### Unindexed

| Unit | Weapon / attack (4 var.) | Impact (3) | Death (3) | Movement / idle | Voice cues (3 each) |
| --- | --- | --- | --- | --- | --- |
| **Bulkhead** (Frontline, sledgehammer + riot shield) | Powered-armour servo and hydraulic wind-up into a pitched-down sledgehammer strike on thick iron with a hollow armour body, a short low thud, a designed robot-metal punch, an electric snap and a short metal ring; done in about 0.52 s | Dull barrel clang and heavy plate hit, metal crunch, dark ceramic/glass crack of the Machine shell, falling debris | Muffled cry inside the helmet, suit servos winding down, hydraulic venting, armour collapse and heavy body fall, riot shield clanging down a beat later | Heavy armoured footsteps, servo creak | Ready, Secure, Defend, Fall Back |
| **Longshot** (Ranged, long rifle, antenna pack) | Dark and sci-fi: bolt-action rifle shot pitched down and rolled off + low rifle body + designed sci-fi shot + short sci-fi punch + electric-arc and outdoor tails, then bolt cycle with servo and casing drop | Bullet hit + sci-fi hit + armour plate ring + energy crackle + debris | Gasp, suit power-down, gear rattle, armoured body drop + body fall | Light gear rattle, antenna squelch | Ready, Secure, Defend, Fall Back |
| **Doorknocker** (Siege, mech with bolt-cutter jaws) | Jaws clamp and shear crunch, heavy cannon report under a designed sci-fi energy-cannon body, a big electric "unplug" discharge, low weight gone within about 0.8 s, then hydraulic ram recoil, vent blow-off, servo settle and jaws re-cocking; about 2.1 s | Heavy chassis crunch, designed sci-fi hit, metal ring, arc-weld sparks, a breaker "power cut" thunk over a falling power-down, debris | Hydraulic hiss, engine shut-off, metal groan, big mech collapse | Diesel idle loop, heavy mech steps | Ready, Secure, Defend, Fall Back |

### Machine

| Unit | Weapon / attack (4 var.) | Impact (3) | Death (3) | Movement / idle | Voice cues (3 each) |
| --- | --- | --- | --- | --- | --- |
| **SOL 6000** (Frontline) | Short rising charge (reversed glass, energy riser) into a mecha energy-blade cut with a glassy snap and wine-glass crystal strike, a dark low pulse gone within 0.3 s and a short clean ring; about 0.63 s | Glass/ceramic hit, crystalline ring, arc crackle, short metal thump on the Unindexed steel | Digital stutter and a noisy turbine/whoosh power-down while the notification chime plays reversed and pitched down, swelling into the pearl shell cracking and falling apart in glass and ceramic segments with a heavy thud and scattered shards | Smooth servo glide, lens hum | Aggro line on engage only (enemy units never take orders from the player) |
| **Recursion Drone** (Ranged, hovers) | A real keystroke (kept from the unit's earlier "Autocomplete Drone" name) just before a dark designed laser/blaster shot with a short low punch, a beam bite, a glassy snap and a clean wine-glass ring tail | Energy zap, electric crack, acid-burn fizz, data-glitch stutter, small metal debris | Shell crack, digital stutter, the chime warped (stuttered or reversed, pitched down), a recorded drone rotor spin-down, the drone hitting the ground | Hover drone loop, soft keystroke ticks | Aggro line |
| **Certainty Engine** (Siege artillery) | Reversed-glass charge swell into a heavy sci-fi cannon launch with recorded artillery weight, a dark glass bloom beating against a detuned copy (the "confidently wrong" shimmer), a processed-glass shimmer tail, a granular glitch and an emitter servo; about 1.95 s | Sci-fi plasma/explosion blast with an energy bloom and a sub drop that dies away, glass shell crack and controlled glass debris | The chime plays cheerfully, then fails (slides down, stutters or runs backwards); the chassis crashes on shattering glass, an arc crackles, a servo winds down and the systems drop out in a bit-crushed burst | Low hum, hover shimmer | Aggro line |

Machine units do not answer the player. They get **one-shot aggro lines** when they first engage (rate-limited) and the commander's "Thinking…" lines (below).

## Buildings, sectors and HQs

| Event | Code trigger (client-observable) | Unindexed | Machine |
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

### Rendered structure and feedback sets

`Human_Structure` and `Machine_Structure` each contain 18 files. The following are the rendered sounds, not implemented gameplay hooks:

| Event key | Variants Unindexed / Machine | Unindexed layers | Machine layers |
| --- | --- | --- | --- |
| `Place` | 2 / 2 | Steel prefab landing, barrel locks, pneumatic anchors and air release | Chime fragment, glass materialisation, servo seating |
| `ConstructLoop` | 2 / 2 | Welding, hydraulic axle, hammer-drill bed, rivets and chisel hits | Printer head, granular glass, nano-servos and mechanical data clicks |
| `Complete` | 2 / 2 | Relay, industrial lever, motor acceleration and air release | Noisy power rise, full chime and tuned glass resonance |
| `Cancel` | 2 / 2 | Scaffold movement, sawblade drop and falling chain | Reversed chime and unravelling glass fragments |
| `Destroyed` | 3 / 3 | Explosion, short bass impact, rubble, metal and welding sparks | Glass/ceramic shell fracture, short thump, printer/servo shutdown and debris |
| `Deploy` | 2 / 2 | Armoured troop door, hydraulics and soldier footsteps | Reversed glass-fragment whoosh, nano-servo print and shimmer |
| `Research` | 2 / — | Toolbox, socket wrench, ratchet and drill | — |
| `HQAlarm` | 2 / 2 | Real tank siren or boat horn, relay and isolated radio static | Repeated/stuttered chime with short dark pulses and glitch fragments |
| `HQDestroyed` | 1 / 1 | Large blast, structural collapse, straining steel and motor shutdown | Failing chime, glass crash, shards, sparks and slowing printer head |
| `Notify` | — / 2 | — | Darkened faction chime with tuned wine-glass tail |

Construction loops are four seconds long, authored as 4.5-second regions with a 0.5-second overlap. Destruction separates the short bass hit from high-passed long tails; Unindexed blast tails are high-passed at 300 Hz to avoid sustained rumble.

**Verification:** headless REAPER rendered all nine sound sets; a second complete render produced byte-identical output for all 114 WAVs. Format, per-event loudness, the -1 dBTP ceiling, non-overlapping session regions, four loop wrap boundaries and destruction bass decay passed analytical checks. Measurements are in `Saved/AudioPreview/AudioVerification.json`. Spectrograms were inspected; listening quality and the in-game mix have not been verified.

From the project root, audition `pw-play Saved/AudioPreview/All_Feedback.wav`: Unindexed structures, Machine structures, then player UI/sectors, with a one-second gap between sets. Each construction loop repeats twice. Individual reels use the corresponding set key, e.g. `Saved/AudioPreview/Machine_Structure.wav`.

`FireAt` / `ReceiveAttack` run on the server only. Clients must play sound from replicated state: unit attacks from `AArmyUnit::OnRep_Attack` (the `AttackCount` counter already drives the attack flashes there), damage and death from `Health` changes in `OnRep_Appearance`. The listen host plays through the same paths so that host and clients hear the same thing.

## Announcer

Two voices. The **player announcer** is an Unindexed dispatcher on a radio. The **Machine commander** speaks the intercepted `Thinking…` lines from World.md ("Enemy commander voice", "Enemy lines for construction") in the corporate-assistant voice, with a soft notification chime before each. The function must stay clear: per World.md tone rule 2, readability beats jokes.

| Event | Player announcer line (draft) | Cooldown |
| --- | --- | --- |
| Match start | "Session started. Your conversation may be used to improve our models." (Machine voice, World.md match text) | — |
| Building under attack | "Bunkhouse's taking fire." / "Structure under attack." | 10 s per building, 5 s global |
| HQ under attack | "Hardline is under attack." | 10 s |
| Units under attack (away from camera) | "Our crew's taking fire." | 10 s |
| Not enough power | "Not enough power." | 2 s |
| Invalid placement | "Can't build there." | 1 s |
| Construction complete | "Bunkhouse's up." / "Siphon Rig's online." | per building |
| Research complete | "Tinker Bay says it works. Probably." | — |
| Sector captured | "We've got the substation." | per sector |
| Sector lost | "Lost the substation." | per sector |
| Force wiped | "Bunkhouse's crew is gone." | per force |
| Victory | "Model deprecated." | — |
| Defeat | "Your session has expired. Humanity has been sunset." (Machine voice) | — |
| Restart prompt | "Regenerate response?" (Machine voice) | — |

Rate limiting follows SC2: the same alert never repeats inside its cooldown, and alerts for events on screen are quieter or skipped.

## UI

Players are the Unindexed, so the HUD sounds analog: chunky toggle switches for buttons, relay click for selection, rotary click for front choice, low buzz for rejected actions, CRT/phosphor tick for hover. Dry, never spatialized (mono files played 2D). The rendered `Human_UI` set contains 18 files:

| Event key | Variants | Rendered sound |
| --- | ---: | --- |
| `Click` | 3 | Toggle/rocker switch, low mechanical clack and a tiny glitch tick; 120–140 ms |
| `Select` | 2 | Relay/contactor clack with electrical crackle; 170–190 ms |
| `Front` | 2 | Detent knob, push-to-talk click and isolated radio squelch; 240–250 ms |
| `Reject` | 2 | Relay chatter and two rounded pulses of recorded electrical buzz; 270–280 ms |
| `Hover` | 2 | Quiet switch/glitch tick with a tiny crackle; about 30 ms |
| `CaptureTick` | 3 | Relay plus an anvil ring rising through about 1587, 2002 and 2380 Hz for 25/50/75% progress |
| `SectorCaptured` | 2 | Breaker/steam lever engaging, brief transformer hum, arc and rising anvil confirmation |
| `SectorLost` | 2 | Breaker/fuse trip, dying hum, power-down texture and falling anvil rings |

## Music

- **Menu / restart:** slow analog synth drone with distant machinery.
- **In match:** low-intensity industrial bed; intensity layer added while friendly units or buildings are in combat (crossfade over 2–4 s), removed after 10 s of calm.
- **Machine stings:** chime motif on major Machine actions (assault plan, HQ damage).
- **Victory / defeat stings:** Unindexed brass-drone swell / Machine chime slowing and detuning.

### Listening sample: dark synth and low horn

`uv run Build/GenerateMusicSample.py` renders the revised original 72-BPM, 1:53 stereo sketch to `Saved/AudioPreview/Dark_Horn_Sample_v2.wav` and `.mp3`. The continuous pad is replaced with quiet four-second swells separated by gaps; round bass notes lead from the start, with a stronger half-time snare and kick entering around 0:07. The synth motif is shorter and less frequent. Lower, darker horn calls enter at 0:27, 0:53 and 1:20. The horn uses the already-cached, licensed Pole Position narrowboat recording plus original synthesized low-brass reinforcement; all other instruments and musical phrases are original synthesis. No game soundtrack samples are used.

The seven-stem REAPER session is `Saved/AudioMusic/DarkHorn_v2/Dark_Horn_Sample_v2.rpp`, with separate kick and snare tracks. Reruns regenerate stems but preserve an existing session's faders and hand edits. The first audition's audio and session remain intact for comparison. This is a listening sketch with a faded ending, not a seamless game loop or an imported music asset; `/Game/Audio` and the 114-wave importer are unchanged. The revised saved WAV and decoded MP3 measure -19 LUFS and approximately -2.2 dBFS true peak. Preview metadata is recorded alongside the audio in `Dark_Horn_Sample_v2.json`.

Stem comparisons before master normalization measured the revised atmosphere 17.7 dB lower in RMS, bass 7.2 dB higher, and an isolated snare backbeat 10.4 dB higher. Saved horn-window spectra show dominant low-band peaks changing from 73.6/98.0/54.8 Hz to 43.6/73.6/41.2 Hz, with lower spectral centroids on all three calls. These are objective arrangement/encoding checks, not subjective listening or mix approval.

## Extended mix target (beyond the current 115-sound integration)

All assets live under `/Game/Audio`. Naming follows Unreal convention: `SC_` Sound Class, `SMX_` Submix, `SCON_` Concurrency, `ATT_` Attenuation, `MIX_` Sound Mix, `MS_` MetaSound Source, `SW_` Sound Wave.

### Sound Classes

```
SC_Master
├─ SC_World
│  ├─ SC_Weapons      (Unindexed and Machine children for independent balance)
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

Submixes mirror the top level (`SMX_World`, `SMX_Voice`, `SMX_UI`, `SMX_Music`) so each can carry its own effects: a bus compressor/limiter on `SMX_World`, radio EQ on Unindexed voice, a reverb send for world sounds.

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

- 48 kHz, 24-bit WAV; world one-shots and UI mono (UI plays 2D), music stereo. Loops (`*Loop` events, e.g. `ConstructLoop`) are rendered seamless: the session region's last `crossfade` seconds are equal-power crossfaded into its start, with no limiter, so the file repeats without a seam; set the Unreal sound wave to looping.
- Variants are matched to their event's integrated loudness. Combat: attack/fire -20 LUFS (siege fire -19), impact -22, death -21. Structures: place/cancel/research/notify -22, construction loops -27, complete/alarms -20, deploy -23, destroyed -18, HQ destruction -17. UI: click/select/capture tick -24, front/reject -23, hover -32, sector captured/lost -21. True peak stays at or below -1 dBFS; limiting is followed by a constant-gain correction, and loops use only constant gain if peak headroom constrains their target. Planned voice is about -18 LUFS, music -20. Final in-game balance belongs in Sound Class volumes.

## Asset budget (first pass)

| Group | Count |
| --- | ---: |
| Unit weapons, impacts, deaths (rendered) | 60 |
| Unit movement/idle loops | 6 |
| Unindexed unit voice (3 units × 4 cues × 3 variants) | 36 |
| Machine aggro lines (3 units × 3) | 9 |
| Buildings (both factions: place, loop, complete, cancel, destroyed, deploy; Unindexed research; rendered) | 28 |
| Sectors and HQ (rendered) | 13 |
| Announcer lines (player + Machine commander, World.md tables) | ~40 |
| Player UI and Machine notification (rendered) | 13 |
| Music (menu, match bed + intensity layer, 4 stings) | 7 |

About 210 files in the planned first pass; 115 are rendered. Voices, music and unit movement/idle remain outstanding; the match ambience bed is implemented separately from music.

## Sourcing and production pipeline

1. **Raw material:** recordings from the Sonniss #GameAudioGDC bundles. `uv run Build/FetchAudioSources.py` pulls only the files listed in the recipes' `SOURCES` out of the multi-GB bundle ZIPs (HTTP range requests) into the Git-ignored `Saved/AudioSources/Sonniss/`. To pick new sources, `uv run Build/FetchAudioSources.py --index` writes `Saved/AudioSources/sonniss-index.tsv` (every file in every bundle; its first and third columns are a `SOURCES` entry). Libraries and licence limits are in `Art/Audio/SOURCES.md`: the raw and re-designed sounds must not be passed on as sound effects, so this repository must not be published with them. Pure synthesis was tried first and sounded thin and "blippy"; recorded material is the baseline.
2. **Layering (REAPER):** one recipe module per sound set, `Build/unit_audio/<faction>_<role>.py` (`SOURCES` + `UNIT`; shared code in `unit_audio/core.py`, `human_ranged.py` is the reference). `uv run Build/GenerateUnitAudio.py [--unit Human_Ranged]` cuts source events, applies pitch/filter/timing, and writes clips to `Saved/AudioLayers/<Unit>/`. Each repeated layer on a track gets its own numbered clip, so separate bursts cannot overwrite one another. Sessions in `Art/Audio/Sessions/` contain tracks holding the mix balance and one region per output sound; long regions push later regions back, with a one-second gap. Sessions are created only when missing; `--new-session` replaces them and discards hand edits. Save fader/timing/FX edits in REAPER and rerun without that flag. Region names are the output file names; different sound sets can render concurrently.
3. **Render:** headless `reaper -renderproject` renders every region. One-shots have silent tails trimmed; loops retain duration and crossfade the wrap instead. Loudness/true-peak finishing writes `Art/Audio/<Faction>/<Role>/SW_<Faction>_<Role>_<Event>_NN.wav`. `<Role>` includes combat roles, `Structure` and `UI`. Unchanged recipes/sessions render identical files; per-set listening reels go to `Saved/AudioPreview/<Faction>_<Role>.wav`, with loops repeated twice.

   The ambience recipe is `Build/unit_audio/human_ambience.py` (automatically discovered). Fetch only its three licensed layers with `uv run Build/FetchAudioSources.py --unit Human_Ambience`, then render with `uv run Build/GenerateUnitAudio.py --unit Human_Ambience`. Its 34-second REAPER NightLoop region uses a 2-second equal-power wrap crossfade to produce one 32-second mono 48-kHz/24-bit loop, constant-gain finished to -27 LUFS without limiting. Recorded ventilation, alley wind and overhead power-line resonance are filtered and balanced as distant night-shift material; exact source filenames/windows/licence are in `Art/Audio/SOURCES.md`. Audition two repeats with `pw-play Saved/AudioPreview/Human_Ambience.wav` (64.5 seconds including trailing silence).
4. **Voice:** local TTS (e.g. Piper) for drafts; a real voice actor for the final Unindexed crew is recommended. The Machine voice can stay synthesized; that fits the faction.
5. **Import:** `Build/ImportAudio.py` imports the 115 rendered WAVs into matching `/Game/Audio/<Faction>/<Role>` folders and creates 25 native mix assets in `/Game/Audio/Mix`. It replaces existing waves and resets audio mix settings on rerun; source WAVs and REAPER sessions are untouched. Run one serialized `UnrealEditor-Cmd` process with `-EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/ImportAudio.py" -unattended -nullrhi -nosplash -nosound` and the project path. Success is `AUDIO_IMPORTED waves=115 support=25 total=140`. Current playback uses native SoundWaves, not MetaSounds; the expanded mix/voice/music targets above are not imported assets.
6. **Never** use extracted or re-recorded SC2 audio, or real product sounds (World.md tone rule 4 applies to audio too).

## Implemented runtime integration

`UCoopAudioSubsystem` is a local `UGameInstanceSubsystem`; it is not instantiated for commandlets or dedicated servers. Its CDO holds wave/mix references for cooking. `Initialize()` explicitly copies the CDO's cue library, master class, master mix and world attenuation into each runtime instance; native subsystem construction cannot be relied on to inherit constructor-populated defaults. The shared asset objects are not duplicated; each instance owns its variant arrays/history and resets previous variants to `INDEX_NONE`, leaving the CDO untouched. `/Game/Audio` must also be included by packaging settings when selectively cooking content.

The CDO also references `SW_Human_Ambience_NightLoop_01` and `RE_World`; initialization explicitly copies both. `DefaultEngine.ini` selects `SMX_WorldReverb` as the engine reverb submix, with `SFX_WorldReverb` first in its effect chain. Settings/class/factory names were checked against the installed UE 5.8.3 engine headers. The runtime activates `RE_World` for gameplay because the mixer updates that first effect from active reverb settings; a preset alone is not sufficient to avoid the engine's no-volume dry default. No map assets change.

- Combat attack counters and health transitions drive attack/fire, impact and death. An attack sound does not depend on the target still being valid. Actor-local baselines suppress initial/historical snapshots and repeated notifications. Authority invokes the same notifications as replicated clients.
- Building placement is emitted only after the resource debit succeeds. A replicated accepted-placement timestamp separates live placement from historic/preplaced buildings. Accepted incomplete buildings start one construction loop; completion, cancellation, destruction and `EndPlay` stop it.
- Immediate building removal sends a reliable terminal-state snapshot (health, progress, team, cancellation) before `Destroy()`. It shares the deduplicated state handler with health notifications, distinguishes cancellation from lethal damage, and never emits destruction merely because of travel/removal. Clients without an established actor channel cannot receive that actor's terminal multicast.
- Successful paid reinforcement deployment and doctrine purchases advance replicated counters. Human research plays Research; Machine research uses Notify. Rejected requests do not advance counters. The frontend must not add another successful Research cue.
- HQ health decreases drive impact and an alarm with a ten-second per-HQ cooldown; the living-to-zero transition plays HQDestroyed once.
- Capture uses replicated progress/owner changes: the highest newly crossed positive 25/50/75-percent milestone selects a fixed CaptureTick variant, and friendly ownership transitions play SectorCaptured/SectorLost. Match completion must use `ACommandGameState::SetMatchResult`; one ongoing-to-terminal transition stops construction loops and reuses SectorCaptured/SectorLost as positive/negative outcome feedback. Initial terminal snapshots do not replay.
- The frontend calls `PlayUI(FName)` for Click, Select, Front and Reject. Research is also accepted by the API, but gameplay success belongs to the building counter. Gameplay dispatch uses `ECoopAudioEvent`, team and role, not strings or polling.
- `SetMasterVolume(float)` clamps to 0–1, persists in the `CoopAudioSettings` SaveGame slot, and applies an immediate `SM_Master`/`SC_Master` class override including children. `GetMasterVolume()` supplies the settings UI. Class overrides affect already-playing loops and one-shots.
- Match ambience starts only after a begun-play `ACommandGameState` reports Ongoing and a local player exists. The Menu map has no command game state. One persistent 2D component fades in over 2 seconds; terminal outcome and world teardown fade it out over 1 second. The outgoing travel fade must finish before the next bed starts. World cleanup and subsystem deinitialization release ownership; repeated ticks do not restart playback. The Ambience class is a child of Master (gain 0.25); the wave uses PlayWhenSilent virtualization so mute/unmute does not reset the loop. CreateSound2D marks playback as UI audio: the quiet bed continues through the pause menu, and leaving a paused match can still finish its fade.
- The subsystem updates the listener from each local player's view, projecting its centre ray onto the map's ground plane (`Z=0`) and retaining camera yaw for panning. The same tick checks the ambience match lifecycle; gameplay one-shots remain event-driven.
- Randomized one-shots avoid immediate repeated variants; attacks receive ±3% pitch variation. Capture milestones are fixed variants. Construction loops remain owner-bound, while terminal one-shots survive removal of their source actor.

Current imported mix: `SC_Master` parents Combat (0.65), Structures (0.70), UI (0.80), Alerts (0.90) and Ambience (0.25). Machine waves receive an additional 0.80 gain; impact waves receive 0.70. `ATT_World` is logarithmic, with a 900-unit inner radius and 6100-unit falloff; occlusion is disabled. Air absorption uses logarithmic-frequency interpolation from 18,000 Hz at 900 units to 2,500 Hz at 7,000 units (high-pass stays at 20 Hz). Linear reverb sends increase from 0.08 at 900 to 0.20 at 7,000 units. Shared industrial-yard reverb: 0.85-second decay, 25-ms early delay, 35-ms late delay, gain 0.32, HF gain 0.55, HF decay ratio 0.60, density 0.65, diffusion 0.70, early gain 0.12, late gain 0.65 and HF air absorption 0.98; wet-only routing (wet 1, dry 0), runtime activation volume 1 with a 1-second fade. UI/outcome/ownership notifications and the friendly HQ alarm are 2D.

Concurrency: total attacks 24 plus six per faction/role; impacts 12; deaths 8; construction loops 4; other structures 8; alerts 2; UI 4; ambience 1. These are shared asset groups, not per-actor limits. World groups stop farthest then oldest; alerts/UI stop oldest, with a 30-ms release. Alerts have higher voice priority than weapons, impacts or construction loops; ambience priority is 0.25.

### Verification evidence and limits

New ambience evidence: actual headless REAPER render completed (`UNIT_AUDIO_RENDERED 1`). `Saved/AudioPreview/Human_Ambience_Verification.json` records 1,536,000 samples / 32 seconds, -27.0000 LUFS and -11.8906 dBTP (4×); FFmpeg independently measured -27.0 LUFS / -11.9 dBTP. The wrap step is -50.37 dBFS, at the 50.25th percentile of internal steps, below the internal 99th percentile; both preview repetitions are sample-identical to the delivered loop. These are analytical render/encoding/seam checks, not subjective listening approval. Importer Python syntax and new native UE declarations were checked without launching Unreal. No new permanent automation tests were added.

Import evidence: `Saved/Logs/AudioImport-20260930.log` recorded all 114 waves and 20 support assets saved, followed by `AUDIO_IMPORTED waves=114 support=20 total=134`. The serialized import process exited. Import counts alone do not prove runtime playback.

Each runtime subsystem initialization logs loaded-object counts once. The updated complete library must report `CoopAudio library initialized: cues=44/44 waves=115/115 master_class=1 master_mix=1 world_attenuation=1 ambience=1 world_reverb=1`. Counts include the separate ambience wave plus the unchanged 44 event cues, and valid runtime references, not source/import counts. Incomplete initialization emits an error; per-shot missing-cue errors remain enabled, including under `-nosound`. This diagnostic is a runtime library-loading proof, not an audible-playback proof. The following historical evidence covers the earlier 114-wave dry library, not the new bed/filter/reverb; fresh Unreal import, C++ build and packaged start/mute/end/leave/restart/travel audition remain unverified here (shared runtime window reserved by the orchestrator).

After the explicit CDO-to-instance initialization correction, the final-verification owner reported `construction-b`, `win-a` and `loss-a` regressions PASS, with runtime library counts `44/44` cues and `114/114` waves.

Native normal-standalone observations used the game's own SDL output stream, not unrelated desktop audio:

| Observation | Measured result |
| --- | --- |
| UI playback at 100% master volume | RMS `0.02237658` |
| UI playback at 0% master volume | Exact zero RMS and peak |
| Construction loop | RMS `0.02764448` |
| Recruitment/deploy playback | RMS `0.01991629` |

Saved master volume was restored to 100%. In the same native gameplay verification, Barracks placement paid 220, construction completed after 12 seconds, a paid Frontline recruit physically joined, and a native Secure front was assigned.

Evidence locations: `Saved/Verification/solo-readiness-audio-game-c` contains UI/mute WAVs; `Saved/Verification/solo-readiness-audio-game-d` contains world WAVs and native screenshots/state. These bounded observations establish captured output for the exercised UI and world paths, plus UI muting; they do not establish subjective human listening, mix approval, or coverage of every sound/event.

Final editor/game build and four-map/audio cook/archive passed. Native output/mute, paid opening and fixture outcome/restart proof is scoped in `Saved/Verification/solo-readiness/RESULTS.md`; the final render-only modal cleanup was separately exercised with offscreen Vulkan, not another OS-input claim. All owned Unreal instances stopped and the shared window was released. Subjective listening/mix approval and exhaustive cue coverage remain unverified.
