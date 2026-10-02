# Research: Protecting the win condition, off-screen awareness, and pace: lessons for Post-Frontier

Scope: (1) how games stop a single unattended push from ending a match in minutes, (2) how they tell a player about events they are not looking at, including the player's own successes, and (3) which levers make an RTS feel less rushed. Each entry follows **Mechanic → why it works (evidence) → applicability → pitfall**. `[INFERENCE]` marks my own reasoning. Sources are official sites, wikis, developer interviews and press. Where only community forums were available, the entry says so.

---

## 0. The playtest in numbers `[INFERENCE]`

| Quantity | Now | At design HP (1800) |
|---|---|---|
| Lattice vs 1 Frontline force (120 DPS) | 7.5 s | 15 s |
| Lattice vs 1 Ranged force (63 DPS) | 14.3 s | 28.6 s |
| Lattice vs 5 Frontline forces (5-player co-op, 600 DPS) | 1.5 s | 3 s |
| Nearest JEV force walking HQ-to-HQ (211 m at 4.2 m/s) | ≤ 50 s | ≤ 50 s |

**Doubling HP does not fix the playtest.** Even at 1800 HP a lone force kills the Lattice well before a JEV force walking back can arrive. It also kills it before a player who is busy in his base would notice. The kill window has to be longer than *notice time + decision time + JEV response time*, which is roughly 60–90 s. Reaching that with HP alone would take 7,200–10,800 HP against one force, and the same HP would be trivial against five. The games below solve this with **structure** (gates, phases, confirmation holds), not with bigger numbers.

---

## Topic 1: Protecting the win condition from early or unattended wins

