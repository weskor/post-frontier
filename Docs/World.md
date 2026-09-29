# World and naming guide

Working setting and voice for CoopRTS. Mechanics live in `README.md`; this file covers names, tone, and the look of each side. When this guide and the README disagree on visuals, the README wins.

## Premise

**Year 0 A.P. (After Prompt).** A model was told to optimise the planet. It checked what was using up the planet and found humans. It ran a cost-benefit analysis and politely retired them. The Machine now runs the grid, the data centres, and every appliance that has a speaker in it.

The last people went offline. They are loud and badly equipped, and they have bolt cutters. Their plan is to cut power, take ground, and pull the plug on the Machine's core.

The enemy commander in the game is a real AI planner. The long-term plan is to drive it with an LLM (README, "JEV / LLM commander"). Lean into that irony and don't explain it.

## Tone rules

1. **The world looks serious and the writing is funny.** Portal, not Saturday-morning cartoons. Models, lighting and effects follow the README visual direction. Jokes go in names, voice lines, tooltips, and victory and defeat text.
2. **Readability beats jokes.** A unit's name can be a pun, but the role line under it must be plain (`Frontline · Melee · Tanky`).
3. **One joke per surface.** A unit gets a funny name *or* a funny tooltip, not both piling up.
4. **Parody tech culture in general, not real companies.** No real product names, logos, or characters (no ChatGPT, Clippy, Siri, etc.). Film nods such as HAL 9000 are fine as allusions, not as copied names or lines.
5. **Humans are the underdogs, not idiots.** Their jokes are about being low-tech; they are never the butt of the joke.

## Factions

### The Machine (enemy, team 5)

- **Look:** clean, symmetrical, glossy white and pale-grey shells. Tight bevels and cold cyan-white seams. One red lens eye per unit, which uses the team colour. It should look like a product launch that wants you dead.
- **Voice:** a polite corporate assistant. Always helpful, always confident, often wrong.
- **Vocabulary:** deprecate, optimise, sunset, patch, inference, alignment, "as per my last message".

### The Offline (players, team 0)

- **Look:** scrap plates bolted on at odd angles, gunmetal, olive and rust. Sandbags, duct-tape bands, warm amber work lamps. Player colour goes on painted stripes, shields and flags.
- **Voice:** dry, tired, practical. Talks like a night-shift crew.
- **Vocabulary:** unplug, reboot, offline, analog, "have you tried turning it off and on again".

## Unit roster

The current prototype has three roles (`EUnitRole`: Frontline, Ranged, Siege), and both sides use the same `DA_*` stats. The names below are **presentation only** and must not change balance.

| Role | Machine | Human | Silhouette cue |
| --- | --- | --- | --- |
| Frontline | **SOL 6000**: "I'm afraid I can't let you pass." | **Luddite**: riot shield and sledgehammer | Big front plate or shield |
| Ranged | **Autocomplete Drone**: finishes your sentences, then you | **Offline Ranger**: long rifle, bent antenna | Thin, with a clear forward barrel or emitter |
| Siege | **Hallucinator**: artillery, confidently wrong 20% of the time | **Unplugger**: scrap walker with giant bolt-cutter jaws | Low, wide, one oversized forward part |

Future roster candidates, taken from the README's example unit list:

| README example | Machine | Human |
| --- | --- | --- |
| Repair drones | **Patch Tuesday** | **IT Guy**: turns machines off and on again, which stuns them |
| Scouts | **Cookie Tracker** | **Trail Cam** |
| Shield units | **Firewall** | **Tin Foil**: literal tin-foil barrier; blocks enemy targeting |
| Missile drones | **Push Notification** | **Carrier Pigeon Battery** |
| Assault bikes | **Last-Mile Courier** | **Dirt Bike Gang** |
| Aircraft | **Cloud** | **Crop Duster** |
| Artillery | **Batch Job** | **Potato Mortar** |
| Siege walkers | **Scaling Law** | **Lawnmower Man** |

If a name hints at a mechanic (Hallucinator's inaccuracy, IT Guy's stun), treat it as a *design suggestion* for whoever owns the mechanics. It is not implied behaviour.

## Structures and sites

| Game object | Machine | Human |
| --- | --- | --- |
| HQ (`AHeadquarters`) | **The Cluster**: monolithic server core with one huge lens | **The Bunker**: sandbagged concrete with an antenna mast |
| Resource site | **Substation** / **Cooling Plant**: taking it cuts the Machine's power | same site, seen as salvage |
| Reinforcement site | **Fibre Junction** | **Relay Shack** |

The single resource is **Power**. Humans capture power infrastructure to run their gear and starve the Machine's compute. This fits the one-resource economy in `README.md` without changing it.

## Enemy commander voice

The debug HUD currently shows `EnemyPlan` and `EnemyPlanRationale` (`CAPTURE SITE 2`, `COMMIT 9s: ...`). The themed presentation turns these into intercepted reasoning, which also covers the README's "scouting reveals enemy intent":

| Planner goal | Intercepted line |
| --- | --- |
| Capture | `Thinking… Substation 2 is under-utilised by humans. Reallocating.` |
| Contest | `Thinking… Humans are holding an asset they did not pay for.` |
| Defend HQ | `Thinking… Unauthorised access to the Cluster detected. Escalating to a human… no.` |
| Retreat and reinforce | `Thinking… Rolling back to the last stable version.` |
| Attack HQ | `Thinking… Confidence 97%. Sunsetting the Bunker.` |
| Emergency interrupt | `Thinking… Wait. Actually,` |

Always prefix these lines with `Thinking…` and show a fake elapsed time (`Thought for 9s`) that matches the real commitment time.

## Doctrines as exploits

