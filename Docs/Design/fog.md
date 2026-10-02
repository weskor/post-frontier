# Fog of war and reveal [New] — decided; build after the step-1 playtest

> Part of the [Post-Frontier design](../Design.md). Related: [map](map.md), [ui](ui.md).

There's no fog today; both sides see everything. The target is **region-based visibility with blips beyond**. You always know *where* trouble is next to you; learning *what* it is takes intel. Reveal comes from strategic actions, never from steering a scout around.

**Always known:** geography, region outlines and traits, deposits and neutrals. The map variant is previewed on the node anyway ([map.md](map.md)). Fog hides only JEV's units and buildings.

**Visibility levels:**

| Level | Where | What you see |
|---|---|---|
| **Visible** | Regions your team holds; regions where any of your forces stands; the radius of Relay Towers and held Watchtowers; regions revealed by abilities | Everything: units, types, sizes, buildings |
| **Blips** | Neighbours of any visible region | A pulsing radar mark per JEV group with a size band: *few* (1–4), *some* (5–10), *many* (11+). No types. |
| **Last seen** | Everywhere else | JEV buildings stay as ghosts until a reveal proves them gone. Unit marks fade after 20 s. |

**Reveal sources:**
- Holding a region.
- Any force standing in a region. Pushing forward extends the picture, and a fast Raider is a natural scout without micro.
- Relay Towers and held Watchtowers, within their radius.
- Abilities, for their duration:
  - the Wiretap passive and *Jam*,
  - Scrambler and Raider perks.

**Other rules:**
- **Being shot does not reveal the attacker.** An enemy firing from a blip region stays a blip, but hit flashes show its damage type ([ui.md](ui.md)). You learn a little by taking hits.
- **Team vision is shared.** All commanders see the same picture.
- **JEV plays under the same fog.** It plans on what it can see, so hiding, decoys and *Prompt Injection* work.
  - Today `AEnemyCommander` reads the true state: hostile counts per region, total human unit count and total human income (`EnemyCommander.cpp`).
  - It needs a perceived-state layer: visibility per region for team 5, and estimates in place of exact human totals.
- **JEV's published plans under fog:** the **target region and ETA are always shown**. The plan's **size and composition** are shown only if its source region is visible.
- **Look:** fogged regions are desaturated and dimmed, with a scan-line edge. Geography stays readable, and blips render as pulsing radar marks. The war table and minimap use the same three levels.
