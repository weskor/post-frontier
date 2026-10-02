# Workflow audit: Post-Frontier (CoopRTS), current state, 2026-10-02

Scope: git, shared mutable state, entry points, agent guidance, doc sprawl and workaround paths. I only read files and ran cheap shell commands. No Unreal, UBT, UAT or harness process was launched. Paths are relative to `/home/wes/workspace/game` unless they are absolute. `[INFERENCE]` marks claims I did not observe directly.

---

## 1. Git facts

| Fact | Value | Evidence |
|---|---|---|
| Workspace root `/home/wes/workspace` | **Not** a git repo. It contains `game/` next to unrelated projects (`copia`, `den`, `draft`, `hfac`, `koers`, …). | `git rev-parse` in the root → "not a git repository" |
| `game/` | Git repo with top level `/home/wes/workspace/game` | `git rev-parse --show-toplevel` |
| Branches | Only `main` (local). The merged lane branches (`lane/p3-income`, `lane/p8-map`, `lane/rules-extraction`, `lane/enemy-economy`, `lane/group-encapsulation`) have been deleted. | `git branch -a`; reflog |
| Worktrees | Only the main checkout. Earlier lanes used `/home/wes/workspace/game-lanes/<lane>` worktrees, which have since been removed. | `git worktree list`; `Saved/AgentBriefs/restructure-wave3-lanes.md:3,9,26,41`; `Saved/AgentBriefs/map-lanes-p8-p3.md:3,7,17` |
| Remotes | **None**. No push target, no server-side checks, and LFS locking is impossible (`git lfs locks` → `missing protocol: ""`). | `git remote -v` is empty |
| LFS | Installed locally (`filter.lfs.*`, `lfs.repositoryformatversion` in local config). 370 LFS files: 190 `Art/`, 180 `Content/`. **No `lockable` attribute.** | `.gitattributes:8-14`; `git lfs ls-files` |
| `.gitattributes` | `* text=auto`; LF for `cs/cpp/h/ini/uproject/sh`; LFS for `*.uasset *.umap *.fbx *.blend`, `Art/Textures/**/*.{png,jpg}`, `Art/Audio/**/*.wav`. Other PNGs under `Art/` (153 tracked PNGs in total) are plain git blobs. | `.gitattributes:1-14` |
| `.gitignore` | Ignores `/Binaries/ /DerivedDataCache/ /Intermediate/ /Saved/ /Builds/`, IDE files, `/Makefile`, `/compile_commands.json`, `/Build/Linux/FileOpenOrder/`, `__pycache__/`, `*.blend1`, `*.rpp-bak`. | `.gitignore:1-19` |
| Tracked vs ignored | Tracked: 741 files (`Art` 431, `Content` 180, `Source` 59, `Build` 39, `.agents` 18, `Docs` 5, `Config` 4, plus root files). Nothing under `Saved/Intermediate/Binaries/Builds/DerivedDataCache` is tracked (0 files). `Content/*.uasset/*.umap` are tracked through LFS. | `git ls-files` counts |
| On-disk sizes | `.git` 483 M (pack 138.75 MiB); `Saved/` **6.1 G** (Verification 3.3 G, StagedBuilds 646 M, Cooked 194 M); `Builds/` 2.2 G; `Intermediate/` 1.3 G; `Binaries/` 939 M; `Art/` 399 M; `Content/` 38 M; `Source/` 832 K | `du -sh`, `git count-objects -vH` |
| History | 27 commits from 2026-09-28 to 2026-09-30, one author (Wesley van de Korput). Subject lines only, with no bodies. Style is `Area: summary` (`Art:`, `Maps:`, `Content:`, `UI:`), `Restructure wave N: …`, `Lane <name>` and `Merge lane/<x>`. Commits are large batches: 5–197 files each. | `git log` |
| Hooks | `.git/hooks/{post-checkout,post-commit,post-merge,pre-push}` are the stock git-lfs hooks. **No pre-commit hook and no custom hooks.** `core.hooksPath` is unset. | `.git/hooks/pre-push` |
| CI | None. No `.github/` directory and no other CI config. | `ls .github` |

### Current `git status` (summary; nothing was changed)

- **125 tracked files modified**: +5121/−1313 lines in total; `Source/` 35 files (+2903/−752).
  - Largest diffs: `CommandHUD.cpp` +484/−115, `CommandPlayerController.cpp` +373/−70, `RulesTests.cpp` +283/−100.
  - Binary changes: 47 `Content/` binaries (35 `Content/Art/Materials/MI_AZ_*`/`MI_*`, 8 `Content/Materials`, 3 `Content/Maps`) and 10 `Art/Audio/*.wav`.
  - Also modified: all 13 skill files, 7 `Build/Generate*`/layout scripts, both `Config/*.ini` files, `CoopRTS.uproject` and `README.md`.
- **286 untracked files** (61 status entries).
  - Includes `AGENTS.md`, `Build/SimulateMatches.py`, `Docs/Design.md`, `Docs/Design/`, `Docs/Research/`, `Docs/Balance.md`, `Content/Maps/AvailabilityZoneV2.umap`, `Content/Maps/Menu.umap`, `Content/Audio/` and `Content/Materials/Overlay/`.
  - Also 22 new `Source/CoopRTS` files, among them `MatchSimulationSubsystem.*`, `SimulationSettings.*`, `CoopSessionSubsystem.*`, `CoopAudioSubsystem.*`, `ForceGoals.*`, `GoalOrderTests.cpp`, `ForceIdentityTests.cpp` and `Rules/GoalPath.*`.
