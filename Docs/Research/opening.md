# Research: the repeated empty-base opening (Post-Frontier)

**Problem.** Every battle starts with an empty base, 600 Power per commander, a 2/s baseline and no units ([battle.md](../game/Docs/Design/battle.md), [run.md](../game/Docs/Design/run.md) "What carries between battles"). JEV's first new behaviour arrives at `v1.1` 2:00. So a group replays the same 0:00–2:00 construction routine 4–6 times an evening, inside battles meant to last 8–12 min. Opening the repo shows the routine has three parts: placing buildings (`PlacementPolicy`), configuring production roles, and assigning fronts (`ServerConfigureProduction`, `ServerAssignFront` in `ConstructionTests.cpp`). Each finding below uses this order: **Mechanic/finding** → why it matters (evidence) → applicability → pitfall.

---

## A. Evidence that the opening is a chore, and that designers cut it

1. **The AoE II Dark Age feels like "busy work".**
   → A new player called the first ~20 game-minutes *"a bunch of busy work … with not much else going on until you reach castle age"*. Replies agree the Dark Age is mostly "set yourself up and scout" ([Steam thread](https://steamcommunity.com/app/813780/discussions/0/3818544339812979996)). One reply points straight to Empire Wars as the fix.
   → Our 0–2 min has the same shape: setup with no opponent pressure until JEV `v1.1`.
   → Pitfall: veterans defend the opening as where economic choices live. The preview of one r/aoe2 thread reads: *"People hate on the dark age for being boring but the choices you…"* ([r/aoe2 thread](https://www.reddit.com/r/aoe2/comments/162uaju/dust_has_settled_whats_everyone_think_of_empire); full text was blocked, so only the search snippet was seen). If you cut the opening, move its decisions somewhere else rather than deleting them.

2. **StarCraft II: Legacy of the Void doubled the starting workers from 6 to 12.**
   → Blizzard's stated reason: *"to generally reduce the passive time-periods in the game"* ([LotV Multiplayer Preview](https://news.blizzard.com/en-gb/article/16654945/legacy-of-the-void-multiplayer-preview)).
   → This is the cheapest lever there is: change the starting numbers, add no new system. Our version would be more starting Power or a faster first construction.
   → Pitfall: it compresses the routine but does not remove it. Players still run the same opening, only faster [INFERENCE].

3. **Battle Aces (Uncapped Games, ex-SC2 lead designer David Kim) removed worker and building micromanagement.**
   → Buildings cost one button press and units are made instantly. Kim: *"We wanted to focus on what makes an RTS fun and cut out everything that gets in the way"* and *"point players toward the types of fun … as quickly as possible"*. Matches ran 5–10 min ([CNET](https://www.cnet.com/tech/gaming/battle-aces-is-streamlined-starcraft-for-fast-fun-pvp-matches)). Players picked 8 units before each match ([Wikipedia](https://en.wikipedia.org/wiki/Battle_Aces)). Beta reception was positive, but development stopped in May 2025 because of the game's performance in testing (same source).
   → This is the closest proof that a 5–10 min RTS works once the opening is cut, with the decision moved to a pre-match pick.
   → Pitfall: the game was cancelled, so being streamlined did not guarantee retention. Don't treat this as proof the model succeeds commercially.

## B. Pre-built and accelerated starts

4. **AoE II Empire Wars.**
   → Players start in Feudal Age with 27 villagers already working, a Scout, Loom researched, plus a Mill, 3 Lumber Camps, a Mining Camp, 6 Houses, 5 Farms and a Barracks ([official page](https://www.ageofempires.com/games/aoeiide/empire-wars); [wiki](https://ageofempires.fandom.com/wiki/Empire_Wars)). Matches tend toward *"heavy Feudal Age aggression"*, but Fast Castle and an instant second Town Center remain viable, so strategy still varies (wiki, Strategy).
   → The mode replaced Deathmatch in the ranked queue ([Update 50292](https://www.ageofempires.com/news/aoe2de-update-50292/)) and was ported to AoE III DE, Return of Rome and AoE IV. That is strong evidence it was well received.
   → Applicability: this is a direct template. Spawn each commander's kit buildings already standing, with production configured.
   → Pitfalls:
     - Civilizations with early-economy bonuses lost their identity (Japanese, Malians, Tatars weaken), while factions with free tech got much stronger (wiki). A pre-built start re-balances commanders.
     - Balance needed several patches: farms cut from 8 to 5, herdables removed, per-civilization villager fixes (wiki changelog).
     - Some maps need their own variant: Northern Isles swaps berries for fishing ships and a Dock (wiki).

5. **AoE IV Empire Wars uses a different start for each civilization.**
   → Each civilization starts with *"pre-built buildings that maximize their chosen civilization's strengths"*, e.g. English farms, the Delhi Mosque with research already running, the Abbasid House of Wisdom ([AoE IV news](https://www.ageofempires.com/news/empire-wars-has-come-to-age-of-empires-iv); [wiki](https://ageofempires.fandom.com/wiki/Empire_Wars)).
   → A pre-built start can show off each commander's identity. Pre-placing the **signature building** does that for us.
   → Pitfall: every faction × map combination needs hand-authored layouts, so content cost grows with the roster [INFERENCE].

6. **Against the Storm: the settlement opens with a Hearth and a warehouse already placed.**
   → A *"default settlement with only a hearth and warehouse"* ([Eurogamer](https://www.eurogamer.net/against-the-storm-review-a-perfectly-chaotic-city-builder)). Reviewers praise it as *"a city builder without any slog"* (same source). Settlements take *"an hour or two — just long enough for a single evening session"*.
   → Our HQ already plays this role. The lesson is that the one fixed anchor is pre-placed and everything else is a choice.
   → Pitfall: none specific.

7. **Thronefall: buildings go on predefined plots, each of which accepts one building type.**
   → *"You don't get to decide where to place any of your buildings"* ([New Game Network](https://www.newgamenetwork.com/article/2820/thronefall-review/)). Reviewers called it *"a breeze to play"*, but say it is *"lost when it comes to creativity and freedom"*. OpenCritic also lists the predefined layout as *"a major limitation"* ([OpenCritic](https://opencritic.com/game/15319/thronefall)).
   → Plots turn "where" into "what", which speeds the opening a lot. Region-anchored plots would fit our region-order pillar.
   → Pitfall: this is the most commonly cited complaint about the game. Use plots for the opening kit only, not for all building.

## C. A chosen starting package (the opening as a decision)

8. **Against the Storm embarkation.**
   → Before landing, players pick species and spend limited embark points on bonuses: goods, extra villagers, Amber, guaranteed blueprints ([GameRant](https://gamerant.com/against-the-storm-best-embarkation-bonuses-starting-perks-spend-points)). They see the biome first, so they can plan around it (e.g. bring humans for farmland). Seasonal effects are revealed only after landing ([Eurogamer](https://www.eurogamer.net/against-the-storm-review-a-perfectly-chaotic-city-builder)). Meta-progression adds embark options over time ([PC Gamer, 91](https://www.pcgamer.com/against-the-storm-review)).
   → This is the best model for "choose what the start looks like". The choice happens *before* the clock, with the map known.
   → Pitfall: one bonus becomes the obvious pick. GameRant calls Newcomers (+villagers) the best and Amber *"hard to see why any player wouldn't take"*. Price the options carefully or rotate them.

9. **Mechabellum 2.0 "Star Expedition" (Sept 2026) is a solo/co-op roguelite with a loadout limited by load cost.**
   → Players slot depot items (e.g. *Battlefield Piggy Bank*: 20% of last turn's leftover supply) under a load-cost cap, then pick a starting squad (e.g. 1 Steel Ball + 2 Fangs). There is no round timer in this mode, and in co-op *"each commander leads their own army onto a shared battlefield"* ([AllThingsHow](https://allthings.how/mechabellum-2-0-explained-star-expedition-mode-and-the-centurion-mech/); secondary source, launched last week).
   → This is a current co-op design for a start chosen under a budget.
   → Pitfall: removing the timer suits async PvE puzzles. In live co-op it invites the analysis paralysis covered in §F.

10. **Rogue Command (roguelite RTS, 1.0 in May 2026).**
    → Before each run players pick an Engineer, an Economy model (refinery and harvester) and a Specialist. Bases are *"called down"*. After each battle players draft Blueprints, Upgrades or Hacks ([Steam page](https://store.steampowered.com/app/1461910/Rogue_Command)). Steam reviews: 89% positive of 1,027.
    → The closest genre peer. The economy shape is chosen per run, and calldown building shortens construction.
    → Pitfall: the choice is per run, so every battle within a run still opens the same way [INFERENCE].

11. **Thronefall: before each level, players pick 1 weapon, up to 5 perks, and optional mutators** ([game.wiki](https://game.wiki/thronefall/getting-started)).
    → The run is short, so the "what the start looks like" decision sits in the loadout screen.
    → Pitfall: the loadout is chosen alone. In co-op that is fine; there is nothing to argue over.

## D. Deployment/planning phases

12. **Mechabellum versus mode.**
    → There is a 90 s deployment phase each round, the same length in 1v1 and 2v2 ([Steam suggestion thread](https://steamcommunity.com/app/669330/discussions/1/591784375625007573/)). Placements persist: the next round starts *"with everyone's previous placements repaired"*. You see the opponent's last round, not their next plan (AllThingsHow). RPS praises the near-perfect information and planning ([RPS review](https://www.rockpapershotgun.com/mechabellum-review)).
    → A planning phase becomes a decision when the opponent's state is readable. That matches JEV's planned published 20–30 s plans.
    → Pitfall: players report 90 s feels tight in later rounds (same thread).

13. **Bad North.**
    → There is no deployment phase. Commanders (squads) persist across islands and are upgraded with coins earned from surviving houses. If a commander's squad dies, that commander is gone ([PC Gamer, 78](https://www.pcgamer.com/bad-north-review)).
    → Squads persist; buildings don't (the island is new each time).
    → Pitfall: reviewers found the permadeath stakes *"at odds with the leisurely pace"* (PC Gamer). Hard loss of carried units feels punishing.

## E. Carrying something between battles without snowballing

14. **Dawn of War: Dark Crusade Honour Guard.**
    → Each conquered province unlocks one Honour Guard unit, which is bought with planetary requisition. Honour Guard *"start the game with your race's commander"* to repel early attacks ([DoW wiki](https://dow.fandom.com/wiki/Dark_Crusade/Honor_Guard); [GamesTemple guide](https://www.gamerstemple.com/games10/001205/001205g110.asp)). One province bonus (*Industrial Production*) gives *"additional resources"* at the start of every battle (guide). Enemy armies field their own Honour Guard. Province strength raises enemy tech and bases ([DoW wiki](https://dow.fandom.com/wiki/Dark_Crusade/Campaign)).
    → An elite, small carried cadre that answers the first minutes. The carry is paid for, not free.
    → Pitfall: enemy scaling has to keep pace, otherwise late battles become trivial [INFERENCE].

15. **Company of Heroes 2: Ardennes Assault, persistent companies.**
    → Three companies carry veterancy *and casualties*. A loss means *"try again with weakened forces"*, and battles won can still worsen the wider war ([PC Gamer, 81](https://www.pcgamer.com/company-of-heroes-2-ardennes-assault-review)). Requisition can't fill every skill tree ([Eurogamer, 8](https://www.eurogamer.net/company-of-heroes-2-ardennes-assault-review)).
    → Attrition is built-in anti-snowball: what you carry also carries your damage.
    → Pitfall: newcomers found it opaque (Eurogamer). A weakened group spirals down, the reverse of snowballing.

16. **Warcraft III campaign heroes.**
    → Hero levels are capped per mission by triggers, so a hero reaches level 10 only in the final mission ([Blizzard forum](https://us.forums.blizzard.com/en/warcraft3/t/hero-not-getting-exp-in-campaign/27061)).
    → The simplest anti-snowball: carry progress, but cap it per node depth.
    → Pitfall: XP earned above the cap is silently thrown away and feels like a bug (that thread is a bug report).

17. **StarCraft II co-op.**
    → Every mission starts a fresh base. Attack waves come on fixed timers; on Void Thrashing (Brutal) the first wave is at 3:00 or 4:00 ([starcraft2coop](https://starcraft2coop.com/missions/voidthrashing)).
    → Carry-over is commander power only: 90 mastery points, *30 per power set*, split between two perks. Points can be reset before any match ([levels](https://starcraft2coop.com/resources/levels); [wiki](https://starcraft.fandom.com/wiki/Co-op_Missions)). Prestige talents trade an advantage for a drawback, e.g. Vorazun P2: *"Combat units deal 25% reduced damage"* ([Vorazun](https://starcraft2coop.com/commanders/vorazun)).
    → Mengsk's *Starting Mandate* mastery originally let him drop two Bunkers at game start. Patch 4.11.3 changed it so double-Bunker expansions wait about 2 min ([Mengsk](https://starcraft2coop.com/commanders/mengsk)).
    → Lessons: caps, choose-one splits and side-grades with drawbacks work. Opening-boost carries get nerfed when they skip the first threat window.
    → Pitfall: never let a carried bonus make the opening's first threat irrelevant.

18. **Northgard Conquest.**
    → Before each battle, players choose 1 of 3 *favors* that last for the whole conquest. Stronger tiers unlock after the second clan challenge. Many favors change the opening directly: "Start with one/two warriors", villagers appear 40/50% faster ([Northgard wiki](https://northgard.fandom.com/wiki/Conquest)).
    → Choose-one with tier gating is a familiar, cheap way to carry *opening* bonuses.
    → Pitfall: favors stack over the conquest, so later battles open stronger. This is fine only if enemies scale [INFERENCE].

19. **Against the Storm: embark resources shrink with distance.**
    → As settlements get closer to the Seal, *"the amount of resources you can embark with dwindles"* while minimum difficulty rises ([Eurogamer](https://www.eurogamer.net/against-the-storm-review-a-perfectly-chaotic-city-builder)).
    → The starting package shrinks with run depth instead of growing. This is the opposite of a snowball, and maps cleanly onto node depth.
    → Pitfall: a shrinking start in late acts may make our late battles' openings *longer*, not shorter.

20. **They Are Billions campaign (a trap).**
    → Every mission rebuilds the colony from scratch with gated tech: *"Build a town with a population of 400. Okay, now do it again but get to 600"*. Swarm-mission resources are *"dictated by how many missions you've done"* ([Vice](https://www.vice.com/en/article/they-are-billions-gets-the-single-player-campaign-nobody-needed)).
    → This is exactly the risk we face: a repeated opening plus progression that only adds numbers reads as a grind.

## F. Co-op planning phases: table talk or delay?

21. **Payday 2 pre-planning.**
    → Before a heist, the crew sees the map blueprint, can **draw on it**, and spends a **shared, capped favor budget** that does not scale with difficulty. Escape-plan choices need a majority vote ([Payday wiki](https://payday.fandom.com/wiki/Pre-Planning)). A split vote is resolved by the host ([Steam guide](https://steamcommunity.com/sharedfiles/filedetails/?id=278004028)).
    → A shared budget on a map creates table talk ("you take the doctor bag, I'll buy the keycard"). Votes resolve arguments.
    → Pitfall: no timer is documented, so a slow group can stall. Host tie-break concentrates power in one player.

22. **The alpha-player ("quarterbacking") problem.**
    → In co-op games with full shared information, one player tends to direct everyone ([Pandemic, Wikipedia](https://en.wikipedia.org/wiki/Pandemic_(board_game)); [Meeple Mountain](https://www.meeplemountain.com/articles/benching-the-quarterback-how-to-deal-with-alpha-players-in-co-op-games/)). The usual fixes are hidden or individual information, or real-time pressure. *"Real-time play seems to be the only surefire solution"* ([The Alexandrian](https://thealexandrian.net/wordpress/35063/board-games/thought-of-the-day-quarterbacking-in-co-op-games)).
    → An untimed shared planning screen is where an alpha player takes over. Our commanders own separate budgets and kits, which already splits authority.
    → Pitfall: a planning phase without a timer and with one shared pool is the worst case.

23. **Mechabellum 2v2.**
    → Players talk placements over Discord, review each other's boards and call out threats ([Steam thread](https://steamcommunity.com/app/669330/discussions/0/591762230004967427/); [MechaTactics](https://www.mechatactics.com/mechabellum-2v2-basic-team-strategies)). The 90 s timer bounds it, though some find it tight.
    → Answer to Q4: a timed phase with private budgets and a shared board produces talk without stalling.

---

## Answers to the four questions

1. **Shortening or varying the opening.**
   - Pre-built start: Empire Wars, and the per-civilization variant in AoE IV.
   - Accelerated start: more workers (LotV), instant production (Battle Aces), calldown bases (Rogue Command).
   - Predefined plots (Thronefall).
   - Map-specific starting situations: Empire Wars water-map variants; DoW stronghold missions that start with an army and no base ([DoW wiki](https://dow.fandom.com/wiki/Upper_Wastes)).
   - Partial carry: Honour Guard, Ardennes companies.
2. **Keeping it a decision.** Choose the start *before* the clock with the map known: ATS embark, Mechabellum depot, Northgard favors, Rogue Command loadout. Or keep a pre-built start open enough that several strategies stay viable (Empire Wars still allows Fast Castle and a second Town Center).
3. **Carrying without snowballing.**
   - Hard caps: WC3 level cap per mission; SC2 30 points per set.
   - Choose-one: Northgard favors, SC2 split perks.
   - Side-grades with drawbacks: SC2 prestige.
   - Attrition and decay: Ardennes casualties.
   - A budget that shrinks with depth: ATS embark.
   - Paying to field the carry: DoW requisition.
   - Nerf carries that skip the first threat: SC2 Mengsk 4.11.3.
4. **Planning phase in co-op.** It produces table talk when the phase is timed, each player has their own budget, the board is shared and drawable, and there is a visible opponent intent to answer (Mechabellum, Payday). It causes delay or an alpha player when it is untimed and uses one pool (alpha-player literature; Mechabellum "tight timer" complaints show the opposite failure).

## Ranked lessons

1. **Move the opening's decisions before the clock, then skip its execution.** Every well-received example (Empire Wars, ATS, Battle Aces, Northgard) keeps a choice and removes the routine.
2. **Start states should differ by commander and by node, not by run progress.** Identity comes from AoE IV's per-civilization starts; variety comes from Empire Wars map variants. Neither snowballs.
3. **Timebox any planning phase (30–60 s for us), give each commander a private budget, and use a shared drawable map.** This gives table talk without an alpha player.
4. **Show JEV's first plan during planning.** Mechabellum's readable state is what turns placement into a decision. This also tests our planned JEV plan timeline early [INFERENCE].
5. **If anything physical carries over, cap it, make it choose-one, attrit it, and make it cost Power to field.** Ardennes, DoW, WC3, SC2.
6. **Starting bonuses must not erase the first threat.** Keep JEV `v1.1` meaningful (the SC2 Mengsk nerf).
7. **Re-balance commanders after any pre-built start.** Empire Wars needed several patches.

## Traps

- **Faster but the same.** More Power or faster builds shorten the routine but keep it identical every battle (LotV-style) [INFERENCE].
- **The "obvious pick" embark bonus.** One package dominates (ATS Amber, Newcomers).
- **The They Are Billions loop.** A repeated rebuild plus number progression reads as a grind.
- **Untimed shared planning** leads to an alpha player or a stall. **A timer that is too tight** leads to frustration (Mechabellum).
- **Locking placement completely** (Thronefall) costs player creativity. Limit it to the opening kit.
- **Snowball through an opening bonus.** A carried army arrives before JEV's `v1.1` and the first 2 min stop mattering.
- **Faction identity loss.** A pre-built start erases commanders whose strength is the early game (Japanese/Malians in Empire Wars).

## Open questions

1. How long does the opening actually take today? Measure time-to-first-force-order per commander in the harness, not by feel.
2. Is the boring part *placing* buildings, *configuring* production and fronts, or *waiting* for construction? Each points to a different option.
3. Should JEV get a matching head start (a pre-built Lattice outpost) so `v1.0`–`v1.1` remain a contest?
4. Does a planning phase pause the simulation, or run under JEV's `v1.0` clock?
5. With 4–5 players, is the planning phase per commander in parallel, or one shared screen? How do AI adjutants and late joiners behave in it?
6. Should the start package shrink with node depth (like ATS) or grow (like Northgard)? Which suits a 2–3 act curve?
7. Does pre-placing the signature building over-centralise each commander's build?
8. Can the existing `PlacementPolicy` validate pre-battle placements without a live HQ territory? [INFERENCE: probably needs a planning-mode flag.]

## Options for Post-Frontier

| | A. Forward Deployment (timed planning phase) | B. Landing Kit (pre-built start) | C. Requisition (point-buy start package) | D. Cadre (capped unit carry-over) |
|---|---|---|---|---|
| What | 45 s phase before 0:00. Each commander places their kit on region plots, sets roles and fronts, sees JEV's first published plan, and draws on the shared map. Construction begins when the phase ends | Kit production buildings spawn already built and configured. The signature building is not pre-placed. Power is reduced to compensate. Layout varies per node | Before each battle, spend N requisition points: an extra pre-built building, a starter force, +Power, faster first build, or reveal JEV plan #2. N is fixed or shrinks with depth | One force (≤ X supply) survives into the next battle. It costs Power to field, keeps its casualties, and is lost if wiped. One per commander |
| Time saved | ~60–90 s of placing and configuring [INFERENCE] | ~90–120 s [INFERENCE] | 30–90 s depending on the pick | Small: the base is still built from scratch |
| Decisions added | High: placement, plus team coordination against JEV's plan | Low (removed); medium if 2–3 layout templates are offered | High: a priced choice that reacts to the node | Medium: keep or fold the force, whom to protect |
| Snowball risk | None (inside one battle) | None | Low if the budget isn't earned by performance; medium if it is | Medium–high; needs a cap, attrition and a field cost |
| Build cost | Medium: planning game state, ghost placement off live territory, timer UI, JEV plan preview | Low: spawn-at-start data per commander and node | Medium: package data, a screen, balancing | High: unit serialization across battles, UI, balance, adjutant handling |
| Precedent | Mechabellum, Payday 2 pre-planning | AoE II/IV Empire Wars | ATS embarkation, Northgard favors, Mechabellum depot | DoW Honour Guard, Ardennes Assault |

**Recommendation [INFERENCE]:**
- **A + B together.** B removes the routine; A keeps "where and against what" as a timed team decision on the map, and doubles as the first appearance of JEV's published-plan timeline.
- **Add C later** as the per-node variety lever. Its budget should come from node type or depth, not from performance.
- **Treat D as a card or Protocol** (in the spirit of the "Demo Unit" memo, which lasts one battle), not a default. That keeps the run.md rule that units don't carry over.
