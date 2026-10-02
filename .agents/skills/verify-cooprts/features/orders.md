# Barracks fronts and internal force orders

Players command each persistent force through its own barracks: Secure attack-moves, Defend guards an area, and Fall Back regroups at the chosen front. Replacing a barracks front updates only that force and its travelling recruits; another producer's force/front is untouched. Wiped forces retain front and identity; destroyed-producer survivors keep their last front without receiving replacements or transferring. Squad selection and manual Tab/Q/H/R/right-click orders are removed. Internal Move/Attack/Hold/Retreat APIs remain for combat and explicit fixtures; rejected orders preserve accepted intent/serial. Source: `CommandPlayerController.cpp`, `CommandBuildingProduction.cpp`, `ArmyGroup.cpp`, `ArmyOrderTests.cpp`, `ConstructionTests.cpp`.

## Live proof selection

`regression --scenario orders` (`CoopRTS.Orders.ReplaceHoldRetreat`) exercises controlled fixture orders: replacement approach, Hold, rejected boundary request and center retreat. It does not prove every member arrived or current human controls. `production` checks paid building-owned forces, producer-scoped fronts and recruitment that follows a moving force to physical arrival. Add `combat` for changed attack precedence and `movement` for changed fixture crossing. Use fresh worlds with exact Success and SoftQuit markers.

## Targeted native proof

Complete a barracks, choose its force type and Start & Lock, then click Secure, Defend or Fall Back followed by navigable world ground; the targeting cursor is a 150×150 cm square, not a snapped building footprint. Compare accepted fronts/serials with real travel. Give a second barracks a separate front and verify it is unaffected. Click an owned living unit to select its living producer remotely, compare force numbers and building→force-centre→front links, then change that producer's front without changing the other. Observe an individual paid replacement walking to the moving force after a casualty. Right-click or Escape during targeting cancels; right-click outside a mode issues no squad order. Minimap clicks pan without committing placement/fronts. Tab/Q/H/R must not replace automatic orders. Orphan clicks clear selection with destroyed-barracks feedback rather than granting new front control.

The `orders` scenario's center-distance check is weaker than the `movement` scenario's every-member path success/arrival. The historical six-member packaged probes and `key 1`/`key 2` recipes are not valid for a new construction game.