- **Consequence:** HEAD (`8b0a97f`, 2026-09-30) is far behind the real working state. A worktree or clone made from HEAD would lack `AGENTS.md`, the simulation harness, the Steam and audio subsystems, the menu and V2 maps, and two test files. The working tree is the de facto source of truth, and only the owner commits. Briefs forbid agents from running any git write command (`/tmp/cooprts-work/COMMON2.md:16`).

---

## 2. Shared mutable state and parallel-agent risk

| # | Resource | Who writes/holds it | Isolation today | Parallel-agent risk |
|---|---|---|---|---|
| 1 | **Unreal lock `/tmp/cooprts-work/ue.lock`** (0 B, created 2026-10-01) | Taken only by `Build/SimulateMatches.py:211` (`fcntl.flock`, default `--lock` at `:499`). Docs tell agents to wrap commands in `flock`: `features/multiplayer.md:56`, `Docs/Maps/AvailabilityZoneV2.md:36`, `Docs/Balance.md:50`, and the out-of-repo brief `/tmp/cooprts-work/COMMON2.md:18`. | **Convention only.** `verify.py`, `network.py`, `hud_capture.py`, `network_desktop.py`, `Build.sh`, `RunUAT.sh` and the generator commands in README/World.md never take it. The lock lives in `/tmp` (lost on reboot), and the rule that defines it lives outside the repo. | **High.** An agent that follows only `SKILL.md`/README runs unlocked. Wrapping `SimulateMatches.py` in `flock`, as the COMMON2 rule says to, self-deadlocks; `Docs/Balance.md:32` warns against exactly that. Contradictory rules. |
| 2 | Engine install `~/.local/opt/unreal-engine/5.8.3` | README shader patch edits `Engine/Shaders/Private/ShaderPrintCommon.ush` (`README.md:134-140`). Development Steam init writes and deletes `Engine/Binaries/Linux/steam_appid.txt` for editor-hosted game processes (`README.md:204`). | None. One install shared by all checkouts and projects. | Medium. Concurrent editor `-game` Steam runs race on the same `steam_appid.txt` `[INFERENCE]`. A patch reapply affects every checkout. |
| 3 | DDC | Engine ZenLocal server (`[::1]`, namespace `ue.ddc`), plus `~/.config/Epic/UnrealEngine/Common/DerivedDataCache` (307 M) and the engine `Compressed.ddp` pak. Project `DerivedDataCache/` holds only `VT` (1.6 M). | Per-user and shared, but designed for concurrent multi-process use. | Low. One shared zenserver per user (log: "No current process using the data dir found, launching a new instance", `Saved/Logs/CoopRTS.log`). |
| 4 | `Intermediate/` + `Binaries/` (1.3 G + 939 M) | `Build.sh CoopRTSEditor` and BuildCookRun `-build` (which rebuilds **both** `CoopRTSEditor` and `CoopRTS`; see `Saved/Verification/round2/development-package.txt` "Running … -Target=CoopRTSEditor … -Target=CoopRTS"). UBT picks its adaptive non-unity working set with `git status` (same log). | One build output per checkout. `verify.py:86-100` / `regression` (`:286-287`) **detect** a module change mid-run and fail; they do not prevent it. UBT `-WaitMutex` serialises UBT engine-wide `[INFERENCE]`. | **High within one checkout:** any agent's build invalidates every other agent's running test or editor. A worktree avoids that but costs a full ~2.2 G build tree per checkout `[INFERENCE: not measured for a cold build]`. |
| 5 | `Intermediate/` asset-registry cache, `Saved/Config/LinuxEditor`, `Saved/Autosaves`, `Saved/Logs/CoopRTS.log` | Every editor or `-game` process in the checkout. Lock files are visible at `~/.config/Epic/_____________________workspace_game_Intermediate_AssetRegistryCacheLock`. | Harness runs isolate their logs with `-abslog`. `SKILL.md:187` admits "Unreal may share saved settings". | Medium. Default logs get overwritten, and shared GameUserSettings can be touched. |
| 6 | Shared UBT/UAT logs in `~/Library/Logs/Unreal Engine/LocalBuildLogs/` | Fixed names (`UBA-CoopRTSEditor-Linux-Development.txt`, `Log.txt`, `Cook-*.txt`) | None. Global per user. | Medium. A second build overwrites the first agent's build log. |
| 7 | **`Builds/Linux`** (Development) and `Builds/LinuxShipping` | BuildCookRun `-archivedirectory=$PWD/Builds` (`README.md:164`). Read by `verify.py:18,73` (`BINARY`, paks), `network.py:146` (packaged cwd), `network_desktop.py:124` and `hud_capture --mode packaged`. | **One package path per checkout.** `doctor` detects package mutation after launch (`SKILL.md:181`). | **High.** Any repackage overwrites the artifact under test **and** the "protected" friend playtest build (`Docs/Balance.md:160`; `Builds/CoopRTS-playtest-tonight-Linux-Development.tar.gz`). Two concerns share one path. |
| 8 | `Saved/Verification/<run>` (332 dirs, 52 `RESULTS.md`, 3.3 G) | Every harness. `--run` must be fresh: `network.py:46` `mkdir(exist_ok=False)` and `launch`. Exception: `verify.py regression` uses `exist_ok=True` and refuses only an existing log (`verify.py:274-278`). | Naming convention (`<topic>-<letter>-<date>`), chosen by each agent. | Low–medium. Collisions are refused, but evidence is **untracked**: README cites 16 `Saved/Verification/...` paths and SKILL.md cites 16, and none of them exist in a clone or worktree. |
| 9 | Generators that rewrite maps and assets in place (`Build/Generate*.py`, `Build*`, `Import*`) | **Map generators**: `GenerateCommandMap` replaces Boot (`README.md:276`). `GenerateCampusZero`, `GenerateAvailabilityZone`, `GenerateAvailabilityZoneV2` and `BuildArtGallery` all `load_level` → `new_level` → `save_current_level` (`GenerateAvailabilityZone.py:156-164,518`; `GenerateAvailabilityZoneV2.py:105-111,332`). **Shared assets**: `BuildSharedMaterial.py:559` deletes and recreates `M_Shared`, which every mesh uses. `GenerateAvailabilityZone.py:116-142` rewrites 23 `MI_AZ_*` instances that **V2 also consumes** (`GenerateAvailabilityZoneV2.py:98-99`), a hidden cross-map coupling. `GenerateMatchContent.py` and the legacy `GenerateCombatUnits.py` **both** write `/Game/Units/DA_*`. Layout code is shared: `MatchLayout.py` is used by Boot and CampusZero. | None. No lock and no ownership. The only guard is prose: close the editor and run nothing else (`Docs/World.md:329`; `Docs/UnrealMCP.md:30`). | **High.** Two agents regenerating different maps can still collide on `MI_AZ_*`/`M_Shared`/`DA_*`. Outputs are LFS binaries. |
| 10 | Binary asset merges | 370 LFS files, 47 currently modified `Content/` binaries. Tuned combat values exist **only** inside `Content/Units/DA_*.uasset` (`README.md:155`: "Existing assets keep their tuned combat values"). | No `lockable`, no remote, no LFS locks, no merge driver. | **High.** Any two agents touching the same `.uasset/.umap` (including through a generator side effect) produce an unmergeable conflict; the last writer wins. |
| 11 | Ports | `network.py:51-53` and `network_desktop.py:103-107` choose a free loopback port (bind to 0). README direct IP uses a fixed **7777** (`README.md:220`); Steam hosting listens on 7777 (`features/multiplayer.md:52`). Unreal MCP uses a fixed **8000** (`Docs/UnrealMCP.md:9,32`). | Harness ports are isolated (short race between `close` and `Popen`). | Medium. `UnrealMCP.md:32` says generator runs skip MCP auto-start. **The logs contradict this**: `UnrealEditor-Cmd -ExecutePythonScript` runs log `LogModelContextProtocol: Starting MCP server on port 8000` (104 logs, e.g. `Saved/Verification/campus-zero-20260929/kitchk-v9-1.log`), because `-ExecutePythonScript` is a full editor, not a commandlet. Any two generators, or a generator plus an open editor, contend for 8000 `[INFERENCE: no bind failure was found in the logs]`. `-game` regression logs do not start MCP. |
| 12 | Steam | One Steam client and account per machine; App ID 480 (`Config/DefaultEngine.ini:42`) | `network.py`/`network_desktop.py` inject `-nosteam` (`network.py:131`, `network_desktop.py:116`). `verify.py launch` does not. | Medium. Steam runs need the real desktop client, and a same-account loopback is rejected (`features/multiplayer.md:54`). |
| 13 | Desktop, compositor and GPU | `verify.py` and `network_desktop.py` drive Hyprland (`hyprctl`, `wtype`, `grim`, a compiled `pointer.c`). `hud_capture.py` uses Vulkan `-RenderOffScreen`. | Focus guard; "Only one driver may control this desktop at a time" (`SKILL.md:187`). | **High** for native runs: one desktop means one native agent at a time. |
| 14 | Unreal MCP (live editor bridge) | `.omp/mcp.json` gated by `UNREAL_MCP`; `Config/DefaultEditorPerProjectUserSettings.ini:2` `bAutoStartServer=True` | "One agent only" (`Docs/UnrealMCP.md:29`). Advisory. | Medium. It is a second, interactive way to mutate the same assets that generators overwrite. |
| 15 | Working tree itself | Round-2 workers edited one shared tree at the same time: "Other workers edit other parts of this repository at the same time … re-read a file immediately before every edit" (`COMMON2.md:17`). | Prose-level file ownership in briefs. | **High.** There are no worktrees today. |

