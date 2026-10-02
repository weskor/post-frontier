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
- **Upkeep:** none. Army size is limited only by refill cost.

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

## Deposits [Built]

| Deposit | Rate | Reserve | Runs out after |
|---|---:|---:|---:|
| Normal | 4/s | 2400 | 600 s |
| Rich | 6/s | 3000 | 500 s |

- A Drill Rig costs 160 Power, takes 9 s to build and has 350 HP.
- Habitable Zone v2 has 16 deposits: 8 normal and 8 rich.
- Deposits stay finite for now. Whether to halve reserves or drop depletion is open ([battle.md](battle.md), [open-questions.md](open-questions.md)).

## Gifting [New]

- Any commander can send Power or Data to a teammate for free.
- Every gift appears in a visible team log.
- **Decided:** gifting is **free and unlimited**. The visible log is the only safeguard; we trust friends.

## HQ tiers and team calldowns [Later] — cut from launch scope

- The design: teammates chip in Power to raise 3 HQ tiers, which unlock team calldowns ([buildings.md](buildings.md)).
- Moved out of launch scope after the design review, because commander abilities already provide the team moments.
- Revisit after launch.
