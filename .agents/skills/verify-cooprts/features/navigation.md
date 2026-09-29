# Group crossing and obstacle navigation

## Sub-features

Two independently commanded six-member formations navigate around one another and the central obstacle using Unreal navmesh and built-in Detour crowd avoidance, with Character RVO disabled. A paid casualty replacement joins an already-moving group near a valid home source before the long crossing. Retreat returns each army to its own home. Source: `ArmyGroup.cpp`, `ArmyUnit.cpp`, `ArmyMovementTests.cpp`, and the generated arena in `Build/GenerateCommandMap.py`.

## Primary proof and selection

Run `"$V" --run "$LOGIC_RUN" regression --scenario movement` first with the current editor build. `CoopRTS.Movement.TwoGroups` checks crossing, obstacle traversal, eight replacements per group, rejected requests preserving motion, purchased casualty joining a live Move, Hold, and every restored member's latest arrival. Successful path completion, proximity and velocity are checked together; idle alone is not success. The scenario has a 90-second deadline and the wrapper allows 150 game seconds. Require exact Success and exit zero; retain `movement.log` and `movement-stdout.log`.

This run covers moving-newcomer path and Hold behavior; `economy` covers purchase/source/payment/role/rebuild invariants. Desktop probes below cover changed route presentation, arena geometry or input integration. For a targeted pathfinding correction, choose a representative changed route rather than repeating the entire automated crossing/boundary matrix in pictures.

## How to get to it (user POV)

Begin near the cyan home marker with the army selected. Zoom out and pan to see ground on the opposite side of the central rectangular obstacle. Do not regenerate the map for verification.

## Targeted desktop probes

1. Send `key 1`, `key space`, then positive scroll ticks until enough of the arena is visible. Capture `navigation-home`.
2. Pan forward with a short `key w --hold <milliseconds>` and capture `navigation-target`. Choose clear floor beyond the far edge of the central obstacle. Use the screenshot, not a canned click position; the obstacle top is not the destination.
3. Right-click that observed point. Capture intermediate movement and the accepted destination in the HUD/log. Allow traversal, then capture `navigation-arrived` and count all six units near the destination ring. The obstacle must lie between the starting marker and destination; a same-side move does not cover this feature.
4. Send `key r`. Capture the return while moving. Once settled, send `key space`, capture `navigation-returned` and count all six units at home. Confirm the destination changed to home and the order is Retreat.
5. For the two-group crossing, return both groups home first. Zoom out until both formations are visible. Order army 1 to army 2's observed starting area, switch with `key 2`, and order army 2 to army 1's original area. Capture the exchange in progress and after arrival. Count every member of each army; no permanent jam or stranded unit is acceptable.
6. Order both groups to distinct clear areas beyond the central obstacle, switching selection without stopping either group's orders. Inspect intermediate routes and both final formations. Their destinations must have enough spacing for two formations, rather than asking both to occupy identical slots.

## Gotchas

Dynamic navmesh generation can briefly reject orders immediately after load. Wait for the initial arena to settle, doctor, and retry one observed reachable request; do not hide persistent rejection behind unlimited retries. Slot projection is deliberately tight: a navigable center near a wall can still be rejected when the whole formation will not fit. A rejected request must leave the serial, destination, and existing unit movement intact. The scenario covers selected crossing and boundary cases, not arbitrary crowd density, every passage, or multiplayer.

The obstacle's dark-blue shadow is walkable floor, not the raised obstacle itself. For a rejected obstacle click, target the light-colored raised surface and require rejection feedback with unchanged order serial/destination. A move accepted on its shadow is correct behavior. The per-unit scenario requires successful path completion as well as proximity, idle status, and low velocity; idle after a blocked move is not success.
