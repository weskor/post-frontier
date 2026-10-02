# Research: Indirect-control / macro-only strategy — lessons for Post-Frontier

Scope: games where the player does not (or need not) micro units. Each entry: **mechanic → why it works (evidence) → applicability → pitfall**. Claims marked [INFERENCE] are my reasoning, not sourced.

---

## 1. Hearts of Iron IV — battle plans executed by AI (closest analogue to our per-barracks orders)

**Mechanic.** Players draw a *frontline* (assign divisions to a border), an *offensive line* (advance to here), a *fallback line*, or a *garrison area*; AI-run divisions execute. Hovering an offensive arrow previews the step-by-step advance. Plans accrue a **planning bonus** while divisions sit prepared; it decays as the plan advances ([Dev Diary 45](https://store.steampowered.com/news/posts?appids=394360&enddate=1457698210)).

**Why it works (designer's own reasoning, DD45):**
- HOI3's AI-run HQs failed: "features that play themselves are generally not a good idea. Giving up control also isn't something most players like to do."
- Rule for the executor: "The system is specifically not allowed to be clever, that is the player's job, so if you tell it to suicide into the maginot line it will say it thinks it's too risky, but do it anyway."
- Player's role is "to draw up high level plans and to watch for opportunities … expect that you will need to improvise and adapt parts as you go."
- Planning bonus turns *waiting* into a decision: attack now, or wait for the bonus to max ([community guide](https://grandstrategy-hub.com/hoi4-battle-plans-frontlines-guide)). Overstacking one front gets a debuff, so players spread across fronts (same guide).

**Applicability.** Our Hold / Expand / Assault / Fall Back are frontline / offensive / fallback / garrison under other names, so the HOI4 lessons carry over almost one-to-one:
- Show the Expand route and the Assault target before committing, as HOI4's arrow preview does.
- Add a **preparation charge**: a barracks that Holds next to its target builds an assault bonus that decays once it moves. "When do we go?" then becomes a team decision with a timer the whole team can see.
- Add **region frontage**: past N squads in one region, effectiveness drops. This pushes players toward multi-front play without adding micro.
- Keep the executor dumb and predictable. Auto-retreat below 40% is fine because it is a *rule*. A clever executor that second-guesses the player would take the decision away from them.

**Pitfall.** HOI4 players learn to avoid the "Activate" button and micro the important attacks themselves (same guide). The automation is trusted for *holding*, not for *winning*. We have no manual override, so our order vocabulary has to cover the decisive moments: a narrow "spearhead to region X" vs a broad push, for example.

## 2. Mechabellum — composition and placement as the whole game

**Mechanic.** Round-based auto-battler. Each round you get supplies to buy and place units, unlock tiers, buy techs that change a unit's role, or spend on one-off devices. Units can't be moved or ordered afterwards ([Wikipedia](https://en.wikipedia.org/wiki/Mechabellum); [RPS review](https://www.rockpapershotgun.com/mechabellum-review)).

**Why it works.**
- Designer intent: "RTSs require too much of a player's reflex, and chess with no hidden information requires too much calculation. What I want is a game focused on predicting your opponent, forming good formations, and innovative thinking" ([Bearlike interview](https://mechabellum.ru/index.php/novosti/interview-with-bearlike-game-developer)).
- Counters are readable and hard: "each mech able to become another mech's worse nightmare". Placed units persist between rounds, but the current planning phase is hidden. You see what the enemy *has* but not what they are *adding*, which gives "effectively perfect information" plus a guessing game (RPS).
- Techs flip roles: Mustangs can buy anti-missile to counter Stormcallers (RPS). Counters come from purchasable *answers*, not only from a fixed RPS triangle.
- Short-term power that costs long-term economy (missiles, shields, nukes) makes "the power curves of both players … lead alternately … which is why comebacks happen so frequently" ([Dev Log #1](https://mechamonarch.com/news/mechabellum-dev-log-1)).
- They noticed the "watching" problem and added auto speed-up when one side can't fight back, plus speed-up votes (Dev Log #1).

**Applicability.**
- Our barracks lock is a Mechabellum placement: a permanent composition commitment. Frontline dominance is the symptom you'd expect when no purchasable answer exists. Give each unit type a **counter-tech** bought at the Workshop, e.g. "Ranged: Suppressing Fire vs massed melee", "Siege: Counter-battery". Workshop specializations already exist and can carry this.
- Let JEV's *existing* barracks types be visible and its *new* ones hidden until scouted.
- Make one-shot Power sinks (an airstrike, a shield) explicitly trade economy for tempo.

**Pitfall.** Mechabellum's counters only work because placement is precise. Our units path through regions on their own, so counter value can be lost to bad engagement geometry. The sim has to let the intended counters actually connect, or players will blame the sim and not their own composition.

## 3. Company of Heroes — territory, sector supply, capture points

**Mechanic.** Strategic points claim a sector. Owned sectors provide resources, vision and population cap. Income only flows if the sector **connects in a chain back to HQ**; cut-off sectors stop paying until reconnected. Points can be *secured* (an observation post) for higher yield and capture immunity until the post is destroyed ([CoH wiki: Strategic Point](https://companyofheroes.fandom.com/wiki/Strategic_Point); [Resources](https://companyofheroes.fandom.com/wiki/Resources)). Victory Point mode drains the opponent's tickets while you hold more VPs ([Wikipedia](https://en.wikipedia.org/wiki/Company_of_Heroes_(video_game))).

**Why it works.** Connectivity makes *where* you fight matter more than *how hard*. Taking one link in the chain cuts off several sectors, so cheap flanking captures pay off. Tickets force teams to contest several points at once instead of massing on one.

**Applicability (strong).** We already have 15 regions and 13 anchors, and today "nothing locks capture".
- Add **connectivity**: an anchor's income or bonus counts only if linked to your HQ through owned regions. Expand then gets a real target choice (cut JEV's chain vs. extend ours). JEV's planner can also reason about cuts deterministically.
- Add **securing** (a paid fortification on an anchor) as an investment decision.
- Consider a VP-style secondary win: hold more anchors and JEV's "uptime" drains. That gives an alternative to the HQ deathball inside 8–12 min.

**Pitfall.** CoH's flanking caps rely on micro'd infantry. Our caps happen through Expand paths, so capture speed (8 s) and path choice must be visible and quick enough to read. Otherwise cut-offs will feel random.

## 4. Unity of Command (I/II) — supply as the strategic layer

**Mechanic.** Units must stay within supply range of depots along roads and rail. Flanking moves cut them off and they become ineffective. UoC2 added player-placed depots and HQ units that must be in range for special attacks ([Kotaku UoC2](https://kotaku.com/unity-of-command-ii-is-a-really-solid-world-war-two-str-1839842115)).

**Why it works.** Kotaku: "the balance between movement, combat and supply is almost perfect … makes every single decision … feel important". Being cut off was "as close as a game has ever come to recreating just how catastrophic that can be."

**Applicability.** Supply radius from owned anchors or extractors could gate reinforcement: barracks auto-replacement only reaches squads in supplied regions. Deep Assaults then become a real risk/reward choice, and protecting the corridor becomes a job for a teammate. It also gives the auto-retreat rule something to *mean*.

**Pitfall.** Kotaku's main complaint was the strict turn limit, which "encourages … throwing units at the enemy so that you just keep on moving". Cramped maps made "dreary slogs". Our 8–12 min cap can cause the same thing: if the clock punishes preparation, players will just mash Assault. Tie time pressure to *escalation* (JEV gets stronger) rather than to a fail state.

## 5. R.U.S.E. — information as the resource

**Mechanic.** You always see *where* enemy units are, but not *what* or how many until recon has line of sight ([Eurogamer preview](https://www.eurogamer.net/r-u-s-e-preview)). Units only auto-fire on targets actively spotted by recon ([GameSpot review](https://www.gamespot.com/reviews/ruse-review/1900-6275700)). Ruses are cards played **per map sector**, with limits per sector. They fall into four classes: reveal (Spy/Decryption), hide (Radio Silence), fake (Decoy units/offensive), and alter behaviour (Blitz/Terror) ([Wikipedia](https://en.wikipedia.org/wiki/R.U.S.E.); GameSpot). The zoomed-out view shows units as chip stacks on a war table, so one click commands a group (GameSpot).

**Why it works.** A fog of *identity* rather than *position* means you always know where the fight is but must pay to learn whether you'll win it, which makes scouting a decision. Sector-scoped cards make a sector map strategic without any unit control.

**Applicability (very strong).** Our regions are R.U.S.E.'s sectors already.
- Draftable **region cards** fit the "drafts between battles" decision perfectly: Decoy barracks, Radio Silence (hide a region's composition), Spy (reveal JEV's queued orders in a region), Entrench.
- Show JEV's forces as blips by region, with type revealed only by an Extractor / anchor / scout card.

**Pitfall.** Deception needs an opponent that can be fooled. JEV is a deterministic planner, so for ruses to matter it must plan on *perceived* state, and the effect has to be visible ("JEV reroutes 2 squads toward the decoy"). Otherwise decoys are dead cards. GameSpot also notes the campaign drip-fed ruses until it got boring, so introduce cards fast.

## 6. Majesty (1 & 2) — incentives instead of orders

**Mechanic.** Heroes are autonomous. The ruler only builds, casts, hires, and places **reward flags** (attack / explore; defend in Majesty 2). Rewards are spent when posted and can't be retrieved. Hero class decides who answers: rogues chase gold, paladins chase danger ([Wikipedia](https://en.wikipedia.org/wiki/Majesty:_The_Fantasy_Kingdom_Sim)). Some buildings have reliable direct levers, e.g. the Warrior Guild's "call to arms" recall (same).

**Why it works.** CGW praised its "quick-paced, hands-off formula" (Wikipedia). It works for *ambient* goals like exploring or clearing lairs, where the unpredictability is the fun.

**Why it fails.** The GamesRadar review of Majesty 2 lists "Doesn't give you enough control" and "Dumb AI frustrates". Its example: 500 gold on a besieged building while a high-level elf walks off to an old explore flag. The review's verdict: the game "throws massive, immediate threats at you … so it seems absurd that your heroes take so long to respond" ([GamesRadar](https://www.gamesradar.com/majesty-2-the-fantasy-kingdom-sim-review)).

**Applicability.**
- Use incentive-style control only for optional, low-urgency goals, e.g. "bounty on JEV extractor X: any teammate's Expand barracks prefers it".
- Give every **emergency** a deterministic answer: Fall Back and Hold must be obeyed instantly, with no probabilistic compliance.
- A co-op twist: a bounty paid from *my* wallet that steers *your* barracks. That gives gifting a spatial meaning.

**Pitfall.** Probabilistic compliance plus time pressure equals helplessness. Our 8–12 min battles are all time pressure.

## 7. Northgard — slow pacing, territory slots, multiple victories, telegraphed hazards

**Mechanic.** Regions with a fixed number of building slots and region-specific resources ([Wikipedia](https://en.wikipedia.org/wiki/Northgard)). Winter raises upkeep and debuffs attack. Events (rats, blizzards) arrive on a schedule you can see in advance. There are several victory types, and special central tiles carry their own victory condition. Three basic unit types ([RPS review](https://www.rockpapershotgun.com/northgard-review)).

**Why it works.** RPS: "challenging not because of complexity but complacency". Its "micromanaging with *intent*" is scheduling, not clicking. Telegraphed hazards produce planning rather than reflex. Central-tile victories produced "one player struggling to hold that special tile against the onslaught of the others". The slow pace leaves "plenty of time to chat" in multiplayer (RPS). Shiro chose to keep combat simple: "Combat … is important, but not more than other gameplay systems" ([RPS interview](https://www.rockpapershotgun.com/we-spoke-to-shiro-games-about-the-future-of-northgard)).

**Applicability.**
- Put JEV "maintenance windows" and "deprecation waves" on a visible schedule. It fits the tone (a polite corporate AI announcing downtime) and creates planning beats.
- A per-node alternative objective (hold the "Data Center" region for 90 s) breaks the HQ-deathball default.
- Northgard succeeds with three unit types, so our three are enough *if* their roles are distinct.

**Pitfall.** RPS notes victory paths converge ("you'll still often find yourself repeating the same tactics"). Map-based objectives vary more than system-based ones.

## 8. Warpips — lanes, loadouts, no micro, short missions (closest *product* analogue)

**Mechanic.** Tug-of-war: spawn waves, destroy the enemy HQ. Before each battle, pick a small unit roster and the map region to attack. The only orders are "take cover" and "push forward". Restock and buy permanent upgrades between missions ([RPS](https://www.rockpapershotgun.com/warpips-is-a-tug-of-war-strategy-game-from-some-subnautica-devs)). The Steam page pitches "Focus on the big picture; no complex micro", 10–20 min rounds, random battles and an unlockable upgrade tree ([Steam](https://store.steampowered.com/app/1291010/Warpips); 89% positive of ~2k English reviews at time of reading).

**Why it works.** RPS: "captures a lot of the macro decision making": which units, quantity vs upgrades, economy. PC Gamer: "strategy but don't have enough brainpower to boot up a full-scale spreadsheet monster" ([PC Gamer](https://www.pcgamer.com/tug-of-war-rts-warpips-is-a-bit-of-cute-casual-carnage)).

**Applicability.** This shows a global-push/hold toggle plus a pre-battle loadout sustains a full campaign. Our per-barracks orders over a 15-region map are *richer* than one lane, which is good. The pre-battle **roster draft** (bring 3 of N unlocked barracks types) is cheap to add and helps the counter game.

**Pitfall.** A single lane means one front. We should not collapse into a de facto lane by making one region path dominant.

## 9. Teamfight Tactics (auto-battler economy and mastery)

**Mechanic.** Interest: +1 gold per 10 banked, capped at 5. Win and loss streaks both pay up to +3 ([guide](https://metabot.gg/en/TFT/guides/tft-economy-gold-interest-streaks-explained)).

**Why it works.** It turns saving vs spending into a timing skill ("roll-down" at your power spike). Riot's pillars list the skills rewarded: **Knowledge, Flexibility, Fortune, Perception (scouting opponents' builds), Speed** of judgement. They explicitly de-emphasise click accuracy, reaction speed "or the ability to coordinate with teammates" ([TFT design pillars](https://teamfighttactics.leagueoflegends.com/en-us/news/dev/dev-design-pillars-of-tft)). The same post argues discovery fades as the meta is solved, so they rotate whole sets.

**Applicability.**
- Interest belongs **between battles** (run wallet), not inside an 8–12 min real-time fight, where banking just means idling. [INFERENCE]
- Loss-streak compensation maps onto our lives pool: losing a battle costs a life but pays bonus draft picks.
- TFT's pillar list is a ready template for what *we* reward, with coordination added as the pillar TFT dropped.

**Pitfall.** Interest in a shared, team-income game creates a "who banks, who spends" coordination problem. That is fun if visible and toxic if hidden.

## 10. Supreme Commander / Total War auto-resolve (short)

- **SupCom.** Taylor's headline feature was strategic zoom: "zoom out to see the entire theatre of war … zoom in right under that cursor" ([HeavenGames interview](https://bfme2.heavengames.com/interviews/taylor)). He wanted true strategy, as opposed to "Real-Time Tactics" games that didn't get the scope right, and order queues let units run while you manage elsewhere ([GOG](https://www.gog.com/en/news/the_story_of_supreme_commander_and_how_chris_taylor_redefined_realtime_strategy_games)).
  - → Our default camera should be the war-table map, with regions as readable icons (R.U.S.E. chips too). Zooming in is for spectacle, not control.
- **Total War auto-resolve.** Community criticism: the AR maths favoured specific races and units and always wiped out the loser. After CA's rework showed the predicted outcome in advance, one critic wrote "Your decision-making at that point isn't strategy, it's paperwork" ([Legacy of Games](https://legacyofgames.com/2021/07/09/the-autoresolve-rework-made-it-even-worse)). Players also report unit types (chariots) dying in AR far beyond the preview ([CA forum](https://community.creative-assembly.com/total-war/total-war-warhammer/forums/8-general-discussion/threads/9714-chariots-are-too-easily-wiped-out-in-auto-resolve-causing-unreasonable-casualties)).
  - → Our battles *are* a live auto-resolve, and this gives two warnings. (1) Hidden sim biases produce a dominant unit; our Frontline dominance is likely this. (2) Fully predictable outcomes kill decisions. Show *odds* (R.U.S.E.'s "very easy … high danger" engagement indicator, [Wikipedia](https://en.wikipedia.org/wiki/R.U.S.E.)), not guarantees.

## 11. Bonus: Into the Breach — telegraphed enemy intent

Subset made every enemy attack telegraphed so "every death felt like your own fault", and the failure reason is always clear ([Game Developer](https://www.gamedeveloper.com/game-platforms/road-to-the-igf-subset-games-i-into-the-breach-i-)). They balanced via enemy *count*: "A single additional enemy turns a battle from 'a fun challenge' to 'completely impossible'".
- → JEV re-plans every 2 s deterministically, so it can *publish* its current plan, e.g. "JEV: Ticket #4471 — reallocating 2 squads to Region 7 in 20 s". That gives information as gameplay, polite-AI comedy, and fair defeats in one feature. Scouting and Spy cards can upgrade how much is visible.

---

## Cross-cutting answers to the brief

**Where do decisions live without unit control?** Five places, all sourced above:
1. **Composition commitment**: Mechabellum, Warpips loadout, our barracks lock.
2. **Where and when to commit**: HOI4 plans and planning bonus, CoH sectors.
3. **Economy timing**: TFT interest, Mechabellum short-vs-long spends.
4. **Information**: R.U.S.E. recon and ruses, TFT perception.
5. **Pre-commitment to telegraphed future events**: Northgard winter, Into the Breach.

Decision density [INFERENCE]: Mechabellum packs roughly 3–6 meaningful choices into each planning phase and then makes you watch. For real-time 8–12 min battles, aim for **one meaningful decision per player every ~20–40 s** (≈15–30 per battle): build/lock, order change, card, gift, tech. Budget that explicitly. Today's build menu plus four orders probably front-loads the decisions into minute 1–3 and leaves the rest as watching. Instrument it: log order changes and purchases per minute per player in playtests.

**Counters without micro.** Readable hard counters plus purchasable role-flipping techs (Mechabellum). Visible enemy composition with hidden *changes* (Mechabellum planning phase; R.U.S.E. identity fog). Odds indicators rather than certainties (R.U.S.E.; the TW anti-lesson). Pre-battle roster choice (Warpips).

**Information as a resource.** Show position, hide identity (R.U.S.E.). Make recon a prerequisite for effectiveness (R.U.S.E. auto-fire only on spotted units). Telegraph scheduled threats (Northgard) and the enemy's plan (Into the Breach). Use deception cards scoped to regions (R.U.S.E.).

**Anti-deathball / multi-front.** Frontage penalties (HOI4). Connectivity income, so cuts matter (CoH, UoC). Several simultaneous point objectives and tickets (CoH VP, Northgard special tiles). Splash and area counters to clumps (Mechabellum Stormcallers). Supply-range limits on deep pushes (UoC).

**Avoiding "watching, not playing".** Give the waiting a decision attached to it (HOI4 planning bonus). Let players plan the next move during resolution (Mechabellum "mentally queuing up priorities for the next"). Speed up decided fights (Mechabellum). Keep emergency levers deterministic (anti-Majesty). Use slack time socially (Northgard chat). Telegraphed JEV beats create "get ready" moments.

## Top 7 transferable lessons (ranked)

1. **Orders are plans; the executor is dumb and obedient.** (HOI4 DD45) Preview the Expand route and Assault target, never second-guess the player, and make the auto-retreat threshold visible and maybe per-barracks configurable.
2. **Territory must have *topology*, not just ownership.** (CoH, UoC) Income or reinforcement only through a connected chain to HQ. This alone creates multi-front play, flanking value and readable JEV threats.
3. **Fog of identity + region-scoped cards.** (R.U.S.E.) Show where JEV is, hide what it is, and let drafted cards reveal, hide or fake per region. JEV must plan on perceived state.
4. **Every unit needs a purchasable answer, and every answer a counter.** (Mechabellum) Fix Frontline dominance with counter-techs and roles, not just stat nerfs. Give a pre-battle roster pick (Warpips).
5. **Reward preparation with a visible, decaying charge.** (HOI4 planning bonus) It turns idle time into a timing decision and gives co-op teams a shared "go" moment.
6. **Telegraph JEV.** (Into the Breach, Northgard) Publish its current plan and scheduled events in corporate-memo voice. Defeats become explainable and the setting's humour gets a gameplay job.
7. **Short-term power must cost long-term economy.** (Mechabellum Dev Log) One-shot Power sinks create comebacks and alternating leads within one battle, and give gifting a tactical use ("fund my airstrike").

## Traps to avoid

- **Probabilistic obedience under time pressure** (Majesty 2 review). Never make Hold or Fall Back "maybe".
- **Hidden sim biases** (TW auto-resolve). If the sim quietly favours melee blobs, no amount of strategy fixes it. Measure win rates by composition in headless sims (JEV is deterministic, so this is cheap).
- **Fully previewed outcomes = paperwork** (TW critique). Show odds, not results.
- **A strict clock that rewards mashing Assault** (UoC2 turn-limit critique). Escalate JEV instead.
- **Deception against a mind-reading AI.** Decoys that JEV ignores are dead draft picks.
- **Single dominant lane / region path** (Warpips is one lane by design; we shouldn't be by accident).
- **Solved meta** (TFT). Without rotating mutators and cards, discovery dies. Our unlock-only meta-progression depends on content variety.
- **Front-loaded decisions.** Build in minute 1–3, then 8 minutes of spectating.

## (a) What keeps players replaying (from this domain)
- Theorycrafting synergies between sessions (Mechabellum workshop, RPS).
- Discovery, refreshed by rotating content (TFT sets).
- Randomised battles and maps plus an unlock tree (Warpips, Majesty random quest maps, Northgard procedural maps).
- Comebacks and alternating leads, so no game is decided early (Mechabellum Dev Log).
- Social slack and shared spectacle (Northgard chat, TFT "playful competition").

## (b) What these games reward
- Prediction / reading the opponent (Mechabellum designer; TFT "Perception").
- Composition knowledge and flexibility (TFT pillars).
- Timing commitments: planning bonus (HOI4), roll-downs (TFT).
- Logistics and positional thinking: supply and connectivity (UoC, CoH).
- Recon and deception (R.U.S.E.).
- Efficiency under scheduled pressure (Northgard).
- *Not* reflexes or click accuracy (TFT explicitly; Mechabellum's designer explicitly).

## (c) Open questions for Post-Frontier
1. What is the target **decisions-per-minute per player**, and where do decisions occur in minutes 4–12?
2. Does JEV reason on **true or perceived** state? This decides whether scouting and deception can exist.
3. Should income or reinforcement require **connectivity to HQ**? If so, how do we visualise cut-offs in 1 s?
4. Is the barracks type-lock the *only* composition lever, or can Workshop techs flip roles mid-battle?
5. What is the **counter matrix** for Frontline / Ranged / Siege, and what does JEV field that demands each?
6. Do we show JEV's plan (Into the Breach) by default, or make it a purchasable/draftable intel tier?
7. Which co-op roles emerge (economist / front-holder / raider / intel), and does team-split income + gifting reward them or flatten them?
8. Is there a non-HQ win condition per node (tickets / hold region) to break the deathball?
9. How do we keep 8–12 min from becoming "mash Assault before the clock"? Is escalation or a hard limit the pressure?
10. Should run-level economy have **interest / streak** mechanics (TFT), and how does that interact with the lives pool?