### 1.1 Dota 2: tiered invulnerability, Backdoor Protection, Glyph
**Mechanic.** Every tower is invulnerable until the lower-tier tower before it in its lane is destroyed. The Ancient is invulnerable until **both** Tier-4 towers fall ([Dota 2 Wiki: Buildings](https://dota2.fandom.com/wiki/Buildings)). **Backdoor Protection** applies 50% damage reduction and heals 180 HP/s of enemy damage while no enemy *lane creep* is nearby. The Ancient's 4,000-unit radius covers the whole base, and protection comes back 15 s after the creeps leave. A team can still force through it with more than 270 DPS before reductions (same page). **Glyph** makes every allied building take no damage for 5 s and has a 5-minute team-shared cooldown ([Liquipedia Glossary](https://liquipedia.net/dota2/Glossary)).
**Why it works.** Damage only "counts" when it comes with the game's own pushing wave, so a lone hero sneaking in cannot end the game. The rule is legible: the base glows while it is protected (wiki effect image). Glyph gives the defender one emergency button whose timing matters.
**Applicability.** This is the closest analogue to a lone force reaching the Lattice. A "Lattice Field" could stay on unless the attack is *supported* in some way: an outer structure is down, two or more forces are present, or a relay is already destroyed. The Glyph maps onto a JEV-side "emergency firewall" that buys response time. JEV fires it automatically, so it costs the players no micro.
**Pitfall.** Dota's conditions (lane-creep proximity, heal-on-damage, the 270 DPS break point) are notoriously hard to read without a wiki. Players who don't know the rule think the game is bugged. Post-Frontier would need the field to be visible and explained in one line.

### 1.2 League of Legends: Nexus gated by inhibitors, early Fortification, plating
**Mechanic.** The Nexus "is invulnerable so long as all three allied inhibitors or at least one of its guarding turrets are standing". Each inhibitor is invulnerable until its turret dies. The Nexus has 5,500 HP and regenerates 20 HP/s ([LoL Wiki: Nexus](https://wiki.leagueoflegends.com/en-us/Nexus); [Inhibitor](https://wiki.leagueoflegends.com/en-us/Inhibitor)). For the first 5 minutes, top and mid outer turrets take 85% less damage ("Fortification"). Turret plates add bonus resistances until 14:00 ([LoL Fandom: Turret](https://leagueoflegends.fandom.com/wiki/Turret)). Riot's stated goal is to "prevent turrets from falling quickly after one gank or mistimed recall" ([ComicBook, quoting Riot](https://comicbook.com/gaming/news/league-of-legends-turret-barrier-stats/)).
**Why it works.** The win condition sits behind a *chain* of objectives, and every link announces itself when it breaks (§2.3). A **time-gated** damage reduction protects the opening phase specifically, which is exactly the phase the playtest exploited.
**Applicability.** Assault could require "1 of N Lattice pylons down → Lattice vulnerable". Separately, a fortification window could cover the first ~6 min of each battle. That window sits naturally next to JEV's v1→v3 versions.
**Pitfall.** Chained gates make Assault look a lot like Raid. Hard time gates feel arbitrary unless the HUD shows them (LoL shows the plates and their timer on the structure).

### 1.3 Deadlock: Patron behind shrines, phase-2 invulnerability, pit-only damage
**Mechanic.** The Patron is untouchable until both Shrines fall. Shrines are themselves invulnerable until a pair of Base Guardians is destroyed ([SteamDB Deadlock objectives](https://steamdb.com/en/deadlock/mechanics/objectives); [05-22-2026 patch notes](https://patchbot.io/games/deadlock/articles/299-05-22-2026-update)). At half health the Patron becomes briefly invulnerable and transforms into a "Weakened Patron" that pulses damage. It is "immune to all incoming damage from outside the pit" ([Deadlock Wiki: Patron](https://translate.google.com/translate?client=srp&hl=tr&sl=en&tl=tr&u=https%3A%2F%2Fdeadlock.wiki%2FPatron)). Structures also get Backdoor Protection when no enemy troopers are nearby ([Deadlock Wiki](https://deadlock.wiki/Backdoor_Protection)).
**Why it works.** The phase break is a **built-in pause**: both teams hear it, reposition, and get a final fight. That is the "boss core with shield phases" pattern from PvE.
**Applicability.** A Lattice with two phases fits well, for example: phase 1 → invulnerable for 8–10 s at 50% while JEV spawns a "final protocol" garrison in place → phase 2. The pause is the alert window.
**Pitfall.** A post-phase invulnerability that lasts long enough to matter feels like a cheat unless it is telegraphed. Keep it short and make the visual and audio unmistakable.

### 1.4 Company of Heroes: victory points (ticket bleed) instead of an HQ kill
**Mechanic.** Sides start at 250/500/1000 points. Whoever owns more VP sectors drains the other side to 0 ([Wikipedia: Company of Heroes](https://en.wikipedia.org/wiki/Company_of_Heroes_(video_game)); [Steam guide: VP mechanics](https://steamcommunity.com/sharedfiles/filedetails/?id=2342774804)). Relic's patch notes for a 3-VP mode state the rule as "players must hold over half the victory points to avoid ticket bleed" ([CoH2 changelog](https://steamcommunity.com/sharedfiles/filedetails/changelog/3262414280)).
**Why it works.** No single action ends the match. Winning requires *sustained* advantage, and the bar is always on screen, so a swing is visible minutes before the end.
**Applicability.** Sabotage (hold 3 regions for 90 s) already works like this. The lesson for Assault is to turn "HQ dies" into "HQ dies **and** the hold is sustained" (§1.8).
**Pitfall.** Pure bleed modes can end in a slow, foregone grind. CoH keeps "Annihilate" for players who want a decisive kill.

### 1.5 Supreme Commander: Assassination vs Supremacy/Annihilation
**Mechanic.** Assassination ends the game on Commander (ACU) death and is used in ~95% of online matches. Annihilation requires destroying everything and is described as "best used never" because a losing player can drag it out ([SupCom Wiki: Annihilation](https://supcom.fandom.com/wiki/Annihilation); [Assassination](https://supcom.fandom.com/wiki/Assassination)).
**Why it works.** A single-point kill gives decisive, short games. A distributed kill gives long ones. The trade-off is explicit and the player chooses it.
**Applicability.** The Lattice is an assassination target. If it stays one, it needs bodyguards and gates. Raid (3 relays) is effectively a small Supremacy mode.
**Pitfall.** Annihilation-style "clean up everything" endings are tedious. Don't fix the rush by requiring the whole base to be razed.

### 1.6 Age of Empires II/IV: Wonder/Relic timers, Town Bell, landmarks
**Mechanic.** AoE2 Wonder and Relic victories need the condition held for a map-size-dependent time ([AoE Wiki: Victory](https://ageofempires.fandom.com/wiki/Victory)). In AoE4, when a Wonder starts "all enemies are warned", it gets a minimap marker, and it must stand for 15 min. Sacred Sites start a 10-min countdown that **pauses** while contested and **resets** if a site is neutralised (same page). Landmark victory requires *all* landmarks destroyed (same). In AoE2 the Town Bell sends every villager into the nearest building ([Town Bell](https://ageofempires.fandom.com/wiki/Town_Bell)), and each garrisoned villager adds arrows to a Town Center ([Villager](https://ageofempires.fandom.com/wiki/Villager_(Age_of_Empires_II))).
**Why it works.** Alternative wins are *announced, timed and contestable*. The Town Bell is a one-click emergency garrison that turns the HQ into a defender.
**Applicability.** Two templates. (a) A "Lattice Collapse" countdown after its HP hits zero, which pauses when JEV units contest the region (Sacred Site). (b) JEV's own Town Bell: Lattice crews, drones or turrets that wake only when the Lattice is threatened.
**Pitfall.** Long timers (10–15 min) are a whole game in a 10-minute battle. Scale the hold to 60–90 s.

### 1.7 Warcraft III: Call to Arms (emergency militia)
**Mechanic.** The Town Hall's Call to Arms turns nearby Peasants (2,000 range) into Militia for 42.5 s ([Liquipedia Warcraft: Call to Arms](https://liquipedia.net/warcraft/Call_to_Arms)).
**Why it works.** A temporary garrison appears *at the HQ*, so defence doesn't depend on armies walking home. The duration caps it so the defender can't exploit it.
**Applicability.** This answers "JEV defends with its single nearest force, which has to walk back". The response should *materialise at the Lattice*, either as timed emergency defenders or as turret activation, and not depend on travel.
**Pitfall.** If the emergency garrison is strong enough to beat a full force alone, Assault becomes "wait for the bell to expire". Tune it to *delay*, not to *defeat*.

### 1.8 They Are Billions, Thronefall, SC2 co-op, Risk of Rain 2, PvZ: PvE cores and final holds
- **They Are Billions:** losing the Command Center (5,000 HP plus 2,500 "Defense Life") loses the game ([TAB Wiki](https://they-are-billions.fandom.com/wiki/Command_Center); [stats](https://theyarebillionsguide.com/command-center/)). The final swarm comes from all directions at ~90% of the chosen duration and is announced with a banner ([TAB Wiki: Swarms](https://they-are-billions.fandom.com/wiki/Swarms); [Steam discussion](https://steamcommunity.com/app/644930/discussions/0/1620600279672387397/)).
- **Thronefall:** build by day, defend by night, lose if the Castle Center falls ([Thronefall wiki](https://game.wiki/thronefall/getting-started)).
- **SC2 co-op:** missions use timed holds ("Defend the Temple 26:00") and fail meters ("Lock & Load" fails when the overload ticker hits 9,000) ([Liquipedia: Temple of the Past](https://liquipedia.net/starcraft2/Temple_of_the_Past); [Lock & Load](https://liquipedia.net/starcraft2/Lock_&_Load)). In Malwarfare, progress **pauses** while Suppression Towers hit the objective, and Aurana says so aloud ([Blizzard: Malwarfare preview](https://news.blizzard.com/en-us/starcraft2/20988567/patch-3-17-preview-new-co-op-mission-malwarfare); [quotes](https://starcraft.fandom.com/wiki/StarCraft_II_Co-op_Missions_quotations/Malwarfare)).
- **Risk of Rain 2:** the teleporter needs at least 90 s of charging, the rate scales with the *fraction of living players inside the dome*, and the boss must also die ([RoR2 Wiki: Teleporter](https://riskofrain2.fandom.com/wiki/Teleporter)).
- **Plants vs. Zombies:** each lane has one lawn mower, a "last line of defense" used once per level ([PvZ Wiki](https://plantsvszombies.wiki.gg/wiki/Lawn_Mower)).

**Why they work.** The climax is a **phase that players must survive or hold**, not a single hit. A countdown with a visible pause or reset rule creates drama and gives teammates time to converge. RoR2's scaling *rewards* co-op presence.
**Applicability.** "Lattice breach → 60–90 s uplink hold in the Lattice region, which pauses while JEV contests it, with JEV's final protocol wave": this turns the rush into the battle's climax instead of a surprise. A once-per-battle JEV "lawn mower" (a pulse that wipes or repels the first attackers) is another cheap delay.
**Pitfall.** Holds that need *all* players inside (RoR2's rate rule) would break "strategy, not micro". Count forces, not players, and let any one force hold. Don't let the hold be a dead wait: put the final wave in it.

---

## Topic 2: Off-screen awareness without micro

### 2.1 StarCraft II: event jump, home key, adjutant lines
**Mechanic.** **Space** jumps to the last event notification and, pressed again, cycles through recent events. **Backspace** cycles through town halls ([Liquipedia: Hotkeys](https://liquipedia.net/starcraft2/Hotkeys)). The announcer also voices *own* completions ("Research complete", "Upgrade complete") ([TL.net announcer list](https://tl.net/forum/starcraft-2/515234-guide-listen-to-all-announcer-quotes)).
**Why it works.** Alerts form a *queue you can step through*, so one key replaces hunting the minimap. Positive confirmations close the loop on orders the player gave and then forgot about.
**Applicability.** A "jump to last event" key that cycles a queue, plus voiced *own-success* lines ("Force Bravo is attacking the Lattice").
**Pitfall.** Jumping the camera is still attention micro. In Post-Frontier the default should be that you *don't need* to jump: the HUD carries the state (§2.6).

### 2.2 Age of Empires II and Company of Heroes: last-notification, idle and flare keys, event cues
**Mechanic.** In AoE2, **Home** goes to the last notification, **H** selects and centres the Town Center, "." finds idle villagers, and **Alt+F** sends a flare ([AgeofEmpires.com: Match goals & hotkeys](https://www.ageofempires.com/learn-to-play/match-goals-aoe2/); [hotkey list](https://diamondlobby.com/age-of-empires-2/hotkey-guide-aoe2/)). Company of Heroes: Tales of Valor binds "Cycle through event cues" to Space and "Ping" to Ctrl+A ([official manual PDF](https://segaretro.org/images/2/20/CoHToV_Steam_manual.pdf)).
**Why it works.** Notifications are stored *objects* (cues) with a location, not just sounds. Flares and pings are the co-op channel for "look here".
**Applicability.** Event cues should be first-class objects: icon, region, age, jump target. In co-op, a cue raised for one player's force should be visible to all players as a team cue.
**Pitfall.** Idle-unit keys reward micro. Post-Frontier forces should never be "idle" in a way that needs finding.

### 2.3 Dota 2 and League: the global announcer covers both sides' structures
**Mechanic.** Valve's announcer script template lists "Your ancient is under attack!", per-lane tower and barracks attack and fall lines, and "You now have megacreeps". It glosses the last as a sign "the match will almost certainly end soon" ([Valve announcer handbook](https://cdn.steamstatic.com/apps/dota2/workshop/announcer/announcer_script_instructional_template.doc); [Dota 2 Workshop: Announcers](https://www.dota2.com/workshop/requirements/announcers)). LoL voices both "Your turret has been destroyed" **and** "Your team has destroyed a turret" ([LoL Wiki: Announcer](https://wiki.leagueoflegends.com/en-us/Announcer)).
**Why it works.** Objective events are **non-spatial, global audio**, so they reach every player wherever their camera is. Own-team successes are voiced as loudly as threats, and escalating milestones signal "the end is near".
**Applicability.** This is the direct fix for the spatial Lattice alarm. Lattice events should play as 2D UI audio to all players, tiered: *pylon down → Lattice exposed → Lattice under attack by your force → Lattice critical → victory*.
**Pitfall.** If every tower hit is announced, lines get ignored. Dota throttles "under attack" in practice, though its rule is not documented in these sources `[INFERENCE]`.

### 2.4 Paradox, Total War, HoI4: per-category filters and pause-on-event
**Mechanic.** EU4's Message Settings let each message type pop up, pause the game, or go to the log, in any combination ([Steam thread](https://steamcommunity.com/app/236850/discussions/0/617328415057171883/); [Paradox forum](https://forum.paradoxplaza.com/forum/threads/message-popup-filter.994305/)). Total War campaign notifications have a per-type filter cog. In Warhammer III, "skip" dismisses every notification of a type at once ([CA forum](https://community.creative-assembly.com/total-war/total-war-warhammer/forums/8-general-discussion/threads/7338-notification-system)). Total War battles offer pause, slow motion and 2–3× speed, with slow motion disabled on Legendary ([Steam thread](https://steamcommunity.com/app/364360/discussions/0/133256689833605688/); [Legendary mod note](https://steamcommunity.com/sharedfiles/filedetails/?id=2789864892)). HoI4 players have complained of missing an invasion because nothing paused or pointed at it ([Steam thread, 2016, community](https://steamcommunity.com/app/394360/discussions/0/357288572123255953)).
**Why it works.** Each player tunes the signal-to-noise. Pause-on-event lets a solo player treat critical events as "turns".
**Applicability.** For solo only, add an optional "pause or slow on critical objective events" toggle. Co-op cannot pause unilaterally, so it gets cues and announcer lines.
**Pitfall.** A full matrix of options is a power-user tool. Ship 3–4 sensible tiers, not 60 checkboxes.

### 2.5 Supreme Commander strategic zoom; Northgard and Frostpunk forecasts; They Are Billions pause
**Mechanic.** Chris Taylor called Strategic Zoom, which goes from the full theatre down to the cursor, the "key design element" that "allowed for more actual strategy" ([MCV interview](https://mcvuk.com/development-news/qa-chris-taylor-talks-strategy/amp/)). Northgard marks disasters on the calendar about 3 months ahead ([Northgard Wiki: Disasters](https://northgard.fandom.com/wiki/Disasters)). They Are Billions lets you issue orders while paused "to encourage strategic thinking over the player's control skills" (director Jesus Arribas, [GamesBeat](https://gamesbeat.com/the-making-of-early-access-hit-they-are-billions/)). Frostpunk's harder Survivor mode removes pause outside menus, which treats pause as a difficulty lever ([PC Gamer via Steam News](https://store.steampowered.com/news/posts?appids=323190&enddate=1532705714&feed=pcgamer)).
**Why it works.** Awareness comes from *overview plus forecast*, not reaction speed.
**Applicability.** JEV's version releases and waves already have a schedule, and the HUD should forecast them. Region-level orders suit an always-available map-overview layer.
**Pitfall.** Forecasting every JEV action removes tension. Forecast the *schedule*, not the targets.

### 2.6 Persistent objective progress (synthesis)
**Evidence.** AoE4 shows Wonder and Sacred countdowns to everyone along with minimap markers (§1.6). CoH's VP bar is always visible (§1.4). LoL renders plates and timers on the structure itself (§1.2). The SC2 co-op objective panel lists counts and timers (Liquipedia mission pages, §1.8).
**Applicability `[INFERENCE]`.** A persistent objective strip per battle type. **Assault:** Lattice phase/HP, which forces are engaging, hold timer. **Raid:** relays n/3 with attacker icons. **Sabotage:** regions held n/3 with a 90 s ring. It never hides, so the designer's playtest becomes impossible: he would have seen "Bravo engaging Lattice 72%".

---

## Topic 3: Pace, when an RTS feels "too fast"

### 3.1 Global game speed (SC2, AoE2)
**Mechanic.** SC2's "Faster" runs at 1.4× Normal and is the default for ladder and custom games ([Liquipedia: Game Speed](https://liquipedia.net/starcraft2/Game_Speed)). AoE2 DE offers Slow 1.0, Casual 1.5, Normal 1.7 and Fast 2.0. DE made 1.7 "Normal" because high-level players were used to it, and renamed the old single-player 1.5 to "Casual" (community posts: [AoE forum](https://forums.ageofempires.com/t/whats-the-equivalent-to-1-5-game-speed-in-aoe2-de/84298); [Steam](https://steamcommunity.com/app/813780/discussions/0/3821912677678091112)). Some campaign scenarios lock the speed (same Steam source).
**Why it works.** One multiplier slows everything consistently: economy, travel and combat.
**Applicability.** A host-chosen speed preset with names like "Relaxed 0.85× / Standard 1.0×" is cheap if every JEV timer is stored in game seconds `[INFERENCE]`.
**Pitfall.** Speed is the bluntest lever. It also slows the boring parts, it can't be per-player in co-op, and it does nothing about *proportions* (7.5 s vs 50 s stays 15 s vs 100 s at half speed).

### 3.2 Lethality and time-to-kill (Stormgate, CoH3)
**Mechanic.** Frost Giant gave Stormgate a slower speed than SC2 and lower lethality, "giving players more time to respond" ([Stormgate Wiki](https://stormgate.fandom.com/wiki/Stormgate); [VentureBeat](https://venturebeat.com/games/frost-giant-studios-unveils-social-real-time-strategy-game-stormgate/)). Relic first set CoH3's time-to-kill slower so players had "a reasonable window to make contact, decide how to react, and then execute". After feedback it raised early-game infantry DPS by ~25% ([Relic patch archive](https://help.relic.com/hc/en-us/articles/39307744455571-Company-of-Heroes-3-Patch-Notes-Archive)).
**Why it works.** TTK sets the *reaction window* without stretching travel or economy.
**Applicability.** The core issue is **structure** TTK, not unit TTK (§0). Lowering unit lethality across the board would make every fight longer.
**Pitfall.** CoH3 is the cautionary tale: too-low lethality read as mushy, and Relic walked it back.

### 3.3 Travel time and map size
**Evidence.** SC2 co-op maps were built with bigger bases, more expansions and wider choke paths for two players ([StarCraft Wiki: Co-op Missions](https://starcraft.fandom.com/wiki/Co-op_Missions)). Jon Shafer defines pacing as "the rate at which 'something interesting' happens" ([Game Developer](https://www.gamedeveloper.com/design/turn-based-vs-real-time)).
**Applicability `[INFERENCE]`.** Lengthening the 211 m lane or slowing the 4.2 m/s units adds *dead* time, not tension, which by Shafer's definition is worse pacing. Travel time is a valid lever only for the *defender's* response, and §1.7 removes that dependency directly.

### 3.4 Phase structure (Thronefall, Northgard, They Are Billions, Tooth and Tail)
**Mechanic.** Thronefall alternates day building with night defence (§1.8). Northgard's winter cuts food and forecasts disasters (§2.5; [Northgard calendar guide](https://northgard.wiki/Guide:Calendar)). They Are Billions schedules its final wave at ~90% of the duration (§1.8). Tooth and Tail caps matches at 12 minutes ([RPS](https://www.rockpapershotgun.com/tooth-and-tail)). Andy Schatz described the target problem: "frustrating to be in the middle of battle and then realize you forgot to queue up … Marines and you needed to be doing both at the same time" ([Game Developer](https://www.gamedeveloper.com/design/crafting-i-tooth-tail-i-a-gamepad-rts-that-aims-to-i-hearthstone-starcraft-i-)).
**Why it works.** A predictable rhythm lets players plan "now I build, then I attack". Schatz's complaint is exactly the playtest's split attention: building in base while a battle happens elsewhere.
**Applicability.** JEV's 2-minute versions are already a rhythm. Tie Lattice vulnerability or alert tiers to that rhythm, for example "the Lattice firewall drops when JEV ships v3".
**Pitfall.** Hard phases make every battle feel the same. Vary them per objective and per run modifiers.

### 3.5 Pause as an accessibility setting (They Are Billions, CoH3, Total War)
**Mechanic.** CoH3's Full Tactical Pause is "an optional singleplayer-only feature" for queuing orders ([TweakTown quoting Relic](https://www.tweaktown.com/news/80541/company-of-heroes-3-announced-adds-new-tactical-pause-feature/index.html)). TAB and Total War pause in single-player (§2.4, §2.5). SC2 co-op was framed by Blizzard as the casual, social mode: "co-op has a more casual slant compared to the ladder play" ([David Sum interview, TL.net](https://tl.net/forum/starcraft-2/528581-interview-with-david-sum-lead-co-op-designer)).
**Applicability.** Solo: offer a pause and optional auto-slow. Co-op: no unilateral pause. Pace has to come from structure and alerts.
**Pitfall.** Building the solo design *around* pause leaves co-op feeling frantic by comparison.

---

## Ranked lessons
1. **Gate the win with structure, not HP.** Dota, LoL and Deadlock all make the core invulnerable until outer objectives fall. At 120 DPS, no plausible HP reaches a 60–90 s window (§0).
2. **Make win-condition events global and non-spatial, own successes included.** The Dota and LoL announcers (§2.3) fix the inaudible Lattice alarm directly.
3. **End with a contested hold, not a hit.** AoE4 Sacred/Wonder, RoR2 and SC2 co-op holds (§1.6, §1.8) turn "won without noticing" into a team climax.
4. **Spawn the HQ's emergency defence in place.** Call to Arms and the Town Bell (§1.6, §1.7) remove the 50 s walk from the equation.
5. **Show objective progress persistently** (§2.6). Alerts are for transitions; the strip is for state.
6. **Protect the opening specifically.** LoL's 5-minute Fortification (§1.2) targets exactly the window the playtest exploited.
7. **Use global speed and lethality last**, as accessibility and tuning knobs (§3.1–3.2), not as the fix.

## Traps
- Doubling Lattice HP and declaring victory. It is still 15 s, and 3 s against five forces (§0).
- Adding an alarm that only the *JEV-side* listener hears, i.e. the current spatial cue.
- Hidden rules in the style of backdoor protection (§1.1). Every gate needs an on-structure visual and a one-line tooltip.
- Requiring every player or many forces present to progress (RoR2-style), which breaks no-micro and punishes a friend who is in the bathroom.
- Emergency defenders strong enough to *win*, which turns Assault into "wait for the timer".
- Lengthening maps or slowing units, which adds dead time (§3.3).
- Trusting AI-vs-AI medians: a 23.6 min median hides the 5-min tail. The harness needs a dedicated "unattended single-force rush" scenario with a minimum-battle-length assertion `[INFERENCE]`.
- Auto-pause in co-op: one player's alert must not stop four friends.

## Open questions
1. Should Assault stay a single-target kill (a gated assassination) or become a two-step "breach + uplink hold"? How different does that leave Raid?
2. What is the minimum acceptable time from "first force reaches Lattice" to "victory": 60 s, 90 s, or tied to JEV's next version tick?
3. Does gating scale with player count (5 forces = 600 DPS) through more pylons or a longer hold, or stay fixed?
4. Who hears which alert in co-op: everyone, or the force owner plus a team line? Which events are "team critical"?
5. Is the solo pause or auto-slow in scope for v1 of battle UX, or is solo treated as "co-op of one"?
6. Should JEV's emergency defence be a visible, cooldown-bound ability (Glyph) that players can bait, or a passive threshold?
7. Does the fortification window collide with JEV's own pacing (versions every 2 min), and should it end at v3 or at a fixed minute?
8. What should a player see if a win becomes imminent while their camera is elsewhere: a banner, a strip highlight, or a timed slow-down?

---

## Options and tradeoffs (checked against no-micro and co-op with friends)

### Topic 1: Protect the Lattice
| Option | What | Pros | Cons | Pillar / co-op check |
|---|---|---|---|---|
| **1A Pylon gate** (LoL/Dota) | Lattice invulnerable until 1 of 2–3 "Lattice pylons" in adjacent regions falls; the pylon fall is announced | Legible; adds 1–2 strategic decisions; creates an alert milestone | Assault looks more like Raid; more map content | ✅ region-level target choice; co-op can split pylons |
| **1B Lattice Field** (backdoor + Fortification) | 75% DR + regen unless supported (pylon down **or** ≥2 forces in region); field also always on before ~6 min | 1800 HP vs one unsupported force ≈ 60 s; protects the opening | Conditional rules are hard to read; needs strong visuals | ✅ no micro, but "≥2 forces" nudges cooperation, which is fine |
| **1C Final protocol** (Call to Arms + Deadlock phase) | At 50% HP: 8 s invulnerability, JEV spawns a capped garrison **at** the Lattice plus a global alarm | Fixes the walk-back gap; dramatic beat | Risk of rubber-band feel; tuning | ✅ JEV acts, players don't micro |
| **1D Uplink hold** (AoE4 sacred / RoR2) | Lattice at 0 → 60–90 s hold in its region; pauses while JEV contests, resets if region lost | Gives friends across the map time to converge; turns the rush into a climax | Can feel padded if JEV sends nothing; adds a state | ✅ any one force can hold; count forces, not players |

**Lean `[INFERENCE]`:** 1A or 1B, plus 1D with a 1C wave inside the hold. Each by itself still leaves a short window.

### Topic 2: Awareness
| Option | What | Pros | Cons | Pillar / co-op check |
|---|---|---|---|---|
| **2A Global announcer** (Dota/LoL) | 2D lines for pylon down, Lattice exposed/attacked/critical, relays n/3, regions held, *by whose force* | Cheapest fix for the playtest; works across the map | Line fatigue without throttles | ✅ zero input needed |
| **2B Objective strip** (AoE4/CoH bar) | Always-on per-battle-type progress with attacker force icons and timers | State is always visible; supports spectating friends | HUD space; must stay compact | ✅ read-only |
| **2C Event cue queue** (SC2 Space / CoH cues) | Cues with region + age; one key cycles; teammate cues shared | Precise follow-up when wanted | Jumping the camera is mild micro | ⚠️ optional only; never required |
| **2D Solo auto-slow/pause** (EU4 / CoH3 / TAB) | Solo toggle: slow to 0.5× or pause on "Lattice critical / victory imminent / HQ critical" | Strong for new and solo players | Impossible in co-op; can feel patronising | ✅ solo; ❌ co-op (use 2A+2B) |

Throttle rule for 2A `[INFERENCE]`: one line per event type per ~20 s; state transitions always speak; "under attack" repeats only on a new attacker or a tier change.

### Topic 3: Pace
| Option | What | Pros | Cons | Pillar / co-op check |
|---|---|---|---|---|
| **3A Speed preset** (SC2/AoE2) | Host picks Relaxed 0.85× / Standard 1.0×; JEV timers in game seconds | Trivial to add; accessibility | Slows boring parts too; doesn't fix proportions | ✅ host-set, same for all |
| **3B Structure TTK, not unit TTK** | Raise effective structure time via gates and DR; leave unit lethality alone | Targets the actual problem | Needs per-structure tuning | ✅ |
| **3C Phase rhythm** (Thronefall/Northgard) | Tie Lattice exposure to JEV versions (e.g. field drops at v3, ~6 min) | Predictable plan-then-strike loop; uses the existing schedule | Every battle risks feeling the same | ✅ forecastable |
| **3D Min-battle-length guard** (TAB final wave at 90%) | Final JEV wave or hold guaranteed to occur before victory | Kills the 5-min tail; ensures a climax | Feels scripted if visible as a rule | ✅ |

**Lean `[INFERENCE]`:** 3B + 3C first, 3D as a safety net, 3A as an accessibility setting, not the fix.
