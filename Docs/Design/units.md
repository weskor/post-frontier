# Combat and counters [Change]

> Part of the [Post-Frontier design](../Design.md). Related: [cards](cards.md), [commanders](commanders.md), [meta](meta.md).

## Problem before the counter retag

A full barracks of each type was worth very different amounts. Before the step 1a retag, all units moved at the same speed.

| Full barracks | HP | DPS | Cost to fill | Range |
|---|---:|---:|---:|---:|
| Frontline ×6 | 840 | 120 | 120 | 175 |
| Ranged ×4 | 360 | 63 | 120 | 560 |
| Siege ×2 | 220 | 32 | 100 + 180 | 1150 |

- Before the retag, no armor or damage-type modifiers existed and targeting was nearest-first, so Frontline dominated. The three Workshop specializations still work: Siege Optics (×1.25 Siege range, ×0.75 damage), Entrenched Frontline (×0.75 damage taken), and Field Repairs (healing).
- Without micro, Ranged could not kite to make up for it. The retag adds counters and class speeds without tuning the other combat or production values; balance validation is still required.

## Counter system [Change] — decided

**Armor classes plus bonus damage [Built]**, StarCraft II style. Every unit has one armor class and every weapon has one damage type. Kinetic, Piercing and Demolition each deal bonus damage to one class. The two-icon presentation is [New].

| Armor class | What it is | Weak to |
|---|---|---|
| Light | Unarmored, fast, cheap | Kinetic |
| Heavy | Armor plating, slow, high HP | Piercing |
| Shielded [New] | A shield layer on top of HP that recharges after a few seconds out of combat | EMP |
| Structure | Buildings and HQs | Demolition |

- **Bonus [Built]:** ×1.5 damage against the matching Light, Heavy or Structure class, ×1.0 otherwise. Starting value; tune it in the harness. Fractional HP truncates; the class bonus applies before the outgoing Siege Optics and incoming Entrenched Frontline modifiers. EMP currently has no HP bonus; its shield rule comes with Shielded units.
- **Targeting [Built]:** follows the rule in [forces.md](forces.md). That topic owns explicit-target priority, retained-target lifetime, automatic acquisition and the pending force-card presentation.
- **Shields [New] — decided:**
  - Shield points absorb damage before HP.
  - They regenerate at 10% per second after 4 s without taking damage.
  - **EMP deals ×2 to shield points**; this replaces EMP's ×1.5 class bonus.
  - Shields reward pulling back and rotating, and EMP strips them fast.
- **Triangle:** each unit carries its own damage type (see the roster below). The roster guarantees **at least two answers to every armor class**.
- **Air [Later]:** an unlockable layer once ground play is proven. It will need its own anti-air answers, and it bypasses region paths.

## Unit rules — decided

- **Roster size:** 9 unit types per side once content is complete. Each commander starts the run with **its own 3** ([commanders.md](commanders.md)); the rest are drafted ([cards.md](cards.md)).
- **Shape:** hand-designed niches, each tagged with an armor class.
- **Factions:** humans and the Machine share roles and armor classes. Each faction has its own **twists** and its own look. JEV keeps using the same rules as the players.
- **Force size:** small squads of 2–6 units per barracks, as today. Every unit stays readable and losses feel personal. The Juggernaut is the exception at capacity 1.
- **Speed by armor class [Change]:** Light is fast and Heavy slow [Built], using the speed bands below. Shielded medium and the faster Raider remain [New]. Every unit moves at its definition's speed; mixed-selection synchronization belongs to [forces.md](forces.md) and is not built here.
- **No friendly fire.** Splash only hits enemies; players can't steer units away from it, so friendly fire would feel unfair.
- **Unit abilities:** every unit has passive traits. Some also have **auto-cast abilities** that fire on a published rule, e.g. *shield bash when an enemy is in melee range*. The player never casts them.

## Roster [New] — decided

Each unit's Human and Machine display names live in [World.md](../World.md#unit-roster). The labels below are the plain role names used in design docs and in the role line under each name.

