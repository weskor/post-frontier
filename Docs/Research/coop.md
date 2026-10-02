# Research: Co-op strategy & co-op PvE design → lessons for Post-Frontier

Scope: how co-op games make each player's contribution distinct, handle shared vs. private economies, scale from solo to 5 players, avoid the alpha player, and let people talk strategy without voice. Every lesson is checked against our constraints: no unit micro, region-goal commands per barracks, 8–12 min battles, a branching run with drafts, a shared lives pool, team income split evenly with private wallets and free gifting, and tuning for solo plus 2–3 players.
Tags: `[INFERENCE]` means my own reasoning, not something a source says. "Community" means player reports, not developer statements.

---

## 1. StarCraft II Co-op Missions (Blizzard): the closest analogue
**Mechanics.**
- Two players each pick a *commander* with a distinct playstyle, units and "top-bar" calldown abilities. At launch: Kerrigan was hero control, Zagara a disposable swarm, Swann mech with free-gas factories and a map-wide Laser Drill ([Blizzard, LotV Co-op](https://news.blizzard.com/en-us/article/19930660/legacy-of-the-void-features-co-op-missions)).
- Missions are objective-based. Matchmaking pairs players by skill *and* commander level.
- Leveling unlocks new units and abilities ([same](https://news.blizzard.com/en-us/article/19930660/legacy-of-the-void-features-co-op-missions)).
- Patch 3.3 added Mutators, Weekly Mutations (stacked mutators with XP bounties per difficulty) and Mastery points spent on per-commander stat categories ([Patch 3.3](https://news.blizzard.com/en-us/article/20112699/patch-3-3-new-co-op-content-and-features)).
- Patch 5.0 added Prestige: replay a commander's 1–15 progression to unlock one of 3 talents that change core play, each **with an explicit downside**. Examples: "Dehaka gets a brother. However, they both have less life"; Abathur's uncapped Ultimate Evolutions in exchange for half-value Biomass. Only one talent can be equipped, and "none" is allowed ([5.0 notes](https://news.blizzard.com/en-gb/article/23482838/starcraft-ii-5-0-patch-notes)).
- The 5.0 "Boom Bots" mutator: "One player must discern the disarming sequence and the other player must enter it" ([same](https://news.blizzard.com/en-gb/article/23482838/starcraft-ii-5-0-patch-notes)).

**Why it works (evidence).**
- Designer Kevin Dong says commanders must be "balanced for skill", not only for power: "Newer players should be able to mass a few units and a-move them… they might even be able to succeed… with a good partner in brutal difficulty. But more experienced players should have more powerful options."
- He cites Nova: she works at both ends of the skill range, and her best unit mix changes with the map and enemy ([Dong interview](https://news.blizzard.com/en-us/article/21574696/co-op-designer-kevin-monk-dong-shares-his-story)).
- The same interview warns that speedrun data misses "how you work together with your ally" and player skill.

**Applicability.**
- Commanders are the right unit of distinction for us. Our players already have no micro, so a commander should differ by *which region verbs, barracks types and calldowns it owns*, not by unit control.
- Weekly mutations translate directly into run mutators and node modifiers.
- **Prestige-style sidegrades fit our "unlocks only" rule; Mastery does not.** Mastery is permanent stat power, which is exactly what we excluded.
- Boom Bots shows how to make communication necessary through split information, not split controls.

**Pitfall.**
- With two commanders, both players get the full mission. Pairing strengths can still make one player "the carry" [INFERENCE].
- Mastery-style power creep forces ever-harder difficulty tiers (Brutal+ exists for exactly this reason) [INFERENCE].

## 2. Helldivers 2 (Arrowhead): pooled lives, team-wide strategic abilities, a scripted meta-war
**Mechanics.**
- Reinforcement tickets work as a shared lives pool: "five tickets per player and mission, up to a maximum of 20 in a full squad."
- When the pool is empty, one ticket regenerates every 2 minutes. A wipe with no tickets fails the mission ([Helldivers wiki, Reinforce](https://helldivers.wiki.gg/wiki/Reinforce)).
- Boosters are picked per player but apply to the entire team ([IGN Boosters](https://www.ign.com/wikis/helldivers-2/Boosters)).
- A human "Game Master" (Joel) chooses Major Orders and where enemies attack, can "reinforce planets" and grant temporary stratagems. Outcomes are decided by players: "The war fought and liberation of planets — that's all you" ([Pilestedt via Windows Central](https://www.windowscentral.com/gaming/helldivers-2-director-explains-the-limits-of-joel-the-game-masters-power)).
- Scaling: patrols are meant to scale linearly ("one player has 1/4th of the patrols compared to four players"). A bug made it 1/6. The fix provoked solo complaints because "you don't have players in different locations diverting enemies… they are all on you" ([PC Gamer](https://www.pcgamer.com/games/third-person-shooter/helldivers-2-devs-explain-why-solo-missions-have-more-patrols-now-the-intention-is-that-one-player-has-14th-of-the-patrols-they-had-16th)).

**Why it works.**
- A per-player pooled budget means one careless player costs everyone, which pressures cohesion without punishing anyone individually.
- The timed trickle prevents a hard lockout.
- Team-wide boosters make each player's pre-mission pick visibly benefit everyone.

**Applicability.**
- Our lives pool (~3) is coarser: a life per *battle*. Borrow the "budget scales with headcount" idea for the in-battle layer, e.g. HQ repair charges or barracks rebuild tokens scaling with N.
- Each commander's draft pick could include one team-wide "booster" so everyone's choice is visible to the others.
- A Joel-like *authored* layer over the run map fits The Machine's voice: "Scheduled maintenance of Region 7 at 04:00" [INFERENCE].

**Pitfall.**
- Linear scaling by player count ignores *threat splitting*. A solo commander cannot hold multiple regions at once, so 1/N of the threat is still harder than 1/N of the difficulty. Tune solo separately.

## 3. Deep Rock Galactic (Ghost Ship): tool roles and a shared team wallet
**Mechanics.**
- Each class owns a terrain or mobility tool the others lack: Driller drills, Engineer platform gun, Gunner zipline plus shield generator, Scout grappling hook plus flare gun ([DRG wiki equipment table](https://deeprockgalactic.wiki.gg/wiki/Resupply_Pod)).
- Resupply costs 80 Nitra "from the team's resources" and any dwarf can call it. The pod has 4 racks, one per player in practice, and excess "cannot be stored or transferred" ([Resupply Pod](https://deeprockgalactic.wiki.gg/wiki/Resupply_Pod)).
- The Laser Pointer marks are visible through walls. In solo, the same pointer directs the bot companion Bosco to mine and fight ([Laser Pointer](https://deeprockgalactic.wiki.gg/wiki/Laser_Pointer)).
- Hazard level sets base difficulty; "increased player count increases enemy damage even further" ([Difficulty Scaling](https://deeprockgalactic.wiki.gg/wiki/Difficulty_Scaling/WIP)).

**Why it works.**
- Roles are *verbs on the shared map* (tunnels, platforms, ziplines, light) that change the space for teammates. Every contribution is visible and persistent.
- The shared Nitra wallet turns gathering into team service, and spending it is an obvious team act.
- Solo play keeps the same command language: the ping that talks to humans also commands Bosco.

**Applicability.**
- For a no-micro RTS this is the key idea. Each commander should *alter the battlefield for allies*: reveal a region, fortify an anchor, open a route, speed captures in an area. That visible persistent mark is how contribution becomes legible.
- Solo: give the solo player an AI adjutant commander driven by the same ping/intent markers humans use.

**Pitfall.**
- A team wallet anyone can spend invites waste or griefing. DRG limits damage with a fixed price and a short cooldown between calls [INFERENCE].

## 4. Risk of Rain 2 (Hopoo): explicit player-count math and pooled unlocks
**Mechanics.**
- The published difficulty coefficient is
  - `playerFactor = 1 + 0.3·(n−1)`
  - `timeFactor = 0.0506·difficulty·n^0.2`
  - `coeff = (playerFactor + minutes·timeFactor)·1.15^stages`.
- Interactable costs scale as `coeff^1.25`. Spawn credits and gold rewards scale with coeff ([RoR2 wiki, Difficulty](https://riskofrain2.wiki.gg/wiki/Difficulty)).
- "Gold from killing enemies and opening barrels is shared between players" ([Gold](https://riskofrain2.fandom.com/wiki/Gold)).
- In multiplayer, "the Artifacts unlocked by all players in the lobby are pooled" ([Artifacts](https://riskofrain2.fandom.com/wiki/Artifacts)).

**Why it works.**
- Sublinear scaling (+30% per extra player, not +100%) rewards bringing friends.
- Time pressure, rather than raw HP, is the main difficulty lever.
- Shared kill gold removes "who gets the last hit" friction.
- Pooled unlocks mean a newcomer joining a veteran gets the veteran's toys for that run.

**Applicability.**
- Copy the *shape*: JEV's budget = base × (1 + k·(N−1)) with k < 1, multiplied by a per-node escalation (≈1.15 per battle). Prices in Power scale with N so private wallets do not trivialize the shop.
- Pool unlocked commanders, cards and mutators across the lobby. This directly supports the "unlocks only" rule and onboarding friends.
- Our "team income split evenly" matches RoR2's shared kill gold.

**Pitfall.**
- Community threads argue multiplayer runs become trivially easy as stacked items snowball ([Steam discussion](https://steamcommunity.com/app/632360/discussions/0/2141966124582714590)). Sublinear threat plus superlinear team synergy can break high counts. Tune separately at 4–5 players.

## 5. Left 4 Dead AI Director (Valve, Booth GDC 2009): pacing and *requiring* cooperation
Source for all points below: [Booth, Replayable Cooperative Game Design](https://cdn.steamstatic.com/apps/valve/2009/GDC2009_ReplayableCooperativeGameDesign_Left4Dead.pdf).

**Mechanics.**
- Each survivor's "emotional intensity" is tracked. The Director runs a cycle: Build Up to a peak, Sustain Peak for 3–5 s, Peak Fade, then Relax for 30–45 s with no mobs or specials. Bosses ignore pacing.
- Boss events are dealt from a shuffled deck (Tank, Witch, Nothing) with no repeats.
- Each special infected exists to punish a specific anti-co-op behaviour: the Hunter kills "lone wolf" players, the Smoker pulls apart tight teams.
- Automatic voice callouts improve situational awareness.

**Why it works.**
- Booth: "Ensure cooperation is the only winning strategy"; "Treat entire Survivor team as 'the player'"; "Avoid artificial/arbitrary enforcement… no invisible leashes."
- "Low probability + High drama = Memorable."
- "If an event is exciting, it will be more so if it broadcasts its impending arrival."
- Scripted when/where "kills cooperation… players expect everyone to have memorized all encounters."
- Bots "allowed us to assume baseline 4 player Survivor team for game tuning"; drop in/out "incredibly valuable."

**Applicability.**
- JEV re-plans every 2 s deterministically. Add a *director layer* on top that modulates JEV's aggression from a team stress signal: HQ damage, regions lost, wallet starvation.
- Telegraph JEV's big attacks with in-voice warnings ("The Machine has scheduled your deprecation for 03:00"). That gives players dramatic anticipation and time to coordinate without micro.
- Give JEV "specials" that punish anti-co-op play, e.g. a raider that targets the commander with the most undefended extractors.
- Bot commanders (an AI ally running JEV's planner on our side) for solo and for drop-out.

**Pitfall.**
- Adaptive difficulty that players can detect feels like rubber-banding [INFERENCE]. Booth's crude intensity model worked because it modulated *pacing*, not outcome.

## 6. Legion TD 2 (AutoAttack): an auto-battler with team economy, no micro
Source for all points below: [Legion TD 2 manual](https://beta.legiontd2.com/manual).

**Mechanics.**
- Fighters fight automatically; players only build and send.
- Spending mythium on sends or king upgrades permanently raises income, so economy and aggression are the same decision.
- Your fighters that clear a wave "teleport to your king to catch allied leaks."
- An AUTO-send toggle for new players.
- Pings: ground pings, plus a waves-bar ping to "signal when you are thinking about sending or expecting a send."
- Legion Spells: "All players in a given match receive the same choices."
- Classic mode gives *bonus income to significantly lower-rated players*. Duo ratings are adjusted because duos "tend to be more coordinated."
- A disconnect pauses the game for 60 s.
- MVP is explained on mouseover.

**Why it works.**
- Fighters fight on their own, so all skill expression is strategic: what to build, when to invest, when to send. That is our design target.
- Catching allied leaks is a *built-in, visible rescue* that needs no coordination.
- The handicap income lets mismatched friends play one match.

**Applicability.**
- Very high. A barracks that completes its region goal could auto-offer "relief" to an adjacent struggling region, our version of leak-catching.
- Add a timed intent ping ("Assaulting enemy main in 30 s") to sync commanders.
- A per-commander handicap income/HP option, set privately, covers skill gaps between friends.
- An explained post-battle MVP/contribution card so everyone sees what they did.

**Pitfall.**
- Its PvP lane structure makes blame obvious ("you leaked"). Contribution displays must celebrate, not shame [INFERENCE].

## 7. Age of Empires (team economy) and Northgard Conquest (run-of-battles vs AI)
**Mechanics.**
- AoE I/II/IV tribute carries a 30% tax. Its stated purpose includes discouraging "giving huge amounts of resources to allies without any penalization." Market techs (Coinage, Banking) reduce or remove it.
- AoM Retold's "Supporter" AI periodically tributes human allies ([AoE wiki, Tribute](https://ageofempires.fandom.com/wiki/Tribute)).
- In SC2 team games, a leaver's gathered resources are split among allies, who can also control the leaver's units ([Arqade](https://gaming.stackexchange.com/questions/7001/starcraft-2-leavers-resources); versus mode, community-sourced).
- Northgard Conquest, playable solo or co-op, is "a succession of custom games ('battles') against AI opponents" with special rules. Each run has 3 fixed clan challenges plus ≥4 of 8 randomized intermediate battles, and a choice of 1 of 3 run-long "favors" before battles ([Northgard wiki](https://northgard.fandom.com/wiki/Conquest); [Steam announcement](https://store.steampowered.com/news/app/466560/view/1609398889579606445)).

**Why it works.**
- A transfer fee keeps team play from collapsing into one mega-player.
- Northgard proves the RTS-battles-on-a-map run structure ships; fixed anchor battles plus random fillers balance authored and procedural content.

**Applicability.**
- We chose *free* gifting. Keep it free, but make it **logged and visible**, and possibly limited by a short cooldown so one rich player cannot pump a single "main" commander [INFERENCE].
- For leavers, transfer barracks and wallet to an AI adjutant or to allies (SC2 precedent).
- Northgard's anchor-plus-filler map is a template for our node map: commander-specific "signature" nodes plus random ones.

**Pitfall.**
- A tax punishes the friend who wants to help a struggling newbie. It solves a *competitive* problem we mostly do not have. Visibility beats a tax in co-op [INFERENCE].

## 8. Co-op board games: Pandemic, Spirit Island, and the alpha player
**Evidence.**
- Pandemic gives each player a role that bends the rules (the Medic treats all cubes, the Scientist cures with 4 cards). Yet "a criticism of the game, dubbed 'quarterbacking', is that there is a tendency for one player… to control the game" ([Wikipedia](https://en.wikipedia.org/wiki/Pandemic_(board_game))). **Distinct roles alone do not prevent an alpha player.**
- Spirit Island: "Play is simultaneous within each phase. Players may confer as they wish" ([rules reference](https://tesera.ru/images/items/1181464/SpiritIsland_RulesReference_v1.1.pdf)). Players credit simultaneous play and each person's busy private board with discouraging quarterbacking ([BGG thread](https://boardgamegeek.com/thread/1825210/how-much-of-an-issue-is-quarterbacking-in-this-gam), community).
- Christian Beck's analysis ([design discussion](https://chrisbeckdesign.com/2020/07/13/design-discussion-alpha-players-and-quarterbacking)):
  - With open information, deferring to the best player is the *rational* way to win.
  - Fixes: hidden information that must be *actively* protected, real-time pressure (which "can lead to… guilt"), decisions that are less plan-critical, goals obvious enough that nobody needs instructions, or an explicit leader role (Captain Sonar).

**Applicability.**
- Real time plus no micro helps: there is less to dictate. But our command set is small (4 verbs × N barracks), so one player *can* call everything [INFERENCE].
- Counter that with:
  - **private information**: each commander sees their own draft offer, their own commander-only intel (e.g. a scout commander sees JEV's next target), and a private calldown hand;
  - **simultaneous, private drafts** between battles;
  - **per-commander ownership**: only you can command your barracks, and there is no shared control (unlike SC2's shared-control option).
- Rotate the run-map route choice ("navigator") or use voting, so one person cannot steer the whole run.

**Pitfall.**
- Too much hidden information in a real-time game makes coordination impossible and breeds frustration. Hide *options*, not *state* [INFERENCE].

## 9. Communication without voice: Apex Legends smart pings (Respawn)
**Mechanic.** One button places a context-sensitive marker and triggers a character voice line. A wheel offers specific pings ([Game Developer](https://www.gamedeveloper.com/design/respawn-played-with-muted-mics-to-get-i-apex-legends-i-smart-comms-system-just-right)).

**Evidence.** Respawn playtested for a month with **muted mics and anonymous names**, so squads had to coordinate entirely through pings ([same](https://www.gamedeveloper.com/design/respawn-played-with-muted-mics-to-get-i-apex-legends-i-smart-comms-system-just-right)). L4D's auto-callouts serve the same role ([Booth](https://cdn.steamstatic.com/apps/valve/2009/GDC2009_ReplayableCooperativeGameDesign_Left4Dead.pdf)).

**Applicability.**
- Our commands *are* intents. Render every commander's current barracks orders as coloured arrows on the shared map: Hold here, Expanding along this path, Assaulting main.
- Add context pings on regions: "I'll take this", "Need help", "JEV massing here", "Assault in 30 s", "Gift me Power".
- Voiced lines from each commander's character, in the setting's funny writing, carry the comms.
- Playtest with mics muted, exactly as Respawn did.

---

## Cross-cutting answers to the brief

**Distinct, visible, necessary contribution.**
- Give each commander (a) an exclusive verb that changes the map for allies (DRG), (b) a team-wide passive or booster (HD2), and (c) a calldown nobody else has (SC2).
- Visibility: colour-coded ownership of regions, extractors and barracks; persistent marks such as fortifications and reveals; a post-battle contribution card (Legion MVP-with-reasons).
- Necessity: build JEV threats that need two verbs at once, e.g. a fortified region that needs Siege *and* a capture-speed commander. Use Boom-Bots-style split information.

**Shared vs. private resources.**
- Even split plus private wallets matches RoR2's shared kill gold, which removes contention.
- Risks:
  - *funneling*: everyone gifts to one "main", which recreates the alpha player through money;
  - *hoarding/AFK*: a wallet that does nothing;
  - *draining*: repeated begging.
- Mitigations: a visible gift log, a gift cooldown or cap per minute, idle wallets auto-pooling to the team after X s [INFERENCE].
- Resolve the conflict that **Extractors pay only their builder**, which contradicts "team income split evenly". Decide which is canonical.

**Scaling solo → 5 and skill disparity.**
- Sublinear threat scaling (RoR2 +30% per player) plus solo-specific relief (HD2 lesson), e.g. an AI adjutant (DRG's Bosco, L4D bots).
- Optional private handicap income for weaker friends (Legion Classic).
- Pool unlocks across the lobby (RoR2).
- An AUTO helper for novices (Legion AUTO-send → an "auto-expand" doctrine).
- Drop-out: transfer to an AI adjutant (L4D "Take a Break") and pause 60 s on disconnect (Legion).
- Drop-in mid-battle: take over an adjutant's barracks.

**Alpha player.**
- Ownership locks, private drafts and intel, simultaneous decisions, rotating route choice.
- Make the obvious plan readable without instruction: shared goals on the map (Beck's "plan clear from the get-go").

**Communication.** Intent arrows plus context pings plus voiced callouts. Test with muted mics.

## Top 7 transferable lessons (ranked)
1. **Commanders differ by map-altering verbs and team-wide effects, not stats** (DRG tools, SC2 commanders, HD2 boosters). Each player's mark on the battlefield must be visible and persistent.
2. **Make cooperation the only winning strategy through enemy design, not rules** (L4D specials). JEV should field threats that need two commanders' verbs at once and punish lone overextension.
3. **Scale threat sublinearly and tune solo separately** (RoR2 `1+0.3(n−1)`; HD2's solo backlash). Give solo an AI adjutant using the same command language (DRG Bosco).
4. **Your commands are your comms**: render every barracks order as a shared intent marker; add context and timed pings; test muted (Apex, Legion waves-bar ping).
5. **Fight the alpha player with ownership plus private options**: only you command your barracks; drafts are simultaneous and private; route choice rotates (Spirit Island, Beck, Pandemic's failure).
6. **Pace with a director over JEV and telegraph big attacks** (L4D Build-Up/Peak/Relax; "dramatic anticipation"). It fits 8–12 min battles and the corporate-AI voice.
7. **Run variety via mutators and sidegrade unlocks pooled across the lobby** (SC2 Weekly Mutations and Prestige talents with downsides; RoR2 pooled artifacts; Northgard anchor-plus-random battles). No Mastery-style stat power.

## Traps to avoid
- **Distinct roles ≠ no quarterbacking** (Pandemic). Distinct roles with open information and few decisions still get dictated.
- **Shared control of barracks**: it hands the game to the most experienced player.
- **Linear per-player threat scaling** that ignores threat splitting (HD2).
- **Permanent stat unlocks** (SC2 Mastery) that inflate difficulty tiers and break our "unlocks only" rule.
- **Scripted when/where for JEV**: it is memorizable, and veterans race ahead (Booth).
- **Invisible leashes**: arbitrary penalties for not cooperating. Players rebel (Booth).
- **Shaming contribution UI**: Legion-style blame for "leaks". Celebrate rescues instead.
- **Money funneling** as a backdoor alpha player under free gifting.
- **Detectable rubber-banding** in the director.

## (a) What keeps players replaying (co-op domain evidence)
- Commander × ally combinatorics and per-commander progression (SC2).
- Weekly rotating mutators with bounties (SC2 3.3).
- Structured unpredictability: "Low probability + High drama = Memorable" (Booth).
- A shared, authored meta-war whose outcomes players decide (HD2 Joel).
- Sidegrade talents that change how a familiar commander plays (SC2 Prestige).
- Friends as content: games that let mismatched friends play together get replayed together (Legion Classic handicap, RoR2 pooled unlocks).

## (b) What these games reward
- Planning ahead and economic timing over execution (Legion: income from mythium, fixed wave order).
- Composition matched to the threat (Nova by map; HD2 loadouts).
- Sticking together and rescuing teammates (L4D).
- Using your unique tool *for others* (DRG).
- Spending the shared budget wisely (HD2 tickets, DRG Nitra).

The no-micro constraint pushes all reward onto these strategic behaviours, which is the intent.

## (c) Open questions for us
1. Extractors paying only the builder vs. the "team income split evenly" decision: which is canonical?
2. Does each commander own a fixed barracks type, or a verb and calldown on top of any barracks?
3. Which JEV threats *require* two commanders, and how is that readable with no micro?
4. What private information does each commander get (draft offers, intel, calldowns)? How much is too much in real time?
5. Solo: is there an AI adjutant commander, extra barracks, or a lower threat budget?
6. Lives pool vs. player count: does a 5-player team also get ~3 lives, or does the pool scale like HD2 tickets?
7. Who chooses the route on the node map? Vote, rotating navigator, or the commander whose signature node it is?
8. Drop-out mid-battle: does an AI adjutant take over, or allies? What happens to that player's wallet?
9. Should gifting be capped, or delayed and logged, to stop funneling to one "main"?
10. Does a director layer sit above JEV's deterministic planner, and how do we hide it from players?
