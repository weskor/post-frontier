# JEV, the enemy

> Part of the [Post-Frontier design](../Design.md). Related: [battle](battle.md), [commanders](commanders.md), [map](map.md), [meta](meta.md), [ui](ui.md).

## How JEV plays [Change] — decided

- **[Built]** A deterministic planner that re-plans every 2 s and follows the same economy, placement and production rules as the players.
- **[New] Matching start:** JEV starts each battle with a pre-built base matching the players' kits, so its first release still matters ([battle.md](battle.md)).
- **[New] HQ guard:** JEV defends its Failover Nodes like any threatened region, and its final protocol wave spawns at the Lattice when it goes offline ([battle.md](battle.md)).
- **[New] Hybrid:** JEV still mines, builds and expands under player rules, so its economy can be raided and its supply chain cut. On top of that, every version release (table below) sends a **scheduled wave**.
- **[Built] Supply chain:** JEV builds and counts a Drill Rig only in a region its main reaches through regions it controls (`ForceOrders::ConnectedMask`, the one connectivity rule), so it never pays for a disconnected rig and resumes investing once the chain is retaken. Its planner values an expansion or Move & Hold that restores connected income, counts an isolated deposit as worth nothing, and values holding a region that connected income depends on.
- **[Built] Waves are free spawns** at JEV's main, paid from a per-release budget rather than JEV's wallet. That is what reliably ends stalls ([battle.md](battle.md)). Each wave gets an Attack order with a published plan. Until personalities arrive (step 4), its composition follows the per-release behaviour below; afterwards it follows the personality.

**Releases [Change].** The visible version schedule. Each release adds a behaviour and a wave. Starting values, orchestrator 2026-10-04:

| Release | At | Wave budget (Power-equivalent, N = 1, node depth 1) | Behaviour added |
|---|---|---:|---|
| `v1.0` | 0:00 | none | Base behaviour: builds, expands, defends |
| `v1.1` | 2:00 | 150 | The wave raids the nearest connected human Drill Rig region, using the cheapest units |
| `v1.2` | 4:00 | 250 | The wave counters the humans' most numerous armor class |
| `v2.0` | 6:00 | 400 | The wave and every JEV force attack together |
| `v2.1` | 8:00 | 600 | Wave units get +15% speed |
| Overrun | every 60 s from 10:00 | 300 each | Sustained pressure designed to overrun a team that is stalling |

- **[Built] Budgets:** multiplied by the player-count factor (see Scaling below). Node depth is 1 in step 1b; depth scaling (×1.15 per node depth) arrives with the run layer.
- **[Built] Spending:** each budget buys whole units of today's roster, and the leftover carries to the next wave. A raid wave buys the cheapest unit; from `v1.2` it buys the cheapest unit strong against the humans' most numerous armor class, then fills the rest with the cheapest unit (the cheapest alone when nothing counters that class). Forces split a wave evenly, at most 6 units each. A release that cannot place units, or has no region to attack, carries its budget instead of losing it.
- **[Built] Spawning:** a wave spawns free, with no wallet change and no extraction, as forces of up to 6 units at JEV's main, each with an Attack order and a published plan. From `v2.0` every other JEV force takes the wave's Attack order and a fresh ticket too, except a force defending its attacked region, retreating or recovering.
- **[Built] Free forces fight to the end:** a wave force has no producer, so it never refills. Its retreat threshold is Never and the planner never sends it to recover, so it keeps its orders however hurt it is. Forces JEV's producers refill keep the normal recovery rules.
- **[Built] Timeline:** each release appears on JEV's timeline 30 s before it happens. The schedule, the release in force, the next release time, whether it is within 30 s and the last 8 wave events replicate on `AEnemyCommander::Release`; match time comes from one function, `GetMatchSeconds`, which is frozen while the game is paused. The schedule stops when the match ends: no release launches after that. The HUD drawing is [New] ([ui.md](ui.md)).
- **[Built] `v2.1` speed:** wave forces launched from `v2.1` on carry a replicated `AArmyGroup::SpeedFactor` of 1.15, multiplied into the force's base march speed. It composes with region traits (Open) and the selection cap, and the planner's ETA uses it. Forces launched before `v2.1`, and forces that join a wave, keep factor 1.
- Calldowns stay in step 4. **[Later]** Target timing for step 4: the first calldown comes with `v2.0`, and calldowns get faster at `v2.1`. All values are starting values for the harness.

