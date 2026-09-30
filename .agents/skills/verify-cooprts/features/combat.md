# Combat and retreat

A fresh Boot has two HQs, three sectors, no friendly forces/buildings and an economic enemy commander. Completed barracks maintain locked Frontline/Ranged/Siege forces with 6/4/2 slots and individual paid casualty recruits. Players direct combat through the producer's Secure/Defend/Fall Back front, not squad micro. Travelling recruits remain attackable/capture occupants but cannot fire or receive stationary-Defend mitigation before joining. Role ranges/damage, health/death, pursuit limited to 1050 from destination and HQ weapon damage remain authoritative. Enemy HQ has no shield/unlock prerequisite. Internal attacks and Move/Hold/Retreat remain for gameplay/fixtures. Source: `ArmyGroup.cpp`, `ArmyUnit.cpp`, `CommandBuildingProduction.cpp` and `Headquarters.cpp`.

## Live proof and its fixture boundary

`regression --scenario combat` (`CoopRTS.Combat.Encounter`) creates legacy-composition fixtures and removes the enemy planner for a bounded encounter. It checks actual hits/role damage, escape/death, pursuit and internal orders, not current production/HUD fronts. Use `production` for paid forces/physical recruitment, `strategy` for enemy construction/recovery, `match-win`/`match-loss` for weapon-caused outcomes, and `doctrine-frontline` for stationary Defend mitigation. Select affected paths; standalone Success does not prove native input or replication.

## Targeted current-package encounter

Build and complete a barracks, choose a type, Start & Lock and assign Secure to an observed hostile area. Observe physical recruits joining, force approach, actual shots, falling HP and effects over multiple weapon intervals. Replace its front with Defend/Fall Back and inspect response without changing another producer's force. Follow using the camera-only minimap. Accepted feedback is not proof of damage, recruitment arrival or disengagement.

If members die before a front replacement, it cannot prove their disengagement. Inspect the surviving roster/positions and `LogArmyOrders`; individual pursuit/arrival assertions require live state. A wipe preserves producer force/front for paid rebuilding; recruits must still walk and join. Earlier manual Q/H/R and fixed-starting-army captures are historical.