---

## 3. Entry points grouped by outcome

Legend: **C** = the doc calls it canonical (the AGENTS.md chain is README for build/package and SKILL.md for verification). **D** = duplicate or alternative path to the same outcome. "Doc" = where it is written down.

### 3.1 Engine setup
| Way | Doc | Status |
|---|---|---|
| `patch --forward -d "$UE_ROOT" -p1 -i Build/UnrealEngine-5.8.3-ShaderPrint.patch` | `README.md:134-138` | C (only way) |

### 3.2 Build the editor module (`libUnrealEditor-CoopRTS.so`)
| Way | Doc | Status |
|---|---|---|
| `$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh CoopRTSEditor Linux Development -Project=… -WaitMutex` | `README.md:146-149`; `SKILL.md:84` points to it | **C** |
| Same command wrapped in `flock /tmp/cooprts-work/ue.lock` | `/tmp/cooprts-work/COMMON2.md:18` (outside the repo) | D (brief-only variant) |
| BuildCookRun `-build`, which also compiles `CoopRTSEditor` | `README.md:160` (side effect, visible in `round2/development-package.txt`) | D (implicit) |
| Opening the editor on a stale module (rebuild prompt or Live Coding) | not documented | D `[INFERENCE]` |
| `GenerateProjectFiles` → `/Makefile`, `compile_commands.json` | only implied by `.gitignore:13-14` | D (residue) |

