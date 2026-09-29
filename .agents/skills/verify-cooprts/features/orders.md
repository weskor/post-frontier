# Move, Attack, Hold and Retreat

## Sub-features

Right-click ground replaces Move; Q at a live hostile or on valid ground issues targeted/area Attack (see [Combat](combat.md)); H immediately holds; R retreats home. Accepted orders change the serial and destination display; rejected requests preserve accepted intent. Source: `CommandPlayerController.cpp`, `ArmyGroup.cpp`, `ArmyOrderTests.cpp`, `ArmyCombatTests.cpp`.

## Primary proof and selection

Run `"$V" --run "$LOGIC_RUN" regression --scenario orders` first with the current editor build. `CoopRTS.Orders.ReplaceHoldRetreat` exercises the owning controller in a real world: initial movement, replacement approach, individual Hold positions, rejected out-of-bounds serial preservation, and group-center return within 150 units of home. It does not prove every member arrived home. Use `movement` for individual arrival; add `combat` when Attack replacement or Retreat combat precedence changes.

The probes below are for changed controls, visible feedback or a complete baseline. For a targeted rule fix, rely on assertions and exercise only the relevant input transition instead of reproducing all stages as screenshots.

## How to get to it (user POV)

Select the local army with 1. Space centers it. Choose reachable floor in the current image; a valid formation needs space for every unit, not only the center.

## Targeted desktop probes

1. Capture `orders-before`. Right-click observed ground away from the group. Capture `orders-moving` while moving: HUD Move serial increments, destination ring appears, and units leave their original location.
2. Before arrival, right-click a different observed destination. Capture `orders-replaced`: another serial increments, destination changes, and units move toward the replacement rather than finishing the first route. Arrange both commands in one short sequence if tool-call latency would allow arrival first.
3. During movement send `key h`. Capture `orders-hold` and a second `orders-held-later` after at least one second. Positions relative to static ground must remain unchanged; HUD says Hold with a new serial. Do not substitute arrival at a Move destination for this check.
4. Send `key r`, capture `orders-retreat`, then wait for home arrival and capture `orders-home`. HUD says Retreat and the destination indicator turns orange. Confirm all six units return near the home marker.

## Gotchas

Log acceptance alone does not prove units moved or stopped. The HUD retains Move/Retreat after arrival; there is no separate idle order. Rejected ground requests can occur near obstacles/bounds even if the clicked center looks reachable, because every slot must have a complete path. Do not change that expectation without a product decision. The current single local controller does not prove network ownership checks under two players. For Q and combat transitions use the combat recipe and its separate live scenario.
