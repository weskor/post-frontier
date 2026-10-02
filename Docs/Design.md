# Post-Frontier — Game Design

Target design for the game, with the reasoning behind it. Agreed 2026-10-01 over about 25 rounds of design questions, then revised after an independent design review.

This file is the **index**. Each topic lives in its own file under [Design/](Design/); read only the file you need.

## How these docs work

- **Source code describes what is built today.** These files describe where we are going. [status.md](Design/status.md) maps one to the other, area by area. Where README.md or World.md disagree with source, source wins.
- **Each fact lives in exactly one file.** Other files link to it rather than copying numbers. When a number changes, change it in its home file only.
- **Cross-references are links to topic files**, never section numbers.
- **Research behind the decisions:** [Research/](Research/) covers roguelite RTS, co-op design, indirect control, replay and reward design, map structure, area defence, input, the battle opening, and LLMs in JEV. Measured balance data is in [Balance.md](Balance.md).
- **Setting, names and tone:** [World.md](World.md).

Status tags:
- **[Built]** matches source today.
- **[Change]** modifies something already built.
- **[New]** does not exist yet.
- **[Candidate]** is not committed; it gets decided after playtests.
- **[Later]** was designed but cut from launch scope.

## Topic files

| File | Covers |
|---|---|
| [run.md](Design/run.md) | Run structure: acts, the frontier-moon run map, node types, lives, scars, Event memos, saving, joining and leaving, carry-over |
| [battle.md](Design/battle.md) | One battle: length target, the opening (planning phase and pre-built kit), JEV's version releases, measured battle length, objectives, guarding the HQs (Failover Nodes, the hold) |
| [economy.md](Design/economy.md) | Power and Data, the Data budget, shared income, connected territory, deposits, gifting |
| [map.md](Design/map.md) | Territory and capture rules, maps, variants and their fairness rules, link toggles, bases and build zones, region size and defend posts, region traits, neutrals |
| [fog.md](Design/fog.md) | Fog of war: visibility levels, reveal sources, JEV under fog, how fog looks |
| [forces.md](Design/forces.md) | Force cap, steering forces: selection, the three orders and their rules, holding a region, orphans, reinforcement, force settings, barracks tiers and upgrades |
| [buildings.md](Design/buildings.md) | Building rules, the building list and costs, the HQ |
| [units.md](Design/units.md) | Counter system, armor and shields, roster, who beats whom, stat sheet, branches and masteries, faction twists, acceptance check |
| [jev.md](Design/jev.md) | The enemy: how JEV plays, matching start, waves, calldowns, published plans, how JEV decides (no runtime LLM), escalation, player-count scaling, personalities, bosses, difficulty |
| [commanders.md](Design/commanders.md) | The four commanders: kits, passives, abilities, ultimates, growth, solo primary and secondary |
| [cards.md](Design/cards.md) | Drafts and cards: offers, card types, rules, perk/Doctrine/Protocol lists, Salvage |
| [meta.md](Design/meta.md) | Progression between runs: unlocks, pacing, Terms of Service ladder, codex |
| [coop.md](Design/coop.md) | Co-op: ownership, communication and pings, interdependence, leaving, solo |
| [ui.md](Design/ui.md) | Controls, time and pause, camera, selecting and giving orders, the force bar, the build bar, controller plans, awareness (announcer, objective strip, alert key), JEV intent display, feedback and presentation |
| [open-questions.md](Design/open-questions.md) | Undecided questions and content still to design |
| [status.md](Design/status.md) | Current build vs target, area by area, from a source audit |
| [build-order.md](Design/build-order.md) | Implementation order with playtest gates, and the list of systems cut to 'later' |

## Pitch and pillars

**A co-op roguelite RTS for 1–5 players, tuned for solo and 2–3 friends.** You command forces and buildings, never individual units. It's Year 312 A.P.: you play the Unindexed, frontier mining colonies that JEV, a polite corporate AI that deprecated humanity three centuries ago, never got round to. A run is a branching map of short battles against JEV. Between battles you draft cards that change what your barracks and orders can do.

### Pillars

1. **Strategy, not micro.** Players decide *what* to field, *where* to commit and *when* to go. Units carry out orders obediently and predictably and never second-guess the player.
2. **Friends need each other.** Sharing a goal is not enough. Each commander changes the battlefield for the others, and some JEV threats need two commanders to answer.
3. **Every run is different, and you can see why.** Variety comes from commanders, drafts, battle objectives, JEV personalities and modifiers, not just bigger numbers.
4. **Fair, readable fights.** JEV shows its intent in advance, so a loss can be explained and the next attempt planned.
5. **Serious look, funny writing.** See [World.md](World.md).

The experiences we aim for, in order: **Fellowship, Challenge, Discovery, comic Narrative.**

### What the game rewards

| Rewarded | Never rewarded |
|---|---|
| Reading JEV's published intent and answering it | Actions per minute, reaction speed |
| A composition that matches the threat and is hard to counter | Massing one dominant unit type |
| Where and when to commit: timing, cutting supply lines, several fronts | Memorising a scripted enemy |
| Adapting to what the run offers | Forcing the same build every run |
| Interdependence: commander synergy, gifting, rescues | Racing teammates for deposits |
| Choosing risk on the route map | Grinding permanent stat upgrades |

**Decision budget:** aim for one meaningful decision per player every 20–40 s, which is 15–30 per battle. Possible decisions: a build, a lock, an upgrade, a perk, an order change, a retreat setting, an ability, a gift. Log orders and purchases per player per minute in playtests. A battle whose decisions all fall in minutes 1–3 has failed this budget.
