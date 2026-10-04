# Commanders [New] — decided

> Part of the [Post-Frontier design](../Design.md). Related: [cards](cards.md), [meta](meta.md).

Each player picks a commander at the start of the run. **Commanders are unique within a team**, which forces the team to cover different roles.

A commander has:
- a **passive**,
- **one region ability**, paid with **Data plus a cooldown**,
- **one ultimate**, charged by doing that commander's job. It fires once or twice per battle and is a big team moment.
- a **starting kit**: 3 units, plus the production buildings they need and one signature building. The production buildings start each battle pre-built, placed during the planning phase ([battle.md](battle.md)); the signature building is built as normal,
- **its own card pool** for drafts.

Abilities and ultimates target regions, never units, and must change the map for teammates. That's how each player's contribution stays visible and necessary.

## Roster

There are 4 commanders at launch, and **all four are available from the first run**. Commanders are unique per team, so a group of up to 4 can always start. Unlocks add commander #5 and beyond ([meta.md](meta.md)). Effects are proposals; values are starting points for tuning.

| Commander | Starting units | Extra buildings | Passive |
|---|---|---|---|
| Groundbreaker (builder) | Brawler, Artillery, Repair crew | Factory, Turret | **−20% cost on Groundbreaker's own Turrets and support, intel and logistics buildings.** Scoped so the team gains nothing by funnelling all Power to Groundbreaker. |
| Line Cutter (sabotage) | Raider, Rifle, Scrambler | Factory, Conduit | Capture 50% faster |
| Quartermaster (logistics) | Brawler, Rifle, Shield projector | Lab, Repair Bay | Production 25% faster |
| Wiretap (intel) | Rifle, Artillery, Scrambler | Factory, Relay Tower | Sees JEV's next plan as well as the current one |

| Commander | Region ability | Ultimate | Ultimate charges from |
|---|---|---|---|
| Groundbreaker | **Fortify:** anchor can't be captured, allies take −25% damage there, 60 s | **Prefab Drop:** instantly place a finished Turret and Repair Bay in a controlled region | Completing buildings |
| Line Cutter | **Cut the Line:** an enemy region counts as disconnected for JEV, 45 s | **Blackout:** JEV's whole supply chain is disconnected for 30 s | Captures and cuts |
| Quartermaster | **Rush Shift:** instantly refill any ally's force in a connected region. **Quartermaster pays** the units' normal Power price, plus the Data cast cost. | **All Hands:** every friendly force on the team refills instantly. **Each force's owner pays** the normal price; the ultimate's value is time. | Refills delivered to own and allied forces. **Not gifts**, which could be passed back and forth to charge it. |
| Wiretap | **Jam:** delay JEV's committed plan in a region by 20 s | **Prompt Injection:** JEV's next committed plan is redirected to a region you choose | Beating JEV's committed attacks |

**[Change] Until commanders exist (build step 1b):** every commander has **Fortify** as a generic region ability, with the effect above. It costs **40 Data** (the cast cost in [economy.md](economy.md)'s Data budget) and has a **90 s cooldown** (starting value). Groundbreaker keeps it once commanders arrive (owner decision 2026-10-04).

Every kit leaves gaps on purpose. Teammates and drafts fill them. The armor classes each kit can answer are below; entries in *italics* are branches, which have to be drafted first ([cards.md](cards.md)).

| Commander | Kinetic (vs Light) | Piercing (vs Heavy) | EMP (vs Shielded) | Demolition (vs Structure) |
|---|---|---|---|---|
| Groundbreaker | Brawler, Turret | Brawler branch *Breacher* | Artillery branch *EMP Shells* | Artillery |
| Line Cutter | Raider | Rifle | Scrambler | Raider branch *Saboteur* |
| Quartermaster | Brawler | Rifle | Rifle branch *Ion Rounds* | — |
| Wiretap | — | Rifle | Scrambler | Artillery |

## Growth during a run

Ability and ultimate upgrades are cards in that commander's pool, e.g. *Fortify also repairs buildings*. There is no separate commander level track.

## Solo

A solo player picks a **primary and a secondary** commander, Monster Train style:
- The **primary** gives the passive, the ability and the ultimate.
- The **secondary** adds its starting units, extra buildings and card pool.

That way a solo kit covers more armor classes. A solo player's **unit cap is 6** rather than 5, because primary plus secondary can already bring 6 distinct types ([cards.md](cards.md)), and their **force cap is 5** rather than 4 ([forces.md](forces.md)).
