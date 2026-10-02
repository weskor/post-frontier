# Roguelite RTS & run-structure research — for Post-Frontier (CoopRTS)

Scope: run maps, between-battle drafting, carry-over, bosses/ascension, failure handling. Evidence is linked. `[INFERENCE]` marks my extrapolation.
Run budget used throughout: 60–90 min run ÷ (8–12 min battle + ~2 min draft) ≈ **5–7 battles per run**. That is far fewer reward moments than Slay the Spire (~50 floors), so **each pick has to carry more weight**.

## At a glance

| Game | Run unit / length | Choice per reward | What carries over | Loss handling | Difficulty ladder |
|---|---|---|---|---|---|
| Slay the Spire | 3 acts, branching map | 1 of 3 cards or skip; boss relic pick | Deck, relics, HP, gold | Run over | Ascension 1–20, cumulative |
| FTL | 8 sectors, ~20 beacons each | Shops/events | Ship, hull, crew, scrap | Run over (~10% target win rate) | Easy/Normal/Hard |
| Monster Train | Rings with 2 routes | Card/upgrade/merchant | Deck, upgrades, **Pyre HP** | Run over | Covenant 1–25 |
| Against the Storm | Cycle of several 1–2 h settlements | Cornerstone: 1 of 4, reroll or decline | Cycle-wide effects, meta | Failed town only uses up cycle time | Prestige 1–20 |
| Bad North | Island map + advancing mist | Item/commander islands | Commanders, upgrades, items | Commander permadeath; checkpoints added later | Normal/Hard/Very Hard |
| Mechabellum | 1 match, many rounds | 1 of 4, **same offer to both players** | Placed units stay locked in place | Lose round → lose HP | (PvP) |
| Warpips | Operation map | Bonus units on win | Unit unlocks | Retry; enemy grows as you clear | Map pressure |
| Rogue Command | 9 skirmishes, ~5 min each | Reward map of unlocks | Arsenal unlocks | Run over | Ascension + new final bosses |
| Into the Breach | 2–4 islands, then finale | Clear stated rewards | Mechs, pilots, **Power Grid** | Grid at 0 ends run | Easy/Normal/Hard |
| Thronefall | Single levels | Up to 5 perks; any number of mutators | — (meta unlocks) | Retry level | Opt-in mutators raise score/XP |

---

