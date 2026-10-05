# Open questions

> Part of the [Post-Frontier design](../Design.md). Related: [battle](battle.md), [jev](jev.md), [map](map.md).

1. **Fog design is decided ([map.md](map.md)); building it waits for the step-1 playtest.** Open:
   - how accurate JEV's estimates of hidden human strength should be;
   - whether a decoy card set comes with it.
2. **Skill ceiling:** is there one once players have learned to answer JEV's intent? Check this in playtests.
3. **Battle length without a clock:** does the release schedule plus wave budgets ([jev.md](jev.md)) end stalemates by 10–12 min? AI-vs-AI matches last 23.6 min (median), yet the 2026-10-01 playtest was won by one unattended force at about 5 min ([battle.md](battle.md)). The harness must measure both ends: the median, and a scripted rush. Measure after each lever.
4. **Baseline income level:** 2/s or 1/s, with the halved deposit reserves? Measure in the 1b harness ([economy.md](economy.md)). Removing the baseline was rejected on 2026-10-03, because a commander with no income could never get back in.
5. **Two-commander threats:** Split-Brain Cut is **[Built]** ([battle.md](battle.md#two-commander-threat-built)), but its Fortify tuning window is one unit wide and holds on one authored pair only, so JEV holding either of that pair's regions disables it; watch both in the playtest and decide how to author more pairs (a composition per target region is one way). A second, ability-based threat stays open: design one that needs two commanders' abilities at once. Example: a Shielded, fortified relay that needs EMP *and* Demolition within 20 s.
6. **Fog and the Pattern Matcher:** under fog it reacts to the *most-seen* unit type. Define what "seen" counts.
7. **Content still to design.** First sets now exist for stats, auto-casts, masteries, perks, Doctrines, Protocols, calldowns, bosses, Events and scars. Still open:
   - boss phase scripts and boss maps (4);
   - the 3 act maps and their variant rules;
   - commander ability and ultimate numbers, including Data cast costs;
   - Groundbreaker's 3rd branch;
   - Commander cards for each pool;
   - the JEV personality behaviour scripts per release.
8. **Tuning:** every starting value in this document. That covers the ×1.5 counter bonus, the stat sheet, shields, Data rates and prices, upgrade costs, structure HP, the release schedule and wave budgets. Use the harness duel matrix and match simulations.
9. **Duel `roster_worth_ratio` (owner decision):** with the committed Lancer and Scrambler values the roster worth-ratio rule fails, and no Lancer or Scrambler stats can meet it under the prey rule ([Balance.md](../Balance.md#x1-adopted--committed-values-run-and-open-owner-decision)). Decide: relax the limit, or change which opponents the worth score counts ([units.md](units.md#acceptance-check)).