### 3.3 Package
| Way | Doc | Status |
|---|---|---|
| `RunUAT.sh BuildCookRun … -clientconfig=Development … -archivedirectory=$PWD/Builds` → `Builds/Linux` | `README.md:159-164` | **C** |
| Same with `-clientconfig=Shipping … -archivedirectory=$PWD/Builds/LinuxShipping` | `README.md:174-179` | C (separate artifact) |
| Map list `-map=/Game/Maps/Menu+Boot+CampusZero+AvailabilityZone+AvailabilityZoneV2` | Hard-coded **twice** in README (`:162`, `:177`) and **again** as `MapsToCook` (`Config/DefaultGame.ini:13-17`) | D (three copies of one list; adding a map means three edits) |
| Friend tarball `Builds/CoopRTS-playtest-tonight-Linux-Development.tar.gz` | only in `Saved/Verification/playtest-tonight/` (untracked) | D (ad hoc) |

### 3.4 Launch or run the game
| Way | Doc | Status |
|---|---|---|
| `$UE_ROOT/…/UnrealEditor CoopRTS.uproject` (editor) | `README.md:150`, `Docs/UnrealMCP.md:16` | C (duplicated in 2 docs) |
| `./Builds/Linux/CoopRTS.sh [map] -windowed …` | `README.md:165-167`, `Docs/Maps/AvailabilityZoneV2.md:44` | C |
| `./Builds/LinuxShipping/CoopRTS.sh -nosteam …` / `SteamAppId=480 …` | `README.md:181-183` | C (Shipping) |
| Direct binary `Builds/Linux/CoopRTS/Binaries/Linux/CoopRTS` with `LD_PRELOAD`/`SDL_VIDEODRIVER` | `features/multiplayer.md:66-69` | D |
| `verify.py --run R launch [--map]`, which runs the direct binary at 1600×900 under owned identity | `SKILL.md:163-171` | C for verification |
| `UnrealEditor … <map> -game -windowed` (editor-hosted game) | `Docs/World.md:359` (ArtGallery), `README.md:229-233` | D |

### 3.5 Multiplayer (direct IP, local)
| Way | Doc | Status |
|---|---|---|
| Packaged host/client on port 7777 with `-nosteam` | `README.md:218-223` | C for humans |
| Editor `-game` host/client | `README.md:227-234` | D |
| `network.py --mode editor\|packaged --clients 0\|1\|4 --scenario …` (headless) | `SKILL.md:126-146` | **C** for verification |
| `network_desktop.py launch --clients 1\|4 [--probe]` (native windows) | `features/multiplayer.md:100-108` | C for native input |
| Steam host/invite | `README.md:194-212`, `features/multiplayer.md:48-92` | C (manual; two docs) |

### 3.6 Automated tests (UE Automation)
| Way | Doc | Status |
|---|---|---|
| `verify.py --run R regression --scenario <rules\|construction\|production\|force-identity\|goal-orders\|strategy\|orders\|movement\|combat\|match-win\|match-loss\|doctrine-*> [--map]` | `SKILL.md:86-101`; mapping at `verify.py:246-262`; all 14 world tests plus 18 `CoopRTS.Rules.*` tests are covered | **C** |
| Raw `UnrealEditor … -game -nullrhi -ExecCmds="Automation RunTests <filter>; SoftQuit"` (what `regression` wraps) | `verify.py:280-282`; not documented as an alternative | D (trivially reachable) |
| Editor Session Frontend, or the MCP editor bridge | not documented | D `[INFERENCE]` |
| Ad-hoc probe scripts run via `-ExecutePythonScript` inside evidence dirs: **67 `.py`** under `Saved/Verification` (e.g. `campus-zero-20260929/nav_check.py`, `sc2-art-unreal/probe1..N.py`, 10 in `availability-zone/`) | not documented; created by agents | D (workaround pattern; see §6) |

### 3.7 Presentation and visual verification
| Way | Doc | Status |
|---|---|---|
| `hud_capture.py --run R --mode editor\|packaged [--quick LABEL] [--res WxH] [--map]` | `SKILL.md:21,75` | C |
| `verify.py … capture <label>` after `launch` (grim on the native window) | `SKILL.md:163-201` | C (native) |
| `network_desktop.py … capture --peer` | `features/multiplayer.md:106` | C (native multiplayer) |
| `verify.py doctor` (freshness and identity) | `SKILL.md:175-183` | C |

### 3.8 Balance simulation
| Way | Doc | Status |
|---|---|---|
| `uv run Build/SimulateMatches.py --map … --variant … --matches N --run Saved/Simulation/<x>` | `Docs/Balance.md:26-30,38-42` | **C** |
| `--executable <packaged>` instead of editor `-game` | `Docs/Balance.md:7` | D (two runtimes) |
| `--matrix`, `--compare-dilation`, `--report-only` | `Docs/Balance.md:32-42` | variants |
| Raw `flock … UnrealEditor … -autopilot -SimSeed=… -SimOutput=…` | `Docs/Balance.md:47-58` ("equivalent single-match runtime invocation") | **D, documented as equivalent** |