| # | Unit | Armor | Damage | Job |
|---|---|---|---|---|
| 1 | Brawler [Built] | Heavy | Kinetic, melee | **A wall, not a killer:** holds the line and catches Light backline units in melee (today's Frontline; balance tuning pending) |
| 2 | Rifle [Built] | Light | Piercing, mid range | Steady damage against Heavy (today's Ranged; balance tuning pending) |
| 3 | Artillery [Change] | Light [Built] | Demolition [Built] + splash [New], long range | Breaks buildings; clump damage is [New] (today's Siege) |
| 4 | Lancer | Shielded | Piercing beam | Durable assault unit against Heavy |
| 5 | Scrambler | Light | EMP | Strips shields; an auto-cast pulse stuns buildings |
| 6 | Raider | Light, fast | Kinetic | Prefers Drill Rigs and isolated targets, captures quickly, cuts supply chains |
| 7 | Repair crew | Heavy | none | Auto-repairs units and buildings |
| 8 | Shield projector | Shielded | Kinetic, weak | Projects shields onto nearby allies |
| 9 | Juggernaut | Heavy | Demolition, melee | Elite, capacity 1. **Breaks fortified positions** that Artillery can't reach safely: immune to stuns and slows, takes −50% damage from Turrets. **Cut from launch if it fails the duel matrix.** |

Answers to each armor class, as players will actually experience them:

| Armor class | Answered by |
|---|---|
| Light | **Kinetic:** a Brawler in melee (it holds the line but is too slow to chase a Raider), Raider, Turret. **Splash:** Artillery against clumped Light squads. |
| Heavy | Rifle, Lancer (Piercing) |
| Shielded | The Scrambler pulse strips shields so anything can finish the target; also the EMP branches Rifle *Ion Rounds* and Artillery *EMP Shells* |
| Structure | Artillery, Juggernaut (Demolition) |

**Who beats whom.** This is the target matrix for the harness. No two units may have each other as prey; that mutual pair is what broke the first stat sheet.

| Unit | Prey | Predator |
|---|---|---|
| Brawler | Artillery, Scrambler (catches the backline in melee) | Rifle, Lancer |
| Rifle | Brawler, Juggernaut | Artillery (splash) |
| Artillery | Clumped Rifles, structures | Brawler, Raider (dive) |
| Lancer | Brawler, Juggernaut | Scrambler plus any damage |
| Raider | Drill Rigs, Artillery, isolated support | Turret, a Brawler holding the region |
| Juggernaut | Turrets, buildings | Rifle, Lancer |

Scrambler, Repair crew and Shield projector are **support**. They are tested in compositions, not one-on-one duels.

## Stat profiles — decided shape, starting values

**Current stats [Built]:** [Build/Content/units.json](../../Build/Content/units.json) is the text source for every existing unit stat; `GenerateMatchContent.py` writes those values into the cooked data assets. Stable catalogue IDs and asset names remain frontline/ranged/siege. The retag preserves the serialized HP, damage, range, interval, cost, duration, capacity and configuration fee; only armor, damage type, speed and role display names change. The proposed numbers below are not yet applied.

**Authored tags [Built]:** the generator accepts only the real role, armor and damage-type names. Hidden `Unset` defaults and unknown names are rejected before any asset is written.

**Rules:**
- **Cost:** a combat squad costs about 120 Power to fill, support squads cost less, and the Juggernaut about 2×.
- **Range bands:** melee 175, short 300, mid 550, long 1150. Branches move a unit up or down a band.
- **Speed bands by armor class [Change]:** slow 360 (Heavy) and fast 480 (Light) are [Built]; medium 420 (Shielded) and very fast 560 (Raider) are [New].
- **Auto-casts at tier 1:** only the specialists (Scrambler, Repair crew, Shield projector). Branches add auto-casts to other units, e.g. the Warden's taunt.

Starting values for tuning in the harness duel matrix:

| Unit | Squad | Cost per unit | Full squad | HP | DPS | Range | Speed | Auto-cast |
|---|---:|---:|---:|---|---:|---|---|---|
| Brawler | 6 | 20 | 120 | 150 | 10 | melee | slow | — |
| Rifle | 5 | 24 | 120 | 90 | 24 | mid | fast | — |
| Artillery | 3 | 40 | 120 | 100 | 22 splash | long | fast | — |
| Lancer | 3 | 45 | 135 | 110 + 80 shield | 30 | mid | medium | — |
| Scrambler | 3 | 35 | 105 | 80 | 12 EMP | mid | fast | EMP pulse every 10 s: strips shields, stuns buildings 3 s |
| Raider | 4 | 25 | 100 | 85 | 16 | short | very fast | — (passive: 2× capture speed) |
| Repair crew | 2 | 40 | 80 | 160 | — | heals at 400 | slow | Heals the most damaged ally, 30 HP/s |
| Shield projector | 2 | 45 | 90 | 90 + 120 shield | 6 | short | medium | 100-point regenerating shield bubble on allies within 500 |
| Juggernaut | 1 | 260 | 260 | 1600 | 90 Demolition | melee | slow | — (passive: stun and slow immunity, −50% damage from Turrets) |

**Sanity check on the starting values (squad against squad, before range and splash):**
- **Rifles beat Brawlers.** Rifles do 180 effective DPS against 900 HP, so they kill the squad in 5.0 s, and they get about 1 s of free fire while Brawlers close the distance. Brawlers do 90 against 450 HP, so they need 5.0 s plus that 1 s.
- **Lancers beat Brawlers.** 135 effective DPS against 900 HP takes 6.7 s; Brawlers need 9.5 s to kill 570 HP (shield included).
- **No squad leads on both HP and DPS per Power.** Brawler: 7.5 HP and 0.5 DPS per Power. Rifle: 3.75 HP and 1.0 DPS per Power.
- These are paper numbers. **Build the harness duel matrix before committing any of them.** Today `MatchSimulationSubsystem` only reports Frontline/Ranged/Siege totals.

Production time per unit stays roughly proportional to cost, as today (1/6 to 1/7.5 s per Power).

## Branches and masteries — decided starting set

Two branches per unit; each branch is a card ([cards.md](cards.md)). Each branch has one tier-3 mastery. A mastery adds a **new rule or auto-cast**, not just bigger numbers, so it reads as a moment in the fight.

| Unit | Branch A | Branch B |
|---|---|---|
| Brawler | Breacher: Piercing melee, anti-Heavy | Warden: +HP, auto-cast taunt while holding a region |
| Rifle | Marksman: +range | Ion Rounds: EMP damage, anti-Shielded |
| Artillery | Demolisher: +Structure damage | EMP Shells: EMP splash |
| Lancer | Overcharge: the beam pierces through a line of enemies | Bulwark: bigger shield, slower |
| Scrambler | Jammer: longer building stun | Leech: drains enemy shields onto allies |
| Raider | Saboteur: Demolition against Drill Rigs | Outrider: captures 3× faster, weak in a fight |
| Repair crew | Field Medic: heals units in combat | Engineer: repairs buildings and auto-fortifies anchors |
| Shield projector | Dome: bigger radius | Reflector: reflects part of the damage taken |
| Juggernaut | Siege Walker: long-range Demolition | Crusher: melee trample splash against Light |

| Branch | Tier-3 mastery |
|---|---|
| Brawler · Breacher | **Wrecking Crew:** hit targets take +20% damage from everything for 5 s |
| Brawler · Warden | **Bulwark Line:** the taunt pulls all nearby enemies; −30% damage taken while taunting |
| Rifle · Marksman | **Overwatch:** the first shot at each new enemy entering range deals ×3 |
| Rifle · Ion Rounds | **Chain Ion:** EMP shots arc to 2 nearby Shielded enemies |
| Artillery · Demolisher | **Bunker Buster:** ×2 against structures, ignores Shield Generators |
| Artillery · EMP Shells | **Blackout Shells:** hits disable Turrets for 4 s |
| Lancer · Overcharge | **Pierce Through:** the beam carries on into structures behind |
| Lancer · Bulwark | **Aegis:** when its shield breaks, nearby allies get a 3 s shield |
| Scrambler · Jammer | **Signal Jam:** each pulse delays JEV's committed plan in its region by 5 s |
| Scrambler · Leech | **Siphon:** drained shields also heal HP |
| Raider · Saboteur | **Demolition Charge:** a hit Drill Rig goes offline for 20 s |
| Raider · Outrider | **Flag Runner:** instant capture of regions with no hostiles inside |
| Repair crew · Field Medic | **Revive:** every 30 s, saves a dying ally nearby at 30% HP |
| Repair crew · Engineer | **Field Fortification:** a region the crew holds gains the Cover trait |
| Shield projector · Dome | **Fortress Dome:** the bubble blocks all long-range splash |
| Shield projector · Reflector | **Mirror Field:** reflects 30% of absorbed damage back to the attacker |
| Juggernaut · Siege Walker | **Earthshaker:** shots stun structures for 2 s |
| Juggernaut · Crusher | **Rampage:** each kill restores 10% HP and gives +10% speed, stacking up to 3 |

**3rd branches** are Rare or Legendary cards that open **odd hybrid roles**. There are 3 at launch, each tied to a commander theme; more come as unlocks ([meta.md](meta.md)).

| 3rd branch | Unit | Theme | Role |
|---|---|---|---|
| Pioneer | Brawler | Line Cutter | Builds forward Conduits in held regions, extending supply |
| Hacker | Scrambler | Wiretap | *Ignore Previous Instructions:* briefly turns one enemy unit to your side |
| Salvager | Repair crew | Quartermaster | Enemy deaths nearby pay Power into the team pool |

Groundbreaker's 3rd branch comes in the first unlock batch.

## Faction twists — decided

| Faction | Twist |
|---|---|
| Humans | **Repair and salvage:** units repair near friendly buildings, and dead units refund part of their cost in Power |
| Machine | **Regenerate and overclock:** units regenerate slowly, and briefly overclock (+attack speed) while at full HP |

## Acceptance check

[Built] The combat duel harness runs every ordered pair of runtime combat definitions, including mirrors, in verified open ground on the requested map. Fresh squads use real Attack combat, without JEV, production, income, capture, HQ targets or Workshop specializations; each fight ends on a wipe, its game-time cap or an invalid stall. Seeds vary spawn jitter and orientation. [New] Support composition scenarios remain required before support stats are committed.

- [Change] **Combat units:** each wins ≥65% against its prey and ≤35% against its predator, following the who-beats-whom matrix. [Built] The report combines both ordered sides against the opponent; draws remain in the denominator and are not half-wins.
- [New] **Support units:** tested in compositions, e.g. Scrambler + Rifle against Lancer at equal cost. Adding the support unit must raise the win rate against its target by ≥20 points.
- [Change] Mirror matches are 50% ±5%. [Built] The report checks each side's win rate separately and exposes side bias; draws remain in the denominator.
- [Built] **Side-order neutralisation:** odd seeds create team 0 first; even seeds create team 5 first. Left/right identities remain team 0/team 5, and each fight records the first-created team. An odd-sized or parity-unbalanced seed set is not a fully balanced ordering sample.
- [Change] No full squad of one type is worth more than 1.25× another at equal cost, and **no squad leads on both HP and DPS per Power**.
- [Built] **Operational budget:** 120 Power per side, whole units only (`floor(120 / unit cost)`); one-time configuration fees are excluded. Unspent remainder is not converted into units or damage. The report records actual spent Power and survivors' full unit-cost value, not HP-weighted value.
- [Built] **Operational worth:** each side's duel score is `(1 + own surviving Power / own spent Power − enemy surviving Power / enemy spent Power) / 2`. A unit's worth is its mean score over all non-mirror opponents, both ordered sides and seeds. The roster rule passes when maximum worth / minimum worth ≤1.25; zero minimum worth fails. This is an overall roster comparison, not a restriction on counter-matchup margins.
- [Built] **Definition check:** HP per Power is maximum HP / unit cost; DPS per Power is damage / attack interval / unit cost. Strictly leading on both metrics fails; a tie on either does not.
- [Built] **Stall validity:** when neither side removes HP while both sides survive for `max(30 seconds, 10 × the slower weapon's attack interval)`, the fight is `stalled` and the matrix is invalid, not a draw. A wipe takes precedence. Stall telemetry is retained outside the accepted duel rows; no rule may pass on that incomplete matrix.
- [Built] **Seed geometry:** member spacing is 160 cm with independent seeded offsets of ±40 cm per horizontal axis. This avoids overlap at the jitter extremes. Wider offsets and alternating creation order do not prove independent samples.
- [Built] **Cap default:** duel fights default to 300 game seconds; ordinary matches retain their existing cap. An explicit cap overrides the duel default. Cap outcomes remain censored draws only when the runtime has not detected a stall.
- [Built] Every combat rule is reported as pass/fail; missing or invalid seed matrices cannot pass. Failed balance rules are measurements, not invalid engine runs. Support compositions are explicitly unmeasured. Procedures: [`./x help sim`](../../x); dated measurements and report interpretation: [Balance.md](../Balance.md).
