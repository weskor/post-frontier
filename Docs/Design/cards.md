# Drafts and cards [New] — decided

> Part of the [Post-Frontier design](../Design.md). Related: [commanders](commanders.md), [economy](economy.md), [forces](forces.md).

## After each battle

- Every player **privately** picks 1 card from a **structured offer of 3**, or skips:
  - one **Unlock**: a unit, building or branch,
  - one **Perk or Doctrine**,
  - one **Commander card**: an ability or ultimate upgrade, or a commander-flavoured doctrine.
- Everyone picks at the same time. The timer is **60 s**, and anyone can extend it once. Private picks are the main defence against one player quarterbacking the team.
- **Rerolls** cost Salvage.
- Elite and boss nodes add a **team Protocol** (a relic) chosen by vote.
- **Votes** (Protocols and route) go by majority. Ties go to the rotating navigator.

## Card types

| Type | Scope | Examples (illustrative) |
|---|---|---|
| Unlock: unit | Adds a unit type to your options; its production building comes with it if you don't own one | Lancer, Juggernaut |
| Unlock: branch | Makes one tier-2 branch of a unit available | Rifle → *Ion Rounds* |
| Unlock: building | Adds a building to your options | Shield Generator, Relay Tower |
| Perk | A slot item for one unit type, bought in battle with Data | *Hollow Points* (Rifle): +damage against Light |
| Doctrine | A rule for your forces, orders or Workshop | *Shield Wall*: Brawlers holding a region take −30% damage. *Counter-Battery*: Artillery targets enemy Artillery first. |
| Commander | An upgrade to your ability or ultimate | *Fortify also repairs buildings* |
| Protocol | Whole team, whole run | *Redundant Links*: a Conduit bridges two gaps. *Off-Grid*: +1 life, −10% income. |

## Rules

- **Cards change what orders do or what a barracks can be.** No invisible "+5% to units you can't control". This is the Into the Breach lesson: the run layer must come from what the battle lets players express. Perks are the one place for plain stat bumps, because each is bought into a specific force's slot and shown on its force card.
- **Branches are cards.** A unit has no tier-2 options until one of its branches is drafted. Third branches are Rare or Legendary.
- **Unit cap:** a player owns at most **5 unit types** in a run; solo players get 6 ([commanders.md](commanders.md)). Taking one more means dropping one. This forces each player to specialise and gives the team a reason to split roles.
- **Rarity:** Common, Rare and Legendary.
  - Rare is more likely from Elite nodes.
  - A Legendary is guaranteed from bosses.
  - A pity timer guarantees a Rare after a dry streak.
- With only about 6 picks per run, roughly **a third of offers should define the build**.
- **Starting state:** your commander's kit ([commanders.md](commanders.md)), plus **one branch card for one of its starting units**, so tier 2 is usable from battle 1. Everything else is drafted.

## First card lists [New] — decided shape, proposed content

**Perk rules:**
- Commons are pure upsides; Rares and Legendaries carry a trade-off.
- **3 perks per unit (27) plus 6 generic perks** that fit any unit.
- Perks are bought into slots with Data ([forces.md](forces.md)): **Common 30, Rare 50** (starting values, see the Data budget in [economy.md](economy.md)).

| Unit | Common | Common | Rare (trade-off) |
|---|---|---|---|
| Brawler | *Riot Shields:* −20% damage from ranged attacks | *Sledge Swing:* attacks hit 2 targets | *Berserk:* +40% attack speed below 50% HP, −20% max HP |
| Rifle | *Hollow Points:* +25% damage against Light | *Long Barrel:* +15% range | *Glass Cannon:* +40% damage, −25% HP |
| Artillery | *Siege Optics:* +25% range (today's specialization, reworked) | *Cluster Rounds:* +30% splash radius | *Barrage:* 3-shot volleys, then a 6 s reload |
| Lancer | *Capacitor:* +25% shield | *Focus Lens:* damage ramps up on the same target | *Overheat:* +50% DPS, the shield doesn't regenerate |
| Scrambler | *Wide Pulse:* +40% pulse radius | *Quick Cycle:* pulse every 7 s | *Feedback Loop:* the pulse also deals damage; each pulse costs the Scrambler 10% HP |
| Raider | *Nitro:* +15% speed | *Sticky Bombs:* +50% damage against Drill Rigs | *Hit and Run:* +30% damage for 5 s after entering a region, −20% HP |
| Repair crew | *Spare Parts:* +25% healing | *Long Cables:* +150 heal range | *Overclocked Repairs:* healing ×2, the crew takes damage over time |
| Shield projector | *Harmonics:* the bubble regenerates faster | *Wide Field:* +30% bubble radius | *Overcharge:* bubble ×2, the projector loses its own shield |
| Juggernaut | *Ram Plating:* −20% Kinetic damage taken | *Momentum:* the first hit after moving deals ×3 | *Unstoppable:* immune to stuns and ignores the retreat threshold, −20% speed |

| Generic perk | Rarity | Effect |
|---|---|---|
| Rugged | Common | +15% HP |
| Drilled | Common | +10% attack speed |
| Light Packs | Common | +10% speed |
| Spotters | Common | +10% range |
| Veteran Core | Rare | Refills cost −30%, squad −1 |
| Expendable | Rare | Refills cost −50%, units −20% HP |

**Doctrines (first set):**

| Doctrine | Effect |
|---|---|
| Shield Wall | Brawlers holding a region take −30% damage. Replaces today's *Entrenched Frontline* |
| Counter-Battery | Artillery targets enemy Artillery and Turrets first, with +range against them |
| Blitz | Attack orders move +25% faster for their first 20 s |
| Dig In | Forces holding a region for 30 s gain Cover there |
| Scorched Earth | Your Drill Rigs destroyed by JEV refund their cost as Data |
| Field Repairs | Your units heal out of combat. Today's specialization, kept as a card. |

**[Later] Workshop techs:** commander-wide defensive purchases (*Ion Coating*, *Reactive Armor*, *Kevlar Weave*, *Blast Shielding*). Cut from launch scope: they are hidden modifiers that silently weaken the counters players read from two icons. Defensive answers come from branches and perks instead (*Warden*, *Bulwark*, *Riot Shields*).

**Protocols (first set),** team relics that last the whole run:

| Protocol | Effect |
|---|---|
| Redundant Links | A Conduit bridges two gaps instead of one. Replaces *Sneakernet*, which would have deleted the connectivity rule for the whole run. |
| Off-Grid | +1 life now, −10% income for the run |
| Mutual Aid | Gifts arrive with a +20% bonus, up to 150 bonus Power per player per battle. Gifting back to the person who sent it earns no bonus, so there's no loop. |
| Air Gap | JEV calldowns can't target regions next to your main |
| Open Source | Perks one player buys can also be bought for teammates' forces of the same unit type |

**[Later] Cryosleep** (Protocol): one force per commander survives into the next battle, keeping its losses; refilling it costs Power as normal. It's the only way units carry over between battles ([run.md](run.md)).

## Salvage, the run currency

- Earned from battle performance: regions held, objectives, structures destroyed. Partial wins still earn some.
- Spent at Depots (buy or remove cards) and on rerolls.
- Resets at the end of a run.