### 3.9 Generate content
All Unreal-side steps use `UnrealEditor-Cmd CoopRTS.uproject -EnablePlugins=PythonScriptPlugin -ExecutePythonScript=Build/<X>.py …`. **The flag sets differ between docs**:
- README: `-unattended -nullrhi -nosplash`.
- World.md: `$PY -nullrhi`, or `-RenderOffscreen` for materials, plus `-EnablePlugins=GeometryScripting` for masks.
- AZ-implementation: abbreviated as `UnrealEditor-Cmd ...`.
- AZV2: prefixed with `flock`.

| Output | Way(s) | Doc | Status |
|---|---|---|---|
| Boot map | `GenerateCommandMap.py` | `README.md:276-283` | C |
| CampusZero map | `GenerateCampusZero.py` | `Docs/World.md:362` | C |
| AvailabilityZone v1 | `GenerateAvailabilityZone.py` (also rewrites `MI_AZ_*`) | `Docs/Maps/AvailabilityZone-implementation.md:78-80` (abbreviated) | C |
| AvailabilityZone v2 | `GenerateAvailabilityZoneV2.py` under `flock` | `Docs/Maps/AvailabilityZoneV2.md:36-38` | C |
| Menu map | `GenerateMenuMap.py` | Only listed as a file in `features/README.md:20`; **no command documented** | undocumented |
| ArtGallery / UnitGallery | `BuildArtGallery.py`; `ImportUnitMeshes.py` (also builds UnitGallery) | `Docs/World.md:349-358` | C |
| Unit and building data assets | `GenerateMatchContent.py` (`MATCH_CONTENT_READY`) **and** `GenerateCombatUnits.py` (legacy; both write `/Game/Units/DA_*`) | `README.md:155` | **D: two generators for one asset set** |
| Master material | `BuildSharedMaterial.py` (Unreal) and `MasterMaterials.py` (Blender prototype) | `Docs/World.md:345`; `Art/Materials/UNREAL.md` | C, documented in 2 places |
| Overlay materials (cooked; `DirectoriesToAlwaysCook`) | `GenerateOverlayMaterials.py` | **undocumented anywhere** | undocumented |
| Meshes | `blender -b --factory-startup -P Build/Generate{Unit,Building}Meshes.py`, `GenerateEnvironmentKit.py`, `GenerateTerrainKit.py` → `Import*.py` → `VerifyMasks.py` | `Docs/World.md:336-355`; AZ-impl `:47,79` | C, split over 2 docs |
| Audio | `uv run Build/FetchAudioSources.py` → `uv run Build/GenerateUnitAudio.py` (REAPER) → Unreal `ImportAudio.py`; `uv run Build/GenerateMusicSample.py` | `Docs/Audio.md:153,236-242`; `Art/Audio/SOURCES.md` | C |
| Layout validation | `python3 Build/DrawMapLayout.py [--report]` (v1); `Build/DrawAvailabilityZoneV2.py` (v2); `python3 Build/AvailabilityZoneLayout.py --svg` (`AvailabilityZoneLayout.py:8`) | `Docs/Maps/AvailabilityZone.md:294-296`; `AvailabilityZoneV2.md:29` | C per map; 3 validators |
| Textures and UI | `FetchTextures.py`, `RenderTextureContactSheet.py` (**undocumented**), `RenderUIIcons.py`, `ExportUIAssets.py` | `Art/Textures/SOURCES.md`, `Art/UI/*.md` | mixed |
| Interactive editing through the live editor | Unreal MCP agent (`UNREAL_MCP=1 omp`) | `Docs/UnrealMCP.md` | D (second way to mutate the same assets) |

**Runtimes needed to run the entry points: 6.**
1. System `python3` (verify scripts, Draw*).
2. `uv run --script` (SimulateMatches, audio).
3. Blender `bpy`.
4. Unreal embedded Python.
5. REAPER.
6. `cc` + Wayland (`pointer.c`).

There is no single front door (no Makefile, justfile or task runner). 36 `.py` files in `Build/` plus 4 harness scripts.

---

## 4. Agent guidance: enforced vs advisory

### Guidance inventory
- **`AGENTS.md`** (19 lines, **untracked**): a routing table only.
  - Points to `Docs/Design.md`, `Docs/Design/status.md`, `Docs/Research/`, `Docs/Balance.md`, `Docs/Maps/`, `Docs/World.md`, README (build/package/Steam/network) and `SKILL.md` (verification).
  - Doc-editing rules: one fact per topic file, file links, status tags (`AGENTS.md:16-19`).
  - No build, test or lock rules.
- **Skills**: one project skill, `.agents/skills/verify-cooprts`.
  - `SKILL.md` (247 lines), 12 `features/*.md`, `scripts/{verify,network,network_desktop,hud_capture}.py` and `pointer.c`.
  - It enforces through its scripts: tiered proof (rules → world → network slice → presentation → acceptance; `SKILL.md:12-22`), fresh evidence dirs, editor and package freshness checks, explicit-Success parsing, owned-PID cleanup and the focus guard.
  - Global skills in `~/.agents/skills` (`code-review`, `diagnose-crash`, `omarchy`) are not project-specific.