- **[New] Calldowns:** two at launch. Each is **announced 20 s ahead** on the timeline and gives teams a reason to spread out:

| JEV calldown | Effect |
|---|---|
| Forced Update | Disables buildings in one region for 15 s |
| Rate Limit | Production in one region runs at half speed for 30 s |

**[Later]** Three more were designed and cut from launch scope: *Deprecate* (it duplicated the Pattern Matcher), *Auto-Scale* (it duplicates waves) and *Terms Change* (Workshop techs are cut).

## Published intent [Built]

- **[Built] The unit of a published plan is one JEV force:**
  - its source region, target region, size band and ETA;
  - the size band rounds the unit count to the nearest multiple of 2, with a minimum of 2; odd counts round up (3 → `~4 units`), and the display reads `~N units`;
  - the ETA is computed from the per-class speeds along the region path;
  - **[New]** under fog ([map.md](map.md)), size and composition show only when the source region is visible.
- **[Built] Committed:** a plan is held for **25 s**. During that window the planner may not change that force's order, which also keeps the display from flickering with the 2 s re-planning. Two exceptions:
  - The force stands in a JEV-controlled region that is attacked: hostile units are inside it, or JEV-owned assets in it are damaged. It deterministically defends that region and publishes *Escalated: defending X*, retaining its ticket and deadline. An actively Retreating force keeps retreating. Neutral and player-controlled regions never trigger escalation. A plan created to defend its own attacked region is already escalated; it does not flip labels on the next evaluation.
  - The target becomes invalid: destroyed, or captured by JEV.