## 1. Slay the Spire: the reference node map
**Mechanic.** Branching map whose node types are visible: normal fight, elite (guaranteed relic), rest site (heal 30% **or** upgrade a card), merchant (buy, or **remove a card** for 75 gold, +25 each later removal), unknown event, treasure, boss (rare card plus a choice of boss relic). The node before each boss is always a rest site ([dood.gg map guide](https://www.dood.gg/en/slay-the-spire/guides/map-guide)). A card reward is **1 of 3 or skip** ([wiki](https://slay-the-spire.fandom.com/wiki/Card_Rewards)). Rarity depends on the node: rare is 3% from normal fights, 10% from elites, 100% from bosses. A hidden pity offset starts at −5%, rises 1% per common rolled, and resets when a rare appears (same source).
**Why it works.** Players choose risk on the map (elite = relic, at an HP cost) and choose identity at the reward screen. Skip and removal let them refine a build as well as grow it. Mega Crit tuned with telemetry. The two metrics they leaned on most were **pick rate** ("too low and it's basically not a card in our game") and **appearance in winning decks** ("too high and you know it's overpowered") ([Game Developer](https://www.gamedeveloper.com/design/how-i-slay-the-spire-i-s-devs-use-data-to-balance-their-roguelike-deck-builder)). Data showed the boss Awakened One was over-punishing Power-heavy decks, so they slowed its scaling and raised its base damage to compensate (same source).
**Ascension.** 20 cumulative levels, unlocked one at a time by winning. Losing never takes a level away. The modifiers fall into three groups: enemies hit harder or have more HP (A2–4, 7–9, 17–19); the player's resources shrink (less healing after bosses, start damaged, a starting curse, one fewer potion slot, fewer upgraded cards, pricier shops); and structure changes (60% more elites at A1, **two final bosses at A20**) ([wiki](https://slay-the-spire.fandom.com/wiki/Ascension)).
**Post-Frontier fit.** Very high. The node vocabulary carries over almost directly. "Rest = heal 30% or upgrade" maps onto the lives pool: **Repair node = restore 1 life OR upgrade a doctrine**, which turns the lives pool into something players spend. Use pick rate and win rate per barracks type and per draft card. Frontline's dominance would show up immediately.
**Pitfall.** StS gets about 20+ picks per run, and we get about 6. With 1-of-3 commons, most of our picks would feel like nothing. Rare/boss-tier picks need to be the norm here, not the exception.

## 2. FTL: pursuit as a run clock
**Mechanic.** Each jump uses fuel and moves the rebel fleet forward. Beacons the fleet captures become hostile elite fights that pay almost nothing. Nebula beacons slow the advance; a mercenary or the Distraction Buoys augment delays it; some events speed it up ([FTL wiki](https://ftl.fandom.com/wiki/Rebel_Fleet)). Hull and crew carry between fights. Designers tuned for about a 10% win rate and made restarting nearly frictionless, modelled on Super Meat Boy. "The permanence of a gameplay mistake was a critical element" ([Wikipedia](https://en.wikipedia.org/wiki/FTL:_Faster_Than_Light)).
**Why it works.** The fleet prices greed. Every extra beacon means more loot and more danger. Damage that carries over makes even won fights matter.
**Post-Frontier fit.** High. The lives pool alone gives no pressure between battles. A **JEV "rollout" clock** that advances per node, plus extra on a loss, gives "explore vs. push to the boss" stakes without micro. Diegetic hook: The Machine ships patches on schedule.
**Pitfall.** A 10% win target suits solo masochists. A friends group on a 60–90 min evening needs a much higher base win rate, with Ascension supplying the hard mode `[INFERENCE]`.

## 3. Monster Train: persistent run HP and identity from minute 0
**Mechanic.** You choose two clans (primary plus allied) at the start of the run. The **Pyre** is run-wide HP: 80 at start, +30 per major boss beaten, and "damage taken by the Pyre carries over between fights" ([wiki](https://monster-train.fandom.com/wiki/Pyre)). Covenant 1–25 mixes three lever types: enemy stats; **junk added to your deck** (Deadweight, extra starter cards); and economy or structure (Pyre starts 20 damaged, merchant rerolls/purges/goods cost 20% more, −1 capacity on a floor). Win streaks are tracked by bracket ("Cups") ([wiki](https://monster-train.fandom.com/wiki/Covenant_Ranks)).
**Why it works.** Pairing two clans gives an immediate, readable identity. Carried Pyre damage makes a sloppy win still cost something.
**Post-Frontier fit.** High. (a) Pick a **commander doctrine pair** before the run (e.g., "Siege Optics + Field Repairs" lineage), so the draft has a direction from battle 1. (b) Keep lives as the run-end condition, but add a carried **HQ integrity / "uptime"** meter: battles lost or barely won chip it, Repair nodes restore it. That gives a gradient between "clean win" and "lost a life". (c) Junk-in-deck maps to "JEV injects a bad protocol into your build" at high Ascension.
**Pitfall.** Two run-wide health bars (lives and integrity) can confuse players. Pick one, or make integrity the thing that produces lives `[INFERENCE]`.

## 4. Against the Storm: dual meters that force a match to end; cornerstones
**Mechanic.** A settlement is won at a **Reputation** target. A Queen's **Impatience** meter rises over time and when villagers leave or die, and each reputation point removes one impatience. So a town "that just won't take" ends on its own ([Eurogamer](https://www.eurogamer.net/against-the-storm-review-a-perfectly-chaotic-city-builder)). Cornerstones are offered **4 at a time, with reroll or decline for gold** (same source, screenshot caption). Prestige 13 cuts that to 2 choices, Prestige 12 cuts blueprint choices from 4 to 2, and Prestige 4 makes rerolls costlier ([wiki](https://against-the-storm.fandom.com/wiki/Difficulty)). The cycle bar fills with every settlement **completed or abandoned**, by years spent there. When it's full, the Blightstorm wipes the map ([wiki](https://against-the-storm.fandom.com/wiki/Blightstorm_Cycle)). Settlements farther from the capital have higher minimum difficulty and fewer embark resources. Biome and seasonal modifiers are shown before you build, so "I've never played a session where I thought … RNG had messed me over" (Eurogamer).
**Why it works.** The two meters guarantee each session closes, win or lose. Telegraphing modifiers turns randomness into planning. A failed town costs **time**, not the whole run.
**Post-Frontier fit.** Very high for the 8–12 min target. Add a per-battle **Objective vs. JEV Escalation** dual track. Capturing and holding anchors fills Objective; time and lost regions fill Escalation. Destroying the HQ is an early win, and the Escalation cap ends the battle in JEV's favour. Use **"fewer draft options" as an Ascension lever** (4 → 3 → 2), as ATS does.
**Pitfall.** ATS settlements run 1–2 h. Copy the meter logic, not the length.

## 5. Bad North: minimal RTS, permadeath on commanders, and its later softening
**Mechanic.** Up to 4 squads per island. Soldiers replenish, but **if a commander dies you lose that squad and its upgrades forever** ([Nintendo UK interview](https://www.nintendo.com/en-gb/News/2018/April/Interview-Taking-on-hordes-of-invading-Vikings-in-Bad-North-1368315.html)). Gold equals houses surviving (1–3 coins each), so **partial success means a partial reward**. Mist overtakes one line of islands per turn and locks them ([wiki](https://bad-north.fandom.com/wiki/Bad_North's_Basic_Rules)). Critics called the failure cost "extremely harsh … at odds with the leisurely pace" ([PC Gamer](https://www.pcgamer.com/bad-north-review)). The free Jotunn patch then added: **checkpoint islands**, an optional replayable-levels mode, enemy types shown before you commit, unlockable starting items/traits (Codex meta-progression), a resurrect item, and a switch from **per-commander gold to a shared Gold Bank**, with total gold "more controlled, less RNG" ([patch 2.00](https://www.badnorth.com/news/patch/2-00-jotunn-edition)).
**Why it works.** Without micro, the strategy lives in deployment, matchups and reading chokepoints. Rewards scaled by houses saved make every second of defence count.
**Post-Frontier fit.** Very high: closest in feel (few command verbs, auto-fighting squads). Scale rewards by **regions/anchors held at battle end**, not just win/lose. Show JEV's doctrine/unit mix on the node before players pick it. Note that Bad North itself **moved from private to pooled gold**. Our "even split + private wallets + free gifting" is a middle ground, so watch whether players simply pool everything.
**Pitfall.** Permanent loss of an invested asset from a single mistake was the most-criticized part, and the developer patched it.

## 6. Mechabellum: no micro, but consequence through locked placement
**Mechanic.** Rounds of buy/place, then an auto-battle. Units are movable only in the round they're bought; after that they're "stuck where you set them down … for the rest of the match" ([PC Gamer](https://www.pcgamer.com/i-cant-stop-playing-this-autobattling-strategy-masterpiece)). Between rounds each player picks from **4 random cards, and both players see the same set**. That keeps it fair and lets you read your opponent's options (same source). Losing a round costs HP ([Wikipedia](https://en.wikipedia.org/wiki/Mechabellum)). It has a co-op mode.
**Why it works.** Locking makes it "about fixing your mistakes … not flipping the table". Mixed-arms comps beat single-minded cheese (PC Gamer).
**Post-Frontier fit.** High. Our barracks lock-on-first-Start is the same rule, so lean into it. **Shared offer pools in co-op** (one table of options, each player takes one, in rotation) creates table talk and visible role division `[INFERENCE from the shared-set rule]`.
**Pitfall.** Locks only feel fair when information is good. Mechabellum still shows the battle, and you need readable JEV intent too.

## 7. Warpips: no-micro tug-of-war with a pressure map
**Mechanic.** "Focus on the big picture; no complex micro". Rounds last **10–20 min**, battles are randomly generated ([Steam](https://store.steampowered.com/app/1291010/Warpips)). On the campaign map, clearing a level awards bonus units **but makes the enemy army stronger**. In battle, kill XP is spent on unit levels, cash, or unit cap. A shared morale-style bar triggers cover / accuracy / rush orders ([TheXboxHub](https://www.thexboxhub.com/warpips-review)).
**Why it works.** The "grow vs. let the enemy grow" decision lives on the map. In-battle choices are economic, not positional.
**Post-Frontier fit.** High. It is evidence that no-micro battles at our length can hold strategy. The XP → {upgrade, cash, cap} triad is close to our Power spending. Note the review's chief complaint: units walking "straight into a minefield" with no waypoint. **Our region-goal commands must be visibly smart**, or players blame the AI, not themselves.
**Pitfall.** Same as above: when players can't steer around hazards, losses feel unfair.

## 8. Rogue Command (2024 EA → 1.0): the closest roguelite RTS
**Mechanic.** Runs of **9 increasingly hard skirmishes vs. AI**. After each, players choose on a **reward map** that unlocks units, upgrades, consumable abilities and bonuses. Battles average **~5 min**, with "a couple more" for management. Each run won adds an **Ascension** level with stronger enemies **and new final bosses** ([review](https://lifeisagamemagazine.substack.com/p/rogue-command-review-a-clever-rts)). Leaders ("Engineers") change how a run starts; maps add procedural ruins and weather ([PC Gamer](https://www.pcgamer.com/games/strategy/heres-an-indie-that-takes-classic-rts-and-runs-it-into-the-modern-roguelike)).
**Why it works.** "Instead of learning your build orders by heart … every time you start a run you are faced with the challenge of coming up with a new build" (developer, PC Gamer).
**Post-Frontier fit.** Very high: structurally our game minus co-op. Its criticisms are our warnings. (1) "Luck can play a bigger role than skill … you'll sometimes field a weak army that gets quickly overwhelmed in the middle stages". (2) "You're still trying to accomplish the exact same objectives. You're either hunting a specific unit or destroying a building", which reads as repetitive after 10–20 h (review).
**Pitfall.** Same objective in every battle. We currently have one win condition (destroy HQ).

## 9. Into the Breach: run HP, short battles, and a cut-down strategy layer (Subset GDC 2019)
**Mechanic and lessons.** Battles have a **turn limit**: survive N turns rather than kill everything. "Micro-battles were fun", and it enabled non-lethal weapon designs. The fail state is the **Power Grid**, a run-wide HP that buildings feed. The first, larger strategy layer failed because "we ignored the constraints imposed by the combat". The fix: "Cut everything. Have clear rewards." Other choices: only three resources, **difficulty shown by resource count**, **dynamic length of 2–4 islands** before the finale, and "Easy can be fun!" ([postmortem slides](https://media.gdcvault.com/gdc2019/presentations/Into%20the%20Breach%20Postmortem%20Final.pdf)).
**Post-Frontier fit.** Very high. (a) A time-capped "hold for N minutes" objective is the cleanest way to enforce 8–12 min. (b) Let groups choose how many nodes to clear before the boss, to fit a 60 vs. 90 min evening. (c) Draft rewards must come from what the battle actually lets players express: region goals, barracks types, Power. So no "+5% attack speed on a unit you can't target".
**Pitfall.** A strategy layer designed separately from combat (Subset's own failed first version).

## 10. Thronefall: subtract choices, layer complexity, forgiving with incentive
**Mechanic.** Players choose only **when** to build, never where or what: predefined slots, a custom tech tree per level. "If you want to simplify you need to subtract". Buildings **respawn free every morning, but economic buildings that just respawned pay no gold**: forgiving, yet you still want to protect them. Complexity grows "at the end of the game" so onboarding stays clean (Tyroller and Schnepf, [Game Developer](https://www.gamedeveloper.com/design/mastering-minimalism-and-layering-complexity-with-strategy-game-thronefall)). Before a level: 1 weapon, **up to 5 perks**, and **any number of mutators**, which raise difficulty but grant more points/XP ([perks](https://throne-fall.github.io/game-content/perks), [mutators](https://throne-fall.github.io/game-content/mutators)).
**Post-Frontier fit.** High. Our anchor and deposit layout already plays the role of predefined slots. The respawn-but-no-income rule is a template for barracks/extractors lost mid-battle. **À-la-carte mutators** suit mixed-skill friend groups better than a single linear ladder.
**Pitfall.** Expectation management: marketed as "strategy", players expect build-anywhere freedom (Tyroller, same source).

## Shorter notes
- **Kingdom Two Crowns.** Roguelike structure "automatically scale[s] the difficulty … from 'too easy' to 'too difficult' … there is thus always a part with interesting difficulty in between". After losing a crown, a "decay" design lets players **keep a majority of what they built**, which was "a bigger motivation to continue playing" ([Game Developer](https://www.gamedeveloper.com/design/-i-kingdom-two-crowns-i-and-the-practical-intersection-of-pixel-art-and-roguelike-design)). Co-op monarchs can physically drop coins for each other ([wiki](https://kingdomthegame.fandom.com/wiki/Coin)): precedent for our free gifting.
- **Rogue Tower.** Upgrades come as random cards during a run; between runs, XP unlocks towers and adds **permanent stat upgrades** ([Steam](https://store.steampowered.com/app/1843760/Rogue_Tower)). Our "unlocks only" rule is deliberately stricter. Good: it keeps Ascension honest.
- **Age of Darkness: Final Stand.** Advertises "Malices, Blessings & Hardships" roguelite modifiers ([Steam](https://store.steampowered.com/app/1426450/Age_of_Darkness_Final_Stand/)). A 50-hour review found that hordes of more of the same enemy, without new mechanics, make "every horde feel the same" ([review](https://strategyandwargaming.com/2025/03/08/age-of-darkness-final-stand-review-feels-like-wasted-potential)). Escalation needs new behaviour, not just bigger numbers.
- **Diplomacy is Not an Option.** Not a roguelite. Its useful lesson is negative: no indication of the path attackers will take, so defences get bypassed ("a simple arrow indicator … would probably have sufficed"), plus steep difficulty spikes ([Finger Guns](https://fingerguns.net/reviews/2024/10/14/diplomacy-is-not-an-option-review-pc-the-negotiations-were-short)).
- **StarCraft II Co-op.** Weekly **Mutations** and player-built **Brutal+** use mutator "cost brackets" as a co-op difficulty ladder ([Blizzard 3.3](https://news.blizzard.com/en-us/article/20112699/patch-3-3-new-co-op-content-and-features), [Brutal+ brackets](https://starcraft2coop.com/resources/brutal)). This is the co-op precedent for points-budgeted mutators.

---

## Answers to the brief

**Q1 — How does the map + draft create build identity? Choices per node, rarity, removal/upgrade?**
- Identity is set early and refined later: Monster Train's clan pair at run start, Rogue Command's Engineer, ATS's embark caravan. Drafts then deepen that identity rather than create it.
- Offers are 1-of-3 + skip (StS), 1-of-4 with reroll/decline (ATS), or 1-of-4 shared with the opponent (Mechabellum). Rarity rises with node risk (StS rare 3% / 10% / 100%) and has a pity timer. Refinement comes through removal with escalating cost (StS 75 + 25n) and rest-or-upgrade tension.
- **Proposal** `[INFERENCE]`:
  - Per player, after each battle: 1 of 3 personal "Doctrines" (barracks-type modifiers, command upgrades, Workshop extensions) + skip.
  - At elites and bosses: one **team Protocol** (relic) chosen by vote.
  - Repair node: +1 life OR upgrade a Doctrine.
  - Depot node: buy, or "deprecate" (remove) a Doctrine at an escalating cost.
  - Since there are only ~6 picks, make roughly a third of offers build-defining (ATS cornerstone scale), not +5% stat cards.
- Draft content should target the Frontline imbalance. Doctrines that make Ranged or Siege lines viable ("Siege Optics" style) give runs different shapes.

**Q2 — Short yet strategic battles; what carries over?**
- Battles get short by subtracting choices: Thronefall slots, Mechabellum locks, Bad North's 4 squads. Endings get forced by a timer or dual meters (ITB turn limit, ATS reputation vs. impatience). Players see what's coming before committing (Bad North enemy preview, ATS modifiers).
- What carries over varies:
  - Assets: deck/relics (StS), ship plus hull (FTL), commanders plus upgrades (Bad North).
  - Run HP: Pyre (MT), Power Grid (ITB).
  - Rewards scaled by performance: Bad North's houses-to-gold.
- **Proposal**:
  - Carry Doctrines/Protocols, a commander's barracks-type mastery, a small Power bank, and an HQ-integrity or lives state.
  - Do **not** carry unit counts: auto-replacing barracks already erase them, and carrying them causes snowballing.
  - End battles with an Objective vs. JEV-Escalation dual meter, so a stalemate still ends within 12 min.

**Q3 — Bosses and Ascension/mutators.**
- Bosses are visible from act start (StS A20 text: "the first boss that can be seen from the map"). They often test one strategy (Awakened One vs. Power-heavy decks) and come with their own reward (boss relic).
- Ascension/Covenant/Prestige are cumulative small steps. They mix enemy strength, player starvation (healing, slots, prices, fewer choices) and structural twists (more elites, double boss, junk cards). Thronefall and SC2 use opt-in, point-valued mutators instead.
- **Proposal**: JEV currently has no knobs. Build Ascension from rules, not AI smartness: JEV income ×, extra JEV starting regions, fewer draft options, Repair nodes heal less, double final boss.
- Bosses could be "JEV major versions", each with one rule that **counters the dominant strategy** (e.g., a boss that "deprecates" whichever unit type is most fielded). Tune with telemetry, as StS did with Awakened One.

**Q4 — Lost battles / partial failure.**
- The range runs from harsh to soft:
  - Run ends: FTL, StS, MT.
  - Asset permadeath: Bad North, later softened with checkpoints.
  - Failure costs time on the run clock: ATS.
  - Run HP drained: MT Pyre, ITB grid.
  - Partial reward proportional to performance: Bad North gold.
  - Keep most of what you built: Kingdom Two Crowns' decay design.
- **Proposal**: losing costs 1 life **and** advances JEV's "rollout" clock (FTL fleet / ATS cycle). The group keeps everything drafted and still gets a reduced reward scaled by regions held. Add an early **"Fall Back / concede"** option that saves some reward, analogous to FTL jumping away and Bad North's Flee. The last life should be lost only at the boss or by repeated losses, never by one early mistake.

## Top 7 transferable lessons (ranked)
1. **Force every battle to end on a clock, using dual meters or a time-capped objective** (ATS reputation/impatience, ITB turn limit). It's the only reliable way to hold 8–12 min with an AI that can stall, and it opens up alternative win conditions.
2. **Show what's coming before players commit** (StS node icons/boss, Bad North enemy preview, ATS modifiers, ITB "difficulty shown by resource count"). Without micro, information is the player's main lever, and it keeps RNG from feeling like the game "messed me over".
3. **Make draft rewards express things the battle actually lets players do** (Subset: the strategy layer failed when it "ignored the constraints imposed by the combat"). Offer region-goal behaviours, barracks-type doctrines and Power economics, not stats on units nobody controls.
4. **Few picks per run → big picks, plus refinement**: 1-of-3 + skip, rarity tied to node risk with a pity timer, escalating-cost removal, and rest-or-upgrade tension (StS, ATS cornerstones).
5. **Grade failure instead of making it binary**: reward by regions held (Bad North), carried run HP (MT, ITB), a time/escalation cost (FTL, ATS), and keep what you built (Kingdom). Lives then act as a floor, not the only feedback.
6. **Build difficulty from rule-based, cumulative steps, plus opt-in mutators for mixed-skill co-op** (StS/MT/ATS ladders; Thronefall/SC2 point-valued mutators). Reducing the number of choices is an elegant lever (ATS P12/P13).
7. **Instrument picks and wins from day one** (StS pick rate and win rate). Frontline dominance and dead draft cards will show up in a week of playtests instead of a month of arguing.

## Traps to avoid
- **One objective type forever**: Rogue Command's main criticism after 10–20 h. Destroy-HQ alone won't last.
- **Luck-made dead runs mid-run** (Rogue Command). Guarantee a pity rare/elite reward and allow at least one reroll per act.
- **One mistake wipes invested assets** (Bad North's launch criticism, later patched). Don't let a single loss delete a commander's whole build.
- **Escalation through numbers only** (AoD: "every horde feels the same"). Each act should add a new JEV behaviour or unit rule.
- **Opaque AI pathing/intent** (DiNaO: no attack-path indicator; Warpips: units walking into minefields). Without micro, an unreadable AI feels unfair.
- **Snowballing carry-over** of unit counts or Power. Barracks auto-replace already resets armies, and carry-over would undo the per-battle reset.
- **Permanent stat meta-progression** (Rogue Tower model). It conflicts with the designer's unlocks-only rule and blurs Ascension.
- **A strategy layer designed separately from combat** (Subset's own failed first version).

## (a) What keeps players replaying
- A new build each run ("instead of learning your build orders by heart", Rogue Command), made of identity at the start plus drafted synergies.
- Visible mastery ladders: Ascension/Covenant/Prestige, win streaks by bracket (MT Cups), Codex-style discovery (Bad North).
- Learning from failure with frictionless restarts (FTL modelled restarts on Super Meat Boy). Complexity layered over many runs (Thronefall, ATS).
- Novel content per difficulty step: Rogue Command's new final bosses per Ascension. This is weak if objectives stay the same.

## (b) What these games reward
- **Risk pricing on the map**: elites vs. safety (StS), loot vs. fleet (FTL), grow vs. let the enemy grow (Warpips).
- **Composition and matchup reading over execution**: Mechabellum's combined arms beating cheese; Bad North counters and chokepoints.
- **Committing under a lock and then adapting** (Mechabellum placement; our barracks lock).
- **Telegraph-driven planning** (ATS: knowing hostility thresholds lets you prioritise food early).
- **Discipline in drafting**: skipping bad cards and thinning the deck (StS removal priority).

## (c) Open questions for Post-Frontier
1. What is the alternative win state per node: hold N anchors for T min, sabotage JEV extractors, escort? Does Destroy-HQ stay as the early-win path?
2. Is there one run-wide health bar (lives) or two (lives + HQ integrity)? If two, how do they interact?
3. Are draft offers per player, shared (Mechabellum-style table), or both? How does a 5-player group choose a path without stalling: vote, rotating "navigator", or host?
4. What exactly is a "card" here: a barracks Doctrine, a new command verb, a Workshop spec, a team Protocol? Which of these can a no-micro player actually feel within 10 min?
5. How does Ascension work without JEV difficulty knobs? Rule modifiers only, or expose JEV parameters (income, starting regions, re-plan thresholds)?
6. What does a loss cost beyond a life: JEV clock advance, reward loss, a forced retry of the node? Can players concede early to save something?
7. With a 60–90 min target, how many nodes are there, and is length dynamic (ITB's 2–4 islands) for shorter evenings?
8. How do we telegraph JEV intent and boss rules legibly enough that locks and region-goal orders feel fair?
9. Do private wallets survive contact with players, or will groups pool everything as Bad North eventually forced by design?
10. Which telemetry ships in the first playtest build (pick rate, win rate, battle length, objective-meter outcomes)?
