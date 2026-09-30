# Building selection and HUD access

A fresh Boot starts at the friendly HQ with no player squads/buildings. Left-click an owned building to expose its controls; clear world ground or a unit clears building selection rather than selecting a squad. Tab/Q/H/R have no squad-control bindings. Space focuses the selected building or friendly HQ. F4 hides/shows contextual controls, but Construction and the minimap remain accessible. Accepted placement restores building choices automatically. Drawing, panel blocking and clicks share one layout in `CommandHUD.cpp`.

## Targeted native recipe

Inspect a fresh current package, choose Barracks, click valid ground and confirm construction choices return without F4 or selecting the new building. Choose a second building immediately, then cancel. Hide the deck and click the persistent Construction button to reopen it. Select an owned building, inspect kind/level/HP/status and selection ring, and Space-focus it. Clear on non-HUD ground and Space-focus HQ. Unit clicks and Tab/Q/H/R must not select or manually order squads. Minimap clicks must pan without clearing selection, placing buildings or assigning fronts, including while a targeting mode is active.

Do not assume a fixed Army 1/Army 2, six starting roles, preselected unit or `key 1`/`key 2`. Those keys have no current controller binding and neither native driver accepts them. Old captures of Army 2's separate home and empty-group rebuild are historical; wiped groups are not revived with N. Avoid clicking HUD panels while attempting a world-selection/clear probe: panel clicks dispatch HUD actions instead.