- **[Built] Execution:** plans use the same Move & Hold, Attack, Retreat, casualty withdrawal, production and whole-region Hold rules as players ([forces.md](forces.md)). Retreat publishes its executor-selected safe endpoint; JEV sets its producer rally there so natural completion holds safety. Completion publishes the resulting Hold without resetting the ticket/deadline; health scoring may choose another order only when commitment expires. Executor completions can precede publication by up to one planner evaluation interval.
- **[Built] Published state:** `ACommandGameState::EnemyPlans` replicates each live force's ticket, force identity, verb, source, target/structure, size band, ETA with the server time it was computed (`EtaIssuedAt`, so every peer counts down from the same moment), commitment deadline/remaining time, escalation and memo to every player. `EtaIssuedAt` restarts when the ticket, verb, target or escalation changes, and whenever the published ETA value itself changes (for example a display ETA recomputed from a new position); a size-band change alone keeps it. Destroyed forces are removed; a completed match clears the active list. The old global debug string is removed.
- **[New] Disruption:** this commitment gives *Jam*, *Signal Jam* and *Prompt Injection* their meaning: Jam and Signal Jam delay the plan, and Prompt Injection replaces its target.
- **[Built] Memo text**, for example: `Ticket #4471 · Move & Hold: reallocating ~8 units to West Cut · ETA 0:30`. Each memo names its actual verb or defending escalation.
- **[Built] Presentation:** the timeline bar, region badges and memo feed read this state and nothing else ([ui.md](ui.md#jev-intent-display-built--new)). **[New]** Commanders and cards can reveal composition, the next plan and building queues.

## How JEV decides — decided

- **[Built] No LLM runs in the game at launch.** JEV's decisions come from its deterministic planner.
- **[Built] Memos come from writer-made templates** in `[JevMemos]` in [DefaultGame.ini](../../Config/DefaultGame.ini), filled from each plan's ticket number, size band, region and ETA. Missing or contradictory templates are rejected; a memo can never contradict its plan. **[New]** personality-flavoured verbs.
- **[Built] The planner proposes, a chooser picks.** At each decision the pure planner retains the three best legal candidates, and a deterministic chooser picks the highest neutral score with stable region/verb/structure-identity ties. Commitment is enforced separately from choice. **[New]** personality weighting.
- **[Built] Reservations and command rejection:** all valid committed unowned destinations are reserved before any force chooses, independent of force-number evaluation order. A rejected fresh choice tries the remaining legal candidates. If none is accepted, the live actual order stays published and committed instead of repeatedly issuing the same rejected proposal each evaluation; forced defense still overrides this rejection shortcut, and a held defense cannot fall back to an unrelated order.
- **[New] No Fortify in 1b:** JEV has no Fortify ([commanders.md](commanders.md)). Its planner scores a Fortified hostile region's defence × 1.33 (starting value, orchestrator 2026-10-04).
- **[Built] Simulation evidence:** retained creation/escalation events count unique team-5 tickets by their original verb, including short-lived plans between samples. Events capture source-region ownership and whether a command changed at creation/transition time; re-escalation of the same ticket to another defended region writes another event, while repeated publication of the same defense does not. Reports separate already-escalated creation defense, order-changing transitions and label-only transitions by JEV/neutral/player control. Sampled active plans include memos, ETA and remaining commitment; reports show captures and observed attacks alongside plan counts. Team-0 autopilot does not publish JEV plans.
- **[Later] LLM chooser experiment:** an optional, host-only, opt-in mode in which an LLM picks among the legal candidates and returns only an index, with a 3 s fallback to the deterministic pick. It would need Steam's live-generated AI disclosure and its own balance runs in the harness, and it must never see player-written text. Only considered once the deterministic JEV passes its own gates.
- **Why** ([llm-jev.md](../Research/llm-jev.md)):
  - LLM strategists match good scripted AI rather than beat it, and still need a scripted fallback.
  - They would break the deterministic harness and seeded runs.
  - They need a server for the game's lifetime, or 1–2.5 GB of video memory, which loses offline play or the Steam Deck.
  - 85% of core players say they're negative on generative AI in games.

## Escalation [New]

The visible version schedule is the Releases table above; the HUD timeline counts down to each release ([battle.md](battle.md#no-clock-jev-escalates)). Each release adds a behaviour and a scheduled wave.

## Scaling with player count [Change]

- **[Built] Baseline income:** JEV's baseline is multiplied by ×(1 + 0.3 × (N − 1)). N counts human player states with a valid commander slot in the current match roster, read at each payment, so joining or leaving changes the rate. Counts below one use N = 1. Fractional credits carry between integer-wallet payments, including across roster changes; extraction income is not multiplied. **[Change]** The base value becomes JEV's own constant ([economy.md](economy.md#resources-change--decided)).
- **[Built] Wave budget:** uses the same player-count factor, read at each release. **[Later]** Node depth multiplies on top.
- **[Built] Solo:** uses the same formula with N = 1, including single-commander simulation matches.
- **[New] Solo relief:** comes from the secondary commander ([commanders.md](commanders.md)) and active pause ([ui.md](ui.md)).
- **[Candidate] Separate solo factor:** added only if playtests show it's needed.

## Personalities [New] — decided

Three at launch, more as unlocks. Each node gets one, and each tests a different skill:

| Personality | Behaviour | Tests |
|---|---|---|
| Hypergrowth | Fast expansion, raids Drill Rigs, cuts supply chains | Defending connected territory |
| Compound Interest | Builds up its economy, then makes a huge late push | Early aggression and timing |
| Pattern Matcher | Counter-builds the team's most-fielded unit type. Under fog, the most-*seen* type. This is the only launch mechanic that punishes the most-fielded type. | Flexible composition |

## Bosses [New] — decided

- **One boss per act.** The final boss is always **JEV Core**. The act 1 and act 2 bosses are drawn from a pool.
- **Every boss battle has:**
  - a **unique map**,
  - a **boss structure with phases**,
  - a **counter-rule** aimed at the dominant strategy.

| Boss | Rule | Tests |
|---|---|---|
| Echo Chamber **[Later]** | Copies the team's most-fielded unit type each phase. Cut from launch: it duplicates the Pattern Matcher. | Counter-building against yourself |
| Von Neumann Swarm | Expands extremely fast | Cutting its supply chain |
| The Redactor | Bans one of your order verbs per phase (no Attack, no Move & Hold, …) | Flexible plans |
| Triple Redundancy | Three cores in three regions; destroying one makes the others stronger | Splitting the team on purpose |
| **JEV Core** (final) | The Lattice with shield phases; each phase adds the rule of a boss beaten earlier in the run | Everything |

- Each boss has its own reward.
- **Pool:** Von Neumann Swarm, the Redactor and Triple Redundancy. Two are available from the first run; the third unlocks ([meta.md](meta.md)).
- **Content cost:** 3 act maps plus 4 boss maps.

## Difficulty [New] — decided

- One base difficulty.
- The **Terms of Service** ladder unlocks after the first win ([meta.md](meta.md)).
- **Per-player assist:** a private option such as +20% income, for weaker friends. It is never shown as a penalty to others.

## [Candidate] Pacing director

- Alternates build-up, peak and relax phases in *when* JEV strikes, never in how strong it is.
- JEV specials that punish anti-co-op play, such as a raider that targets the commander with the most undefended Drill Rigs.
