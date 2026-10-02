# Replayability & Reward Design: Research for Post-Frontier (CoopRTS)

Scope: what makes players start another run, what to reward, unlock pacing without stat power, snowball control, and co-op motivation. Format per source: **Mechanic → Why it works (evidence) → Applicability to Post-Frontier (PF) → Pitfall**. Evidence tags: [DEV] developer talk/interview, [WIKI] official/community wiki data, [STUDY] peer-reviewed, [OPINION] designer blog or expert commentary, [INFERENCE] my extrapolation.

PF constraints I designed against: no unit control, 4 region-goal commands per barracks, 8–12 min battles, 60–90 min node-map run, lives pool ≈3, team income split evenly with private wallets, unlocks only (no permanent stat power), tuned for solo + 2–3.

---

## Part 1: Theory frames

### 1.1 Sid Meier: "a game is a series of interesting decisions" [DEV]
- **Mechanic:** Run every choice through the test. A choice is *not* interesting if players always pick the same option of three, or if picking at random works as well. Interesting choices involve trade-offs, depend on the situation, last over time, let players express a style, give enough information, and get immediate feedback. Meier says roughly a third of what Firaxis tries gets cut ([GDC 2012 write-up](https://www.gamedeveloper.com/design/gdc-2012-sid-meier-on-how-to-see-games-as-sets-of-interesting-decisions)).
- **Why:** He frames combining "pieces of other games" as a weak design method. The question to ask is which decisions the player is actually making. He also warns that the "worst thing you can do is just move on" after a decision with no feedback.
- **PF:** PF has few verbs: barracks type lock, 4 orders, workshop specialization, node path, draft pick. Each of these has to pass Meier's test. Today Frontline dominates, so the barracks lock fails his "always pick the first" test. Every order change needs loud feedback (VO, map arrows) because players have no unit-level feedback loop.
- **Pitfall:** Information overload in real time. Meier: complicated decisions arriving one after another make the player "feel out of control".

### 1.2 MDA (Hunicke, LeBlanc, Zubek 2004) [STUDY/framework]
- **Mechanic:** Design from aesthetics (Challenge, Fellowship, Discovery, Expression, Narrative…) back to mechanics. The paper's worked example is Monopoly's runaway feedback loop: "as the gap widens, only a few… players [are] really invested. Dramatic tension and agency are lost." It offers two fixes: subsidies or taxes, *or* time pressure through "depleting resources over time" ([MDA paper](https://users.cs.northwestern.edu/~hunicke/pubs/MDA.pdf)).
- **PF:** The target aesthetics are probably **Fellowship + Challenge + Discovery + (comic) Narrative**. PF already has a depletion clock. Normal deposits last 2400/4 = 600 s (10 min) and rich ones 3000/6 = 500 s (≈8.3 min), so deposits run dry inside one battle [INFERENCE from the source-verified numbers]. That is MDA's second fix already built in. Keep it deliberate.
- **Pitfall:** If tax or subsidy maths needs "complex calculations", players lose track of standings (MDA's own warning).

### 1.3 Self-Determination Theory (Ryan, Rigby, Przybylski 2006) [STUDY]
- **Mechanic:** Feeling competent and autonomous in play predicts enjoyment and well-being. In an online multiplayer sample (Study 4), "autonomy, competence, and relatedness independently predict enjoyment and future game play." Autonomy grows when "rewards are structured so as to provide feedback rather than to control the player's behavior" ([paper](http://selfdeterminationtheory.org/SDT/documents/2006_RyanRigbyPrzybylski_MandE.pdf)).
- **PF:** (1) Competence: players must be able to *read why* a battle went the way it did, so add a post-battle "incident report". (2) Autonomy: offer multiple viable strategies and player-chosen difficulty (see Hades Pact). Unlocks should feel like *information and options*, not a leash. (3) Relatedness: an independent predictor of continued play, which is co-op's main advantage.
- **Pitfall:** Extrinsic grind rewards (currency → stat upgrades) are the "controlling" kind of reward that SDT links to lower intrinsic motivation.

### 1.4 Co-op motivation: cooperation vs interdependence (Depping & Mandryk, CHI PLAY 2017) [STUDY]
- **Mechanic:** The study compared cooperation vs competition and interdependence vs independence in otherwise identical games. Cooperation and interdependence are "two distinct factors that both can be used to improve play experience and increase social bonds" ([ACM DOI](https://dl.acm.org/doi/10.1145/3116595.3116639); [summary](https://www.researchgate.net/publication/320313480_Cooperation_and_Interdependence_How_Multiplayer_Games_Increase_Social_Closeness)).
- **PF:** A shared goal (the enemy HQ) is cooperation. Interdependence needs **mechanical reliance**: one commander's Siege only works if another holds the line, gifting matters, and specializations complement each other. The even income split gives cooperation but *removes* some interdependence. Make gifting and role complementarity carry it.
- **Pitfall:** Players who share a goal but play independently are "parallel solo". That is the default failure mode of co-op RTS.

### 1.5 Quarterbacking ("alpha player") in co-op [OPINION/academic review]
- **Mechanic:** In pure co-op with shared information, one expert dictates everyone's moves. The known fixes are real-time play, restricted information, execution complexity, *ownership* (personal roles and avatars), and asymmetric objectives ([Analog Game Studies review](https://analoggamestudies.org/2021/06/roleplaying-as-a-solution-to-the-quarterbacking-problem-of-cooperative-and-educational-games)). Koster recommends time pressure or a shorter information horizon (cited there).
- **PF:** Real-time battles naturally resist quarterbacking. The **between-battle draft is turn-based**, which makes it the high-risk spot. Use simultaneous or private picks, a draft timer, and commander-owned cards.
- **Pitfall:** Fully restricting communication kills the social fun. Keep table talk open and make ownership do the work.

### 1.6 Snowball vs comeback
- **Riot (LoL) [DEV]:** "Comebacks have to be earned." A lead is "an advantage, not a victory." Comeback mechanics "allow winning teams to enjoy feeling more powerful" because snowballing does not have to be watered down, and they "incentivize winning teams to use their leads wisely." Comebacks should come from the leader's misplays and "never feel like the losing team was granted a free win." Fairness "in practice matters more than in theory", so visualize it ([Riot dev blog](https://www.leagueoflegends.com/en-us/news/dev/quick-gameplay-thoughts-2-25-comeback-mechanics)).
- **Casteel, "homeostatic design" [OPINION]:** RTS snowball comes from sunk costs and from economies that are concentrated in one place. Company of Heroes resists snowball because territory and army are only "partially independent", there are many points of interest, and assets are not sunk. "Having a large number of points of interest decreases the pain of loss of each" and "losses are easier to swallow if a player feels like they understand why they lost" ([article](https://www.gamedeveloper.com/design/the-balance-of-power-progression-and-equilibrium-in-real-time-strategy-games)).
- **PF:** PF already has many POIs (13 anchors), captures that never lock, and auto-retreat at <40%, which preserves the army. All three are homeostatic. The snowball risks are the **barracks type lock** (a sunk cost) and **builder-only extractor income** (it compounds per player). See Part 3 for controls.

---

## Part 2: Games

### 2.1 Slay the Spire
- **Mechanic A, intents:** Each enemy shows its next action (attack value, block, buff/debuff, unknown) ([wiki](https://slay-the-spire.fandom.com/wiki/Intent)). The team scrapped an earlier "next move" system for intents after playtester feedback, partly to make the game readable on stream ([Ars Technica War Stories via GoNintendo](https://gonintendo.com/archives/311000-how-slay-the-spire-s-original-interface-almost-killed-the-game)).
- **Why:** Intents turn each turn into a solvable puzzle about allocating resources against a known threat, which is Meier's informed choice.
- **PF:** JEV is a deterministic planner that re-plans every 2 s, so it can **publish its plan**: "JEV intends: assault Region 7 in ~40 s with ~12 units", "JEV is fortifying Region 3". Region-goal commands are exactly the right granularity for answering intents. This is PF's best decision engine.
- **Pitfall:** If JEV re-plans every 2 s, the displayed intent will flicker. Commit intents for a window (e.g. 20–30 s), or show "likely" vs "committed".
- **Mechanic B, Ascension:** 20 cumulative levels, unlocked one at a time per character. They make enemies deadlier, add elites, give less healing and fewer potion slots, make events worse, and end with a double boss ([wiki](https://slaythespire.wiki.gg/wiki/Ascension)). Giovannetti called it "Player Skill Stratification", alongside Custom and Daily modes ([GDC 2019 slides](https://media.gdcvault.com/gdc2019/presentations/Giovannetti_Anthony_SlayTheSpire.pdf)).
- **Mechanic C, unlock pacing:** Cumulative run score per character unlocks content at 300/750/1000/1500/2000 points: card bundles, relics, then the next character ([guide](https://slaythespire.info/en/how-to-unlock-characters-cards-and-relics-spoiler-warning)). Score rewards elites, ?-nodes and speed, so it also teaches risky routing.
- **Balance goal:** "Every card should have a place! (also avoid anything too warping)" ([slides](https://media.gdcvault.com/gdc2019/presentations/Giovannetti_Anthony_SlayTheSpire.pdf)). Giovannetti: building the same deck every time "is actually pretty boring". A relic like Dead Branch means "all of a sudden your run is likely all about exhausting" ([PC Gamer](https://www.pcgamer.com/slay-the-spire-designer-discusses-new-characters-and-card-game-inspirations)).
- **PF:** Build relic-like "Directives" that bend a whole run (e.g. "Siege fee waived, Frontline costs +50%"). Unlock by cumulative score per commander. Give each commander its own ladder.
- **Pitfall:** Ascension mostly adds numbers. Its late levels (17–19) change enemy *movesets*, which is more interesting. PF ladder steps should change JEV *behaviour*, not just HP.

### 2.2 Hades
- **Mechanic A, failure as progress:** Kasavin: "Our big focus from the start… was how to take the sting of failure and reduce that as much as possible… Even the narrative perspective of the game exists in service of that goal." Also: "The part where roguelikes can be brutally difficult is, ironically, directly at odds with the part where they're so replayable", and "If you're playing for five hours before restarting, then you're not experiencing the cool part" ([Inverse](https://www.inverse.com/gaming/hades-god-mode-interview)). The story advances after every death ([Polygon, psychologists' commentary](https://www.polygon.com/psychology-roguelikes-punishment-into-reward)).
- **Mechanic B, Pact of Punishment (Heat):** 15 player-selected conditions with ranks: more enemy damage, more enemies, a timer, fewer boon choices, boss new techniques, and so on. It appears only after the first win. Bounties are tracked *per weapon* with Heat capped at 20, which takes 21 completed runs per weapon to collect them all. Rewards were originally nothing but bragging rights ([wiki](https://hades.fandom.com/wiki/Pact_of_Punishment)).
- **Mechanic C, nudging variety:** Kasavin: "just when you find what feels like a perfect combination… maybe you get nudged into a new set of tools", and "we're not having the same experience over and over, and we're compelled to start new runs" ([Game Developer](https://www.gamedeveloper.com/design/supergiant-s-fourth-outing-i-hades-i-introduces-a-more-mature-organized-dev-process)).
- **PF:** (1) The Machine's polite corporate messages after every battle and life lost ("We noticed you lost. Your feedback matters.") give narrative progress on failure without stat power. This fits the funny-writing pillar exactly. (2) A Pact-style **"Terms of Service"** modifier menu lets the team compose its own difficulty, which is autonomy. Tracking it per commander multiplies ladder content. (3) Rotate drafts so a group can't repeat its comfort build (Hades's "nudge").
- **Pitfall:** Hades softens failure partly with *permanent stat power* (Mirror of Night; God Mode's damage resistance). PF ruled that out, so narrative, humor and sideways unlocks have to carry the whole load. Kasavin himself says God Mode is "not transferable" and difficulty "may need to be proprietary to the game."

### 2.3 Into the Breach (and FTL)
- **Mechanic:** All enemy attacks are shown, with no hit/miss chance and full determinism during the player's turn. Following that constraint led to defensive play, a turn limit as the win-state, and "Killing enemies isn't as fun as manipulating them." A random element (Power Grid) "annoyed players". Puzzle difficulty: "Too hard becomes 'unsolvable'… threshold is a cliff… Easy can be fun!" The strategy layer failed until they "Cut everything, have clear rewards"; difficulty is "shown by resource count" with the "option to plan ahead" ([GDC 2019 postmortem slides](https://media.gdcvault.com/gdc2019/presentations/Into%20the%20Breach%20Postmortem%20Final.pdf)). On FTL they cut features, including multiplayer, to keep "one singular focus" ([FTL postmortem](https://www.gamedeveloper.com/design/designing-without-a-pitch---an-em-ftl-em-postmortem)).
- **PF:** (1) Telegraph JEV and let win conditions reward **manipulating** JEV, e.g. baiting its assault into an entrenched region, rather than out-producing it. (2) Node map: show each node's threat level and reward up front so routing is planned, not gambled. (3) The meta-lesson: derive the strategy layer from the battle layer's constraints. With no micro, run rewards must change *what orders do* or *what barracks can be*, not demand execution skill.
- **Pitfall:** Hidden randomness in a readable system feels like betrayal. Keep battle RNG low or telegraphed.

### 2.4 Balatro
- **Mechanic:** "Interlocking mechanics… and synergies that steer you in interesting directions." Players' metastrategy shifted toward "mitigating risk and having a build that can't be easily countered." Some cards turned out "too good and cannibalise all the adjacent strategies," so balance is tuned by feel through simple number levers ([LocalThunk interview](https://rogueliker.com/balatro-interview/)). There are 8 cumulative stakes, unlocked per deck, and stake wins unlock new decks ([wiki](https://balatrowiki.org/w/Stakes)).
- **PF:** Draft cards should form legible synergy families (e.g. Siege + Optics + "Hold" bonuses). Expect players to optimise for *robustness against JEV's variance*, so reward that. Stakes per commander, with new commanders unlocked by stake wins, is the cleanest unlock chain to copy.
- **Pitfall:** Allowing too much randomness to dominate. LocalThunk concedes "possibly too much" randomness.

### 2.5 Risk of Rain 2
- **Mechanic:** Challenges (achievements) permanently unlock survivors and items, which are then added to future drop pools. Examples: "Complete 30 stages" (Engineer), "Pick up 5 different types of Equipment" (Fuel Cell), and the joke "Fail the Shrine of Chance 3 times in a row" ([wiki](https://riskofrain2.fandom.com/wiki/Challenges)).
- **Why:** Challenges point players toward unexplored behaviours and content, and give failure-adjacent progress.
- **PF:** Use challenge-based unlocks that *teach*: "Win a battle with no Frontline barracks" unlocks a Ranged card; "Gift 1000 Power in one run" unlocks a co-op card; "Lose a life to the Machine's HR Department" unlocks a joke mutator.
- **Pitfall:** Unlocking content *into* a random pool dilutes it, so build odds get worse as content grows. Let players toggle unlocked cards or ban some.

### 2.6 Spelunky and daily runs
- **Mechanic:** Spelunky popularised the shared-seed Daily Run, where everyone gets the same seed ([GameDiscover](https://newsletter.gamediscover.co/p/how-spelunky-got-its-procedural-hook)). Slay the Spire ships Daily and Custom modes as part of its skill stratification ([slides](https://media.gdcvault.com/gdc2019/presentations/Giovannetti_Anthony_SlayTheSpire.pdf)).
- **PF:** A weekly seeded co-op operation (fixed map node graph, mutators and commander pool) gives friend groups a shared appointment and a comparable result. A 60–90 min run is too long for a *daily* habit, so **weekly** fits the session length [INFERENCE].
- **Pitfall:** Leaderboards in co-op with 1–5 players need score normalisation by party size.

### 2.7 Left 4 Dead: replayable co-op [DEV]
- **Mechanics:** "Treat entire Survivor team as 'the player'." Abandoning the team means death. Enemies are designed to pin players so teammates get to "be the hero". "Limited resources… builds group solidarity." "Dramatic Anticipation: if an event is exciting, it will be more so if it broadcasts its impending arrival." "Structured Unpredictability: Low probability + High drama = Memorable… resist the temptation" to show everything every time. The AI Director estimates intensity and alternates build-up, peak and relax phases. Scripted "when/where" encounters "kill replayability… [and] cooperation" ([GDC 2009 slides](https://steamcdn-a.akamaihd.net/apps/valve/2009/GDC2009_ReplayableCooperativeGameDesign_Left4Dead.pdf)).
- **PF:** (1) Add a "rescue" moment: a commander whose HQ-adjacent region collapses can be bailed out by allies' Fall Back or Hold orders plus gifts. (2) Telegraph big JEV moves with escalating warnings (dramatic anticipation, which is also intent). (3) Give JEV a peak/relax rhythm inside 8–12 min so battles have an arc, not a flat grind. (4) Shuffle boss/elite events without repeats.
- **Pitfall:** An adaptive director that *rubber-bands* violates Riot's "earned comeback" rule. Modulate pacing (when JEV strikes), not reward.

### 2.8 RimWorld: loss as story [DEV]
- **Mechanic:** "Loss is an essential part of a story, not its conclusion… your mechanics *must* include loss and recovery." Apophenia (players reading story into abstract events) is fed by "abstracted feedback" and "long-term relevance" ([GDC 2017 slides](https://media.gdcvault.com/gdc2017/Presentations/Sylvester_Tynan_RimWorld_Contrarian_Ridiculous.pdf)).
- **PF:** A lost battle should *change* the run (a scar, a new grudge node, a Machine memo), not just subtract a life. That turns failure into a story beat friends retell.
- **Pitfall:** Scars that compound make a death spiral. Keep a recovery path.

---

## Part 3: Answers to the brief's questions

### Q1. What makes players start another run? Drivers ranked
1. **A different, readable experience each run.** Kasavin: "the part that's interesting… is that it's different every time"; Hades devs are "compelled to start new runs" because experiences differ; Giovannetti: the same deck every time is "boring". Strongest and most consistent across [DEV] sources.
2. **Felt competence and learning ("I know what to try next").** SDT competence predicts future play [STUDY]. Psychologists frame "one more run" as "How am I going to beat the boss? Does this strategy work?" [OPINION, Polygon]. This requires losses to be legible (Casteel; Meier's paranoid / "Mr. Bubble Boy" player).
3. **Low cost of failure plus progress on failure.** Hades's whole narrative serves reducing failure's sting. Run length matters ("if you're playing for five hours before restarting…").
4. **Relatedness: friends to play with.** An independent predictor of continued play in multiplayer [STUDY]. Co-op also creates shared stories (L4D "memorable" moments).
5. **Visible goals ahead.** Next unlock threshold, next ladder level, challenge list (StS, Balatro, RoR2, Hades bounties).
6. **Comparison and appointment.** Daily/weekly seeds (Spelunky, StS).
- Caution on near-misses: gambling research shows near-misses increase motivation to continue ([Clark et al. 2009, Neuron](https://motivation.site.wesleyan.edu/files/2016/06/Clark-2009-Neuron1.pdf); [Reid 1986](https://link.springer.com/article/10.1007/BF01019932)). Natural "almost won" moments are fine. *Manufactured* near-misses are an ethical trap.

### Q2. What to reward so mastery feels earned (no stat unlocks)
- **Reading JEV:** reward answering telegraphed intents (an entrenched Hold where JEV commits, a counter-assault after JEV overextends). ItB: manipulation > killing.
- **Routing judgement:** risk/reward nodes with known stakes (StS elites and ?-nodes earn score; ItB difficulty shown up front).
- **Build robustness:** Balatro's "build that can't be easily countered"; reward diversified barracks over mono-Frontline.
- **Timing and tempo with few verbs:** when to switch Expand → Assault, when to Fall Back to preserve units (auto-retreat <40%).
- **Team coordination:** gifting, complementary specializations, rescue moments (Depping interdependence; L4D).
- **Self-imposed difficulty:** Ascension/Heat-style ladders give mastery a public measure. Ladder rewards are cosmetic or bragging rights (Hades Skelly statues; Balatro stake stickers).
- Unlocks should add **options**, not power: new commanders, cards and mutators, each viable in some situation ("every card should have a place").

### Q3. Pacing unlocks so a 60–90 min run stays fresh for 20+ runs (~25 h)
Evidence base: StS uses score thresholds per character, then a 20-level ladder per character. Balatro uses 8 stakes per deck, with deck unlocks gated by stake wins. Hades bounties need 21 runs *per weapon*. RoR2 uses challenge-based unlocks. Proposed schedule [INFERENCE from those patterns]:
- **Runs 1–3 (onboarding):** a small curated pool, like StS's simpler Ironclad first. Unlock something every run, win or lose: a card bundle, then commander #2 by the end of run 2 or 3.
- **Runs 4–10:** cumulative score per commander unlocks bundles roughly every 1–2 runs. Challenge unlocks start appearing (RoR2-style, teaching behaviours). The first win unlocks the "Terms of Service" ladder, as Hades shows the Pact only after a win.
- **Runs 10–20+:** ladder levels per commander, 10–15 steps where late steps change JEV behaviour. Mutators and alternate bosses unlock from ladder wins (Balatro decks from stakes). Weekly seeded operation.
- **Multiplier rule:** ladders per commander (StS, Hades, Balatro) multiply content without new assets.
- **Co-op rule:** decide how mixed-progress groups pool unlocks (union? host's?). This is an open question, see (c).

### Q4. Snowball control
**Within an 8–12 min battle**
- Keep what's already homeostatic: many POIs, non-locking capture, auto-retreat, deposit depletion around the 8–10 min mark (MDA time pressure).
- Earned comeback, Riot-style: the leader's *exposure* should grow with its lead (more anchors means a longer front for JEV intents to hit). Never hand out free catch-up income.
- Reduce sunk cost (Casteel): allow a costly barracks re-lock or salvage, so a wrong early lock isn't permanent doom.
- Builder-only extractor income compounds per player. The team income split already flattens it; watch that it isn't a hidden per-player snowball.
- JEV pacing: an L4D-style build-up/peak/relax rhythm so a team that is behind gets breathing windows, but the *strength* of JEV is not reduced.

**Across a run**
- Scale threat by node depth (Balatro's rising blind, StS acts), not by player power, to avoid punishing good play.
- The lives pool is the comeback buffer. Make a lost battle change the run (a RimWorld-style scar, a new route) rather than only costing a life.
- Draft rewards should *diversify* more than they *stack* (Hades nudge; "avoid anything too warping").
- Expect the elite/risk-node route to be the main voluntary snowball. That's acceptable because it is chosen (Meier: risk vs reward) [INFERENCE].

---

## Top 7 transferable lessons (ranked)
1. **Publish JEV's intent.** Commit JEV plans for a short window and display them per region. This turns no-micro play into an informed-decision puzzle (StS intents, ItB, L4D anticipation).
2. **Make every loss legible and narrative.** A post-battle "incident report" plus Machine memos that advance on loss, giving failure progress without stat power (Hades, RimWorld, SDT competence).
3. **Design for interdependence, not just a shared goal.** Complementary roles, meaningful gifting, rescue moments (Depping & Mandryk, L4D).
4. **Per-commander ladders plus player-composed modifiers.** Ascension/Stakes structure with Pact-style "Terms of Service" provides the 20+ run spine (StS, Balatro, Hades).
5. **Unlocks are options that teach.** Score-threshold bundles early, challenge unlocks mid-game, ladder unlocks late. Nothing is strictly better (StS, RoR2, "every card should have a place").
6. **Homeostatic battles, earned comebacks.** Keep many POIs, depletion and retreat. Let leads create exposure. Scale by depth, never rubber-band (Riot, Casteel, MDA).
7. **Every choice must pass Meier's test.** Fix the Frontline dominance before adding content, and cut a third.

## Traps to avoid
- Permanent stat meta-progression, or anything that functions like it (also forbidden by the design).
- Rubber-banding that punishes the leading team or hands out free wins (Riot).
- Hidden randomness in a telegraphed system (ItB Power Grid annoyed players).
- Flickering intents from 2 s re-planning.
- A turn-based draft that one expert quarterbacks.
- Unlocks diluting the draft pool until builds can't come together (no toggles or bans).
- Ladder steps that are only bigger numbers.
- Manufactured near-misses or dark-pattern retention.
- Runs that are too long to fail cheaply. If a lost life costs 30+ min of progress, the "one more run" loop breaks (Kasavin).
- Building a strategy layer that ignores battle-layer constraints (ItB's failed strategy layer).

---

## (a) What keeps players replaying
Variety they can read, then competence ("I know what to try next"), then cheap failure with narrative progress, then friends, then visible next goals (thresholds, ladders, challenges), then shared seeds. Ranking and sources are in Q1.

## (b) What these games reward
Informed planning against telegraphed threats (StS, ItB). Adapting to what the run offers rather than forcing one build (StS, Hades). Risk management and robust builds (Balatro). Chosen risk on the route (StS elites). Self-imposed difficulty (Ascension, Heat, Stakes). In co-op: helping, sharing and rescuing (L4D). Mechanical execution is *not* in the list. Every example rewards decisions, which suits PF's no-micro pillar.

## (c) Open questions for Post-Frontier
1. Can JEV commit to a published intent for 20–30 s without becoming exploitable? What is the "intent unit": region, wave size, ETA?
2. With even income split *and* private wallets, what makes players depend on each other beyond the shared HQ?
3. How do mixed-progress groups share unlocks and ladder level (union, host's, minimum)?
4. What does a lost battle *change* in the run besides −1 life?
5. Is the barracks permanent lock an interesting decision or a sunk-cost trap? Should re-locking cost something?
6. Which battle-level decisions remain once players have learned to answer JEV's intents? Is there a skill ceiling without micro?
7. How do we stop the turn-based draft from being quarterbacked at 3–5 players?
8. What does solo play replace relatedness with: narrative, AI allies, or both?
9. Do ladder steps change JEV's *behaviour* (new plans, targets), or only numbers?
10. How are weekly scores normalised across party sizes 1–5?
