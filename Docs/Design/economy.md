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
- **Reward regions [New]:** each connected reward region your team holds pays **1 Data/s per commander**. Data per commander therefore doesn't shrink as the team grows. Region roles finally mean something.
- **Structure kills [New]:** destroying a JEV building pays **60 Data** into the team pool (starting value).
- **Upkeep:** none. Army size is limited only by refill cost. **[Candidate]** Company of Heroes–style upkeep (income falls as the army grows) is the next anti-turtling lever if baseline income still feeds stalemates after the 1b levers. Try it before removing the baseline.

**Baseline income — decided 2026-10-03.** The Power baseline stays, as a floor:
- **Why not zero:** a commander with no paying Drill Rig and under 160 Power could never rebuild income. Unlike StarCraft or Age of Empires, there are no workers to rebuild from, so that commander would sit out the rest of the battle. Territory games keep a floor too: Company of Heroes 2 gives +300 manpower/min, Dawn of War's HQ +20 requisition, Supreme Commander's commander +1 mass and +20 energy.
- Territory drives growth, and Data stays territory-only, with no baseline.
- **[Change]** Build step 1b measures **2/s against 1/s** with the halved deposit reserves below, against the 1b gate ([build-order.md](build-order.md)). Drop to 1/s if 2/s still lets players turtle.
- **[Change]** JEV's baseline becomes its own value. Today it is the human baseline times the player-count factor ([jev.md](jev.md)), so tuning the human floor would also change JEV.

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

- All income, Power and Data, goes into one team pool, which is split evenly into each commander's **private wallet**. Each wallet keeps an accumulator for fractions, so 4/s split three ways on the 2 s tick loses nothing.
- Who built a Drill Rig no longer matters for income; it only decides who can cancel it while it's under construction.
- Today a Drill Rig pays only its builder.

## Connected territory [Change]

- A Drill Rig, and a reward region's Data trickle, pays only while its region is **connected to the friendly main region through controlled regions**. A Conduit can bridge one gap ([buildings.md](buildings.md)).
- **Connected means** a path of regions your team *controls*, starting at your main. A contested region still counts as long as you control it, which keeps the rule readable; a neutral or enemy-held region breaks the chain. JEV's chain starts at JEV's main.
- Cutting the chain stops income from every region beyond the cut.
- The cut must be readable within 1 s: the cut-off region's border flashes and its Drill Rigs grey out.
- Today a Drill Rig keeps paying after its region is lost.
- This applies to JEV as well, so cutting JEV's chain is a real target for an Attack or Move & Hold order.

## Deposits [Change]

| Deposit | Rate | Reserve | Runs out after |
|---|---:|---:|---:|
| Normal | 4/s | 1200 (today 2400) | 300 s (today 600 s) |
| Rich | 6/s | 1500 (today 3000) | 250 s (today 500 s) |

- A Drill Rig costs 160 Power, takes 9 s to build and has 350 HP.
- Habitable Zone v2 has 16 deposits: 8 normal and 8 rich.
- **Reserves halved — decided 2026-10-03,** so the first deposits run dry around 5 min and force the second expansion. Depletion stays. StarCraft II: Legacy of the Void made the same move, half of each base's mineral patches at half the minerals, to make players take expansions more aggressively. Built in step 1b ([build-order.md](build-order.md)).

## Gifting [New]

- Any commander can send Power or Data to a teammate for free.
- Every gift appears in a visible team log.
- **Decided:** gifting is **free and unlimited**. The visible log is the only safeguard; we trust friends.

## HQ tiers and team calldowns [Later] — cut from launch scope

- The design: teammates chip in Power to raise 3 HQ tiers, which unlock team calldowns ([buildings.md](buildings.md)).
- Moved out of launch scope after the design review, because commander abilities already provide the team moments.
- Revisit after launch.