- **Out-of-repo orchestration**:
  - `/tmp/cooprts-work/COMMON.md`, `COMMON2.md`, `CONTRACT-v2.md` and about 30 per-worker briefs and reports. These define the lock rule, "no git write commands", "lean testing", file ownership and `EHUDAction` ordinal stability.
  - `Saved/AgentBriefs/*.md` (38 briefs, untracked) hold the lane/worktree briefs.
  - None of this is versioned, and `/tmp` is volatile.
- **Hooks, CI, lint**: LFS hooks only; no CI.
  - No `.clang-format`, `.editorconfig`, `pyproject.toml`, ruff/black/flake8 config or Python tests, even though `clang-format`, `uv` and `blender` are installed.
- **MCP**: `.omp/mcp.json` gates the Unreal MCP behind `UNREAL_MCP`.

### Enforced mechanically vs advisory

| Rule | Mechanism | Enforced? |
|---|---|---|
| Binary assets go to LFS | `.gitattributes` + LFS hooks | **Yes** (pre-push requires git-lfs) |
| Build outputs and evidence untracked | `.gitignore` | **Yes** |
| Fresh evidence dir per run | `network.py:46`, `hud_capture`/`network_desktop` `exist_ok=False`, `verify.py launch`. `regression` refuses only an existing log (`verify.py:274-278`). | **Yes** (partial for `regression`) |
| Editor module newer than `Source/*.{h,cpp,cs}` and `.uproject` | `verify.py editor_stamp` (`:86-100`); `SimulateMatches.py:165-171` | **Yes**, mtime heuristic only; `Build/`, config, plugins and engine are not checked (`SKILL.md:84`) |
| Module or package not mutated during a run | `verify.py:286-287`; `network.py` artifact stamp; `doctor` | **Yes** (detect and fail, not prevent) |
| PASS needs explicit `Result={Success}`, exit 0, SoftQuit marker, requested map started | `verify.py:289-301` | **Yes** |
| Native input only into the owned, focused window | `verify.py doctor(focused=True)`; pidfd | **Yes** |
| Loopback ports don't collide; `-nosteam` on probes | `network.py:51-53,131` | **Yes** |
| Test, probe and fixture code absent from Shipping | `#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING` (`CoopRTS.cpp:3`, `ArmyNetworkVerification.cpp:3`, `CommandGameState.h:81`); `#if !UE_BUILD_SHIPPING` (`CoopSessionSubsystem.cpp:16`) | **Yes** for those, **no** for autopilot (§6) |
| Simulation overrides only in opted-in standalone worlds; numeric bounds | `SimulationSettings.cpp:33-53` | **Yes** |
| Only one Unreal process at a time | `flock` in `SimulateMatches.py` only | **Advisory** for everything else |
| No git writes by agents; don't revert the owner's changes | Briefs (`COMMON2.md:16`) | **Advisory** |
| Edit only assigned files | Briefs | **Advisory** |
| Don't run generators while the editor has the asset open | `Docs/UnrealMCP.md:30`, `Docs/World.md:329` | **Advisory** |
| One MCP agent | `Docs/UnrealMCP.md:29` | **Advisory** (env gate only) |
| One desktop driver | `SKILL.md:187` | **Advisory** (focus guard only prevents mistyping) |
| Lean verification: one scenario per change | `SKILL.md:32-42`; `COMMON2.md:9-13` | **Advisory** |
| Don't package while a game uses the package; don't overwrite protected playtest builds | `SKILL.md:160`; `Docs/Balance.md:160` | **Advisory** |
| `EHUDAction` ordinals append-only | `COMMON2.md:20` | **Advisory** |
| One fact per doc; status tags | `AGENTS.md:16-19` | **Advisory** |
| Code style, format, lint | none | **Not even advisory**, apart from "follow existing style" in briefs |
| Replicated catalogue order (units frontline/ranged/siege; buildings barracks/extractor/workshop) | `README.md:155` prose | **Advisory** |

---

## 5. Docs sprawl

### Size
- 43 Markdown files outside `Saved/` (706 KB), plus 13 skill files.
- Procedure-bearing docs: `README.md` (52.5 KB, 285 lines; 7 bash blocks, 11 UE invocations), `SKILL.md` (8 blocks), `features/multiplayer.md` (5), `Docs/World.md` (3; art pipeline `:327-383`), `Docs/Balance.md` (3), `Docs/Maps/AvailabilityZoneV2.md` (2), `Docs/UnrealMCP.md` (2), `Docs/Maps/AvailabilityZone-implementation.md` (inline), `Docs/Maps/AvailabilityZone.md` (1), `Docs/Audio.md` (inline `uv run`), `Art/Materials/UNREAL.md`, `Art/UI/IMPLEMENTATION.md`, `Art/Audio/SOURCES.md`, `Art/Textures/SOURCES.md`.
- About **14 docs** describe procedures.
- Plus the out-of-repo briefs in `/tmp/cooprts-work/*.md` (26) and `Saved/AgentBriefs/*.md` (38).

### README content mix
- About 60% history and evidence narrative: `:107-113` "Historical milestone evidence", `:246-266` "Earlier verification status", and Steam evidence `:194-212`.
- It also contains gameplay rules (`:9-87`) that `Docs/Design/status.md` also maps.

