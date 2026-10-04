# Map and territory

> Part of the [Post-Frontier design](../Design.md). Related: [economy](economy.md), [fog](fog.md), [jev](jev.md).

## Territory rules [Built unless noted]

- Habitable Zone v2 has 15 polygon regions. Its asset is still `AvailabilityZoneV2`, and today's menu still calls it Availability Zone v2 ([World.md](../World.md#names-in-code-and-assets)).
- **Main regions:** belong to their team while its HQ lives and cannot be captured.
- **Every other region:** follows its capture anchor.
  - Radius 430 cm, rate 0.125/s: 8 s to take a neutral anchor, 16 s to flip an enemy one.
  - Capture makes no progress when both sides are present or when nobody is, and nothing locks a capture. Only units that belong to a force count.
- **Contested:** any hostile unit inside the region's polygon. You can only build in regions your team controls that are not contested.
- **[Change]:** income needs a connected chain ([economy.md](economy.md)).

## Maps [New] — decided

- **Three handcrafted maps at launch, one per act biome.** Candidates from World.md: Server Moon, Smart Hab, Cold Storage. Habitable Zone v2 is the greybox reference.
- **Organic region shapes stay — decided 2026-10-02.** Borders can follow terrain, and a region with 2 neighbours next to one with 7 makes the supply necks that give cuts their meaning. A hex grid gives every region 6 neighbours, so it was rejected. Shipped roguelikes keep a fixed layout and vary what's in it (Spelunky, Into the Breach, Risk of Rain 2). Research: [map-structure.md](../Research/map-structure.md).
- **Randomized variants:** each map keeps its geography, but every battle shuffles deposit layout, region roles and traits, objective sites, neutral placements and JEV's starting regions. A variant is generated from the run seed and shown on the node before you pick it.
- **Fairness rules for the shuffle** (starting values):
  - Deposits are placed by supply-chain hops from the main, not by world position: normal deposits 1 hop out, rich ones 2–3 hops.
  - No two rich deposits in neighbouring regions, and none next to JEV's start.
  - JEV starts from a small set of authored directions per map, at least 3 hops from the players' main.
  - A seed checker rejects bad seeds before they're offered. Batch-check about 1,000 seeds per map and compare histograms (hops to the nearest deposit, JEV distance, supply necks) to catch hidden bias.
  - Each map keeps one fixed beginner seed.
- **Link toggles:** 2–4 designed toggles per map open or close links between regions, e.g. a bridge out or a pass open. They change the supply graph, which is what makes a known map play differently. Cosmetic changes don't count as variants.
- **Failover Node sites:** each map authors two per side, in regions next to each main ([battle.md](battle.md)).
- **Player count:** the map stays the same; JEV scales instead ([jev.md](jev.md)).

## Bases [Change] — decided

- One **shared HQ** that the team defends together.
- Each commander gets **their own build zone** in the main region, so nobody blocks a friend's space. Placement inside a zone is free grid, as today.
- Captured regions outside the main stay open to everyone.

## Region size and defend posts [Built unless noted]

- Every shipped region carries **2–3 authored defend posts** on walkable ground. They are map data on the server and clients, and show as small ground markers in the local team's regions.
- **Stretch limit [Built]:** every buildable spot must be within **35 m** of a defend post (starting value: about 8 s at today's 4.2 m/s). A region that needs more than 3 posts is too stretched and gets split or reshaped. The map checker enforces both rules.
  - Coverage checks both free-building footprints on their placement-grid phases, with the arena's centre margin and HQ clearances. Inscribed-circle obstacle checks conservatively admit clear footprint boxes; navigation probes, changing ownership and transient blockers are omitted rather than hiding uncovered placements.
- **Habitable Zone v2 check (2026-10-02):** measured from the single capture point, a region's farthest point is 48 m away on average, and 75 m in East Spur. An automatic placement estimate brings every region inside 35 m with 2–3 posts each, 35 in total, so no region needs reshaping.
- **[Built]:** holding forces wait at these posts ([forces.md](forces.md)).

## Region traits [New] — decided

A region can have one trait, shown as an icon on the region and the minimap:

| Trait | Effect on units inside |
|---|---|
| High ground | +range |
| Cover | −damage taken |
| Open | +speed |
| Hazard | damage over time |

Traits make *where* to fight a decision, and they combine with Move & Hold.

**[Change] Built in step 1b** on fixed regions of Habitable Zone v2, with terrain to match each trait: raised plateaus with ramps for high ground, visible cover, and narrow necks giving 2–3 routes between fronts. The seed shuffles traits from step 4 ([build-order.md](build-order.md)).

**Region traits were chosen over map events for launch.** The review asked for one or the other, and traits are static, readable and shape strategy every battle.

## Neutrals [New] — decided

- **Neutral structures** to capture:
  - **Watchtower:** intel, like a Relay Tower. It reveals regions in its radius, including unit types.
  - **Data Vault:** pays a lump of Data once captured.
  - **Generator:** a Power trickle while held and connected.
- **[Later] Rogue drones** guarding reward regions and neutral structures. Cut from launch scope.

## Fog of war

Fog of war and reveal rules live in [fog.md](fog.md).

## Map events [Later] — cut from launch scope

Scheduled events announced 30 s in advance, e.g. a *Power surge* doubling a deposit for 60 s, or a *Storm* slowing a region. Deferred in favour of region traits.