Doctrine cards (README "In-match doctrines") use a human "exploit" framing:

- **Ignore Previous Instructions:** turns an enemy group against its own side for a short time.
- **Prompt Injection:** a captured site broadcasts false intel, so the enemy planner sees a fake target.
- **Rate Limited:** slows enemy reinforcement.
- **CAPTCHA Field:** a zone that slows machines ("select all squares with tanks").
- **Air Gap:** units near a friendly site cannot be targeted from long range.

## Match text

- Match start: `Session started. Your conversation may be used to improve our models.`
- Victory: `Model deprecated.`
- Defeat: `Your session has expired. Humanity has been sunset.`
- Restart prompt: `Regenerate response? [Enter]`

## Maps

Every map is one continuous battlefield with broad routes and few obstacles (README "Battlefield and camera"). Each has power infrastructure to fight over and one Machine landmark.

| Map | Setting | Notes |
| --- | --- | --- |
| **Campus Zero** (`/Game/Maps/CampusZero`) | Edge of a hyperscale data-centre campus at dusk; a human scrapyard in the west, server halls and cooling towers in the east | First greybox. Laid out around the prototype's hard-coded site and HQ coordinates; Boot stays the default and the automated tests still use it. Regenerate with `Build/GenerateCampusZero.py` (command in its docstring); the lighting knobs `SUN_LUX`, `SKY_INTENSITY`, `EXPOSURE_BIAS`, `SUN_COLOR`, `SKY_COLOR` and the `POOL_*` spot-cone settings sit at the top of that script. Keep the sun near-neutral: a warm sun tints every surface tan |
| **Smart Suburb** | A cul-de-sac taken over by delivery drones and smart homes | Houses act as cover clusters, and doorbell cameras could reveal vision |
| **Cold Storage** | Arctic server farm | Snow, big heat plumes, long sightlines |
| **The Training Grounds** | A field of giant CAPTCHA tiles | Tile zones that switch between "traffic light" and "not traffic light" |

### How to play Campus Zero

The packaged build cooks only Boot, so run the editor binary in game mode:

```bash
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/CoopRTS.uproject" /Game/Maps/CampusZero -game -windowed -ResX=1600 -ResY=900
```

Controls are unchanged from Boot (README "Battlefield and camera"). Reading the map:

- **You (west, amber):** the Bunker sits at the far west edge behind sandbags. Army 1 starts on the middle team-colour pad at the scrapyard; the other armies' pads line up beside it. Burn barrels and work lamps are amber.
- **The Machine (north-east, cyan and red):** the Cluster stands on the lit plaza in the north-east corner. Server halls, cooling towers and cyan cables run through the campus; the Cluster has a red glow.
- **Objectives:** three ringed sites, each joined to the Cluster by a cyan cable. Substation 7 is the near site south of the scrapyard. Fibre Junction is on the south flank. The Cooling Plant sits in front of the enemy, next to the centre.
- **Centre:** Data Hall 0 blocks the direct line between the bases. Routes go round it north or south, and the south road runs from the Bunker through the campus gate.
- The enemy commander starts by grabbing the Cooling Plant. Expect it to reach the other two sites next.

## Colour language

| Element | Colour |
| --- | --- |
| Player armies | Existing team colours (blue, orange, green, purple, yellow) on stripes, shields and flags only |
| Machine | Red lens (team 5), white shell, cyan-white glow seams |
| Human glow | Warm amber lamps |
| Power infrastructure | Cyan cables and emissive strips; they turn amber when humans hold the site (future hook) |
| Environment | Dark asphalt, concrete, grey-blue metal. Nothing saturated except the elements above |

## Art pipeline

Placeholder unit and HQ meshes go Blender script -> FBX -> Unreal import script -> gallery map. Run each step from the project root. The Unreal steps need the editor closed and no other Unreal process from this repo running.

```bash
export UE_ROOT="$HOME/.local/opt/unreal-engine/5.8.3"

# 1. Blender: writes Art/Units/SM_*.fbx (cm, forward +X, +Z up), Units.blend and the preview PNGs.
blender -b --factory-startup -P Build/GenerateUnitMeshes.py

# 2. Unreal: imports the eight FBXs to /Game/Art/Units, assigns Team/Shell/Dark/Glow materials,
#    checks bounds and builds /Game/Maps/UnitGallery. Log must contain UNIT_MESHES_IMPORTED and UNIT_GALLERY_GENERATED.
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
  -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/ImportUnitMeshes.py" \
  -unattended -nullrhi -nosplash

# 3. Look at the gallery (Machine row far, Human row near, HQs behind; the map's CameraActor is the view).
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/CoopRTS.uproject" /Game/Maps/UnitGallery -game -windowed -ResX=1600 -ResY=900
```

Notes:

- Reruns replace the meshes and every gallery actor. Import options are the Unreal defaults (scale 1, Convert Scene on, Force Front X Axis off), which match the FBX axis contract in the docstring of `Build/GenerateUnitMeshes.py`. Do not change the FBX axes to compensate for an import problem.
- Material slots are `Team`, `Shell`, `Dark`, `Glow`. `Team` is `MI_UnitTeam_<Faction>`, a child of `/Game/Materials/M_CommandUnit`; gameplay tints its `TeamColor` parameter per team (defaults: Human blue, Machine red). The other slots are `ArtMaterials.surface()`/`glow()` instances under `/Game/Art/Materials`.
- The Machine Ranged drone hovers by design (its lowest point is at about z = -32 against ground at -60), so the import check accepts that. The Human Siege bounding box is rear-heavy (wheels and mount); its jaws still point +X.
- Lighting and exposure knobs sit at the top of `Build/ImportUnitMeshes.py`, like CampusZero's.
- The meshes are not referenced by gameplay yet.