### Duplicated procedure text
1. **Steam evidence and acceptance:** `README.md:194-212` ≈ `features/multiplayer.md:48-92` ≈ `SKILL.md:113`.
2. **Network verification summary:** `README.md:238-242` ≈ `SKILL.md:120-154` ≈ `features/multiplayer.md`.
3. **Evidence "current boundary" lists:** `README.md:268-270` ≈ `SKILL.md:103-113` (both enumerate `restructure-20260930`, `map-integration`, `solo-readiness`, `round2`, `steam` RESULTS).
4. **Cook map list:** `README.md:162` and `:177`, and `Config/DefaultGame.ini:13-17`.
5. **UE path and engine launch:** `README.md:147-150` and `Docs/UnrealMCP.md:15-16`. The engine path default is also duplicated in scripts: `verify.py:279`, `network.py:17`, `SimulateMatches.py:25`, and SimulateMatches has **no `UE_ROOT` override**.
6. **Economy defaults:** `SimulationSettings.h:12-16` and `SimulateMatches.py:27` (`DEFAULT_ECONOMY`) and `Docs/Balance.md:36`.
7. **Generator invocation flags:** README `:279-282` vs World.md `:333-334` vs AZV2 `:36-38` (different flag sets and lock usage for the same pattern).

### Contradictions
- **Lock rule.** `COMMON2.md:18` says "ALWAYS wrap … verify.py, network.py, hud_capture.py" in `flock`. README, `SKILL.md` and `World.md` never mention it. `Balance.md:32` says wrapping `SimulateMatches` in `flock` deadlocks.
- **MCP.** `Docs/UnrealMCP.md:32` says generator scripts skip MCP auto-start. Generator logs show `Starting MCP server on port 8000` (§2 row 11).
- **Default map.** The packaged default is `/Game/Maps/Menu` (`Config/DefaultEngine.ini:3`). The editor startup map and all harness defaults are `/Game/Maps/Boot` (`DefaultEngine.ini:2`; `verify.py:20`). Gameplay maps used for playtests are AvailabilityZoneV2 (`README.md:188`). Documented, but three different "default" maps.

### Stale instructions
- **Skill vs current model.** Skill text still uses the pre-extractor/pre-goal model: "outpost income", "+12/s per friendly wallet", "Secure front", "sector" (`SKILL.md:58,65,68,73,136`; `features/construction.md:3,23`; `features/economy.md:9`; `features/match.md:10`; `features/multiplayer.md:40`). README now describes Extractors and Hold/Expand/Assault/Fall Back goals (`README.md:24,47`). README itself admits the multiplayer recipe "predates the Extractor cutover" (`README.md:240`).
- **AvailabilityZone.md.** `Docs/Maps/AvailabilityZone.md:289` says "Maps are hard-coded in the harness"; `--map` now exists on every harness (`SKILL.md:24-30`).
- **Legacy generator.** `GenerateCombatUnits.py` is labelled "older" (`README.md:155`) but is still shipped and still writes the same assets.
- **Map art remnants.** `Docs/World.md:314` flags shield-objective remnants in `GenerateCampusZero.py` as unchecked.
- **Dangling evidence citations.** README and SKILL.md cite many `Saved/Verification/...` evidence paths, and those paths are untracked.

---

## 6. Workarounds and test-only paths already present

### Test/fixture APIs reachable from gameplay code
1. **`ServerIssueOrder` / `ServerIssueAttack`**
   - `CommandPlayerController.h:63-66`; implementations at `CommandPlayerController.cpp:812-863`.
   - Reliable Server RPCs compiled into **all** configs, Shipping included.
   - No human input calls them: callers are only `Army*Tests.cpp` (e.g. `ArmyOrderTests.cpp:55-97`) and the network probe (`ArmyNetworkVerification.cpp:447,452`).
   - Any client can still invoke them on its own army.
   - The player-facing feedback strings ("Manual order accepted; automatic front disabled", `:867`) remain. `features/multiplayer.md:96` calls them "fixture/API proof, not current human controls".
2. **`bVerificationIncomePaused`**
   - `CommandGameState.h:81-84`; checked in the production `Tick` at `CommandGameState.cpp:128-129`.
   - Set by 6 test files and the probe (`ArmyNetworkVerification.cpp:497-505`).
3. **Network probe (`-CoopRTSNetVerifyDir=`, `-CoopRTSNetVerifyPeer=`, `-CoopRTSNetVerifyAuthority`)**
   - `ArmyNetworkVerification.cpp:717-724`: a file-based RPC loop (`reply.json`).
   - Authority fixtures (`:458-662`, listen-server only): `isolate`, `placement`, `income`, `incomeTick`, `depositRemaining`, `enemyExtractor`, `destroyExtractor`, `fund`, `capture`, `occupant`, `assaultSetup`, `kill`, and **`finish`**.
   - `finish` sets HQ `Health=1`, teleports a shooter next to it, fires, and teleports it back (`:662-679`). This is the "fixture-shortened victory".
   - Present in Development packaged builds (guard `WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING`).
4. **`CoopSteam.Verify checksum|invite-refusal|reject` console command**
   - `CoopSessionSubsystem.cpp:16-63` (non-Shipping).
   - Injects a fake checksum failure, a spawned fake PlayerState, and a loopback `127.0.0.1:1` join with a forced `ConnectionError` (`features/multiplayer.md:84-92`).
5. **`ArmyTestSetup.h`**: spawns armies or a completed workshop as fixtures (`:31,80,100-113`; guarded by `WITH_DEV_AUTOMATION_TESTS`).

