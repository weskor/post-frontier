# Economy

> Part of the [Post-Frontier design](../Design.md). Related: [battle](battle.md), [buildings](buildings.md), [open-questions](open-questions.md).

## Resources [Change] — decided

There are two resources:

| Resource | Buys | Comes from |
|---|---|---|
| **Power** [Built] | Buildings, units, refills | 2/s baseline per commander, Drill Rigs |
| **Data** [New] | Tier upgrades, perks, commander abilities | Destroying JEV structures; holding reward regions |

Power alone funds your army, but tech needs Data, so aggression and holding territory fund your upgrades.

- **Opening [Change]:** each commander starts with **200 Power** (starting value; today 600) and 0 Data, because their kit starts pre-built ([battle.md](battle.md)), and earns a 2/s Power baseline.
- **Reward regions [New]:** each controlled, connected reward region your team holds pays **1 Data/s per commander**, through the team pool. Data per commander therefore doesn't shrink as the team grows. Region roles finally mean something. On Habitable Zone v2 the reward regions are Uplink, Relay Plant, Power Yard and Cooling (starting value, orchestrator 2026-10-04).
- **Structure kills [New]:** destroying a **completed** JEV building pays **60 Data** into the team pool (starting value), whatever killed it. It covers the Barracks, Drill Rig and Workshop, and the Failover Node once it exists. It excludes the HQ, buildings under construction and cancelled buildings. JEV earns no Data in step 1b (starting values, orchestrator 2026-10-04).
- **Upkeep:** none. Army size is limited only by refill cost. **[Candidate]** Company of Heroes–style upkeep (income falls as the army grows) is the next anti-turtling lever if baseline income still feeds stalemates after the 1b levers. Try it before removing the baseline.

**Baseline income — decided 2026-10-03.** The Power baseline stays, as a floor:
- **Why not zero:** a commander with no paying Drill Rig and under 160 Power could never rebuild income. Unlike StarCraft or Age of Empires, there are no workers to rebuild from, so that commander would sit out the rest of the battle. Territory games keep a floor too: Company of Heroes 2 gives +300 manpower/min, Dawn of War's HQ +20 requisition, Supreme Commander's commander +1 mass and +20 energy.
- Territory drives growth, and Data stays territory-only, with no baseline.
- **[Change]** Build step 1b measures **2/s against 1/s** with the halved deposit reserves below, against the 1b gate ([Balance.md](../Balance.md#step-1b-gate-new)). The human baseline stays at 2/s per commander until that measurement says otherwise. Drop to 1/s if 2/s still lets players turtle (starting value, orchestrator 2026-10-04).
- **[Change]** JEV's baseline becomes its own constant, `JevBaselineIncome`, set to 2/s and still multiplied by the player-count factor ([jev.md](jev.md#scaling-with-player-count-change)). Today it is the human baseline times that factor, so tuning the human floor would also change JEV (starting value, orchestrator 2026-10-04).

**Data budget (starting values, per commander per battle).** Demand should exceed supply, so Data forces choices.

| | Data |
|---|---:|
| **Income:** about 1.5 reward regions held for about 6 min | ~540 |
| **Income:** structure kills, a share of 2–4 kills | ~60–120 |
| **Demand:** tier 2 on 3 forces (3 × 50) | 150 |
| **Demand:** tier 3 on 2 forces (2 × 120) | 240 |
| **Demand:** perks, 4 × Common 30 or Rare 50 | ~160 |
| **Demand:** ability casts, 4 × 40 | 160 |
| **Total demand vs income** | ~710 vs ~600–660 |

## Shared team income [Change]

- **The pool [Change]:** on each 2 s payment the team pool collects:
  - the human baseline of every roster commander;
  - the extraction of every connected Drill Rig, whoever built it;
  - the Data of every connected reward region.
- **The split:** the pool is divided evenly among the team's current roster commanders, into their **private wallets**, in commander-slot order. Each wallet keeps a fractional carry per resource, and the split conserves exactly over repeated ticks: 4/s across three commanders loses nothing.
- **Joining and leaving:** a commander who joins starts with the starting wallet and an empty carry. A commander who leaves loses their wallet and carry. With no recipients nothing is paid and no deposit depletes.
- **Not shared:** construction-cancel refunds go to the canceller. Gifts are not income.
- Who built a Drill Rig no longer matters for income; it only decides who can cancel it while it's under construction. Today a Drill Rig pays only its builder.
- These pool rules are starting values (orchestrator 2026-10-04).

## Connected territory [Change]

- A Drill Rig, and a reward region's Data trickle, pays only while its region is **connected to the friendly main region through controlled regions**. A Conduit can bridge one gap ([buildings.md](buildings.md)).
- **Connected means** a path of regions your team *controls*, starting at your main. A contested region still counts as long as you control it, which keeps the rule readable; a neutral or enemy-held region breaks the chain. JEV's chain starts at JEV's main. While an HQ is offline the main stays controlled ([battle.md](battle.md#guarding-the-hqs-change--decided)).
- Cutting the chain stops income from every region beyond the cut. A disconnected Drill Rig neither pays nor depletes, and a disconnected reward region pays no Data (starting value, orchestrator 2026-10-04).
- **[New]** The connected set is recomputed whenever any region controller changes, and at least every 0.25 s. Each team's connected mask replicates together with the server time of its last change (starting value, orchestrator 2026-10-04).
- **[New]** There is one connectivity rule: income here and reinforcements ([forces.md](forces.md)) both read `ForceOrders::ConnectedMask`.
- The cut must be readable within 1 s: the cut-off region's border flashes and its Drill Rigs grey out.
- Today a Drill Rig keeps paying after its region is lost.
- This applies to JEV as well, so cutting JEV's chain is a real target for an Attack or Move & Hold order.

## Deposits [Change]

| Deposit | Rate | Reserve | Runs out after |
|---|---:|---:|---:|
| Normal | 4/s | 1200 (today 2400) | 300 s (today 600 s) |
| Rich | 6/s | 1500 (today 3000) | 250 s (today 500 s) |

- A Drill Rig costs 160 Power, takes 9 s to build and has 350 HP. Doubling structure HP is not part of step 1b ([buildings.md](buildings.md#rules); orchestrator 2026-10-04).
- Habitable Zone v2 has 16 deposits: 8 normal and 8 rich.
- **Reserves halved — decided 2026-10-03,** so the first deposits run dry around 5 min and force the second expansion. Depletion stays. StarCraft II: Legacy of the Void made the same move, half of each base's mineral patches at half the minerals, to make players take expansions more aggressively. Built in step 1b ([build-order.md](build-order.md)).

## Gifting [New]

- During the live battle, any human commander can send a positive whole amount of Power or Data to a teammate in the roster, never to themselves. A gift is free and atomic.
- Every accepted gift is appended to a replicated team log (the last 20 entries) and appears in the alert feed, e.g. "Commander 2 gifted 100 Power to Commander 1". A rejection says why (starting value, orchestrator 2026-10-04).
- **Decided:** gifting is **free and unlimited**. The visible log is the only safeguard; we trust friends.

## HQ tiers and team calldowns [Later] — cut from launch scope

- The design: teammates chip in Power to raise 3 HQ tiers, which unlock team calldowns ([buildings.md](buildings.md)).
- Moved out of launch scope after the design review, because commander abilities already provide the team moments.
- Revisit after launch.
