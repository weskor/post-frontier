# JEV, the enemy

> Part of the [Post-Frontier design](../Design.md). Related: [battle](battle.md), [commanders](commanders.md), [map](map.md), [meta](meta.md), [ui](ui.md).

## How JEV plays [Change] — decided

- **[Built]** A deterministic planner that re-plans every 2 s and follows the same economy, placement and production rules as the players.
- **[New] Matching start:** JEV starts each battle with a pre-built base matching the players' kits, so its first release still matters ([battle.md](battle.md)).
- **[New] HQ guard:** JEV defends its Failover Nodes like any threatened region, and its final protocol wave spawns at the Lattice when it goes offline ([battle.md](battle.md)).
- **[New] Hybrid:** JEV still mines, builds and expands under player rules, so its economy can be raided and its supply chain cut. On top of that, every version release ([battle.md](battle.md)) sends a **scheduled wave**.
- **Waves are free spawns** at JEV's main, paid from a per-release budget rather than JEV's wallet. That is what reliably ends stalls ([battle.md](battle.md)). Each wave gets an Attack order with a published plan, and its composition follows the personality.

| Release | Wave budget (Power-equivalent, N = 1, act depth 1) |
|---|---:|
| `v1.1` | 150 |
| `v1.2` | 250 |
| `v2.0` | 400 |
| `v2.1` | 600 |
| Overrun (from ~10:00) | 300 every 60 s |

Wave budgets scale with player count (see Scaling below) and by ×1.15 per node depth. All values are starting values for the harness.

- **[New] Calldowns:** two at launch. Each is **announced 20 s ahead** on the timeline and gives teams a reason to spread out:

| JEV calldown | Effect |
|---|---|
| Forced Update | Disables buildings in one region for 15 s |
| Rate Limit | Production in one region runs at half speed for 30 s |

**[Later]** Three more were designed and cut from launch scope: *Deprecate* (it duplicated the Pattern Matcher), *Auto-Scale* (it duplicates waves) and *Terms Change* (Workshop techs are cut).

## Published intent [New]

- **The unit of a published plan is one JEV force:**
  - its source region, target region, size band and ETA;
  - the ETA is computed from the per-class speeds;
  - under fog ([map.md](map.md)), size and composition show only when the source region is visible.
- **Committed:** a plan is held for **20–30 s**. During that window the planner may not change that force's order, which also keeps the display from flickering with the 2 s re-planning. Two exceptions:
  - The force's own region is attacked. It may defend, and the plan visibly changes to *Escalated: defending X*.
  - The target becomes invalid: destroyed, or captured by JEV.
- This commitment is what gives *Jam*, *Signal Jam* and *Prompt Injection* their meaning: Jam and Signal Jam delay the plan, and Prompt Injection replaces its target.
- **Today:** `EnemyPlan` is one global text string that's never drawn, and the planner keeps no state between evaluations (`EnemyCommander.h`). Plans per force with a hold window need new planner state.
- It is written as a corporate memo, for example: `Ticket #4471 · Reallocating ~8 units to West Cut · ETA 0:30`.
- Commanders and cards can reveal more: composition, the next plan, building queues.

## How JEV decides — decided

- **No LLM runs in the game at launch.** JEV's decisions come from its deterministic planner.
- **Memos come from writer-made templates** filled from each plan: ticket number, size band, region, ETA and personality-flavoured verbs. A memo can never contradict its plan.
- **The planner proposes, a chooser picks.** At each decision, the planner produces a few legal candidate plans with scores, and a chooser picks one. The launch chooser is deterministic: the best personality-weighted score. The planner enforces commitment whatever the chooser does.
- **[Later] LLM chooser experiment:** an optional, host-only, opt-in mode in which an LLM picks among the legal candidates and returns only an index, with a 3 s fallback to the deterministic pick. It would need Steam's live-generated AI disclosure and its own balance runs in the harness, and it must never see player-written text. Only considered once the deterministic JEV passes its own gates.
- **Why** ([llm-jev.md](../Research/llm-jev.md)):
  - LLM strategists match good scripted AI rather than beat it, and still need a scripted fallback.
  - They would break the deterministic harness and seeded runs.
  - They need a server for the game's lifetime, or 1–2.5 GB of video memory, which loses offline play or the Steam Deck.
  - 85% of core players say they're negative on generative AI in games.

## Escalation [New]

The visible version schedule described in [battle.md](battle.md). Each release adds a behaviour and a scheduled wave.

## Scaling with player count [Change]

- **[Built] Baseline income:** JEV's baseline is multiplied by ×(1 + 0.3 × (N − 1)). N counts human player states with a valid commander slot in the current match roster, read at each payment, so joining or leaving changes the rate. Counts below one use N = 1. Fractional credits carry between integer-wallet payments, including across roster changes; extraction income is not multiplied.
- **[New] Wave budget:** uses the same player-count factor. Node depth multiplies on top.
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