### Environment and command-line flags
6. **Autopilot flags: `-autopilot -SimSeed= -SimBaseline= -SimNormalRate= -SimRichRate= -SimNormalAmount= -SimRichAmount= -SimTimeCap= -SimDilation= -SimOutput=`**
   - Parsed in `SimulationSettings.cpp:28-48`.
   - `UMatchSimulationSubsystem` is created when `-autopilot` is set (`MatchSimulationSubsystem.cpp:49-54`). It sets world time dilation (`:155-160`) and spawns a team-0 `AEnemyCommander` (`EnemyCommander.h:19`).
   - **No build-config guard**: compiled into Shipping.
   - Gameplay economy reads its constants through `FSimulationSettings::ForWorld` in every standalone world (`CommandGameState.cpp:164`, `DepositSite.cpp:20,27`). The *production* defaults for baseline income and deposit rates and amounts therefore live in the simulation struct (`SimulationSettings.h:12-16`).
7. **`-nosteam`**: injected by `network.py`/`network_desktop.py`; required for direct IP (`README.md:225`).
8. **Packet emulation:** `-PktLag=120 -PktLoss=8` (`network.py:141-142`, `--emulation`).
9. **Rendering and timing:** `-RenderOffScreen`/`--offscreen` HUD rendering, and `--max-fps 60` / `t.MaxFPS 0` (`network.py:132-143`; `Docs/Balance.md:58`).
10. **`-abslog=`**: per-run logs, used to work around the shared `Saved/Logs`.
11. **Steam overlay shell configuration:** `SteamAppId=480 SteamGameId=480 LD_PRELOAD=…gameoverlayrenderer.so SDL_VIDEODRIVER=x11` (`features/multiplayer.md:66-69`).
12. **`UNREAL_MCP=1`**: gates the MCP connection (`.omp/mcp.json`).

### Hard-coded paths and workstation assumptions
13. Engine path `~/.local/opt/unreal-engine/5.8.3`: `verify.py:279`, `network.py:17`, `SimulateMatches.py:25`.
14. Lock `/tmp/cooprts-work/ue.lock`: `SimulateMatches.py:499` and docs.
15. Package `Builds/Linux/CoopRTS/Binaries/Linux/CoopRTS`: `verify.py:18`.
16. Other fixed paths: `/tmp/texsheet` (`RenderTextureContactSheet.py:16`) and the Steam overlay `.so` path.
17. Hyprland-only input stack (`hyprctl`, `wtype`, `grim -c`, wlr virtual-pointer; `SKILL.md:158`).
18. A user-local engine shader patch (`README.md:126-140`).

### Checks that are relaxed, skipped or heuristic
19. **Freshness = mtime.** Only `Source/*.{h,cpp,cs}` vs the module, and `Config/*.ini`/`Content/*.{uasset,umap}` vs the paks. `Build/`, plugin, engine and generated-code changes are not detected (`SKILL.md:84,181`).
20. **`regression` accepts an existing `--run` directory** (`verify.py:274`); only the log name must be new.
21. **`verify.py launch` has a 5 s deadline** (`verify.py:162`), while the skill forbids deadlines elsewhere (`SKILL.md:44-46`).
22. **Lean policy.** "Per change: build, then run only the ONE scenario … no packaging, no native desktop runs" (`COMMON2.md:9-13`). The full matrix is deferred to an integration chunk. Balance verification was reduced to V2 baseline 2/3 only (`Docs/Balance.md:24`).
23. **Shipping checks.** These used "removed disposable guarded adapters" because the helpers target Development only (`README.md:186`).
24. **Network acceptance** is one restart cycle; the three-cycle forced-delay check was not rerun (`SKILL.md:152`).
25. **Probe and fixture runs** fund, isolate and pause income explicitly ("not a naturally paced match", `README.md:240`; `SKILL.md:124`).
26. **Throwaway editor probes.** 67 ad-hoc Python/shell scripts sit inside `Saved/Verification/*` (29 in `sc2-art-unreal`, 22 in `campus-zero-20260929`, 10 in `availability-zone`). These are one-off `-ExecutePythonScript` probes (navigation checks, map probes, shots, kit checks) written by agents because no reusable probe or entry point exists. They are untracked and unreviewed.
27. **Duplicate legacy generator:** `GenerateCombatUnits.py` (see §3.9).

---

## Measurements from existing logs
- **UBT** (37 recorded runs in `Saved/Verification`): min 0.98 s, median 9.77 s, max 20.22 s.
  - The latest UBA log compiled all 35 module actions in 9.83 s (adaptive non-unity, 8 processes).
  - A cold build was not measured.
- **UAT BuildCookRun** (17 recorded): median 28 s, max 52 s (incremental).
  - Development package 26 s; Shipping 33 s (`round2/*-package.txt`).
- **Tests and slices** (`Saved/Verification/restructure-20260930/RESULTS.md`):
  - Rules tier: 4 tests in 9.6 s.
  - World scenarios: Lifecycle 9.7 s, VictoryRestart 10.6 s, DefeatRestart ~13 s, Production 56 s, ConstructionEconomy 73 s.
  - Network slices: ownership 27 s, restart 70 s, production 103 s, economy 122 s.
- **Source size:** 14,521 lines in `Source/CoopRTS`; `CommandHUD.cpp` is the largest at 1,575 lines.
