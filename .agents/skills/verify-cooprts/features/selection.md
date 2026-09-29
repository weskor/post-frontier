# Army selection

## Sub-features

Left-click one owned unit to select its group; left-click empty ground to clear selection; 1 and 2 select the corresponding owned army even if its roster is empty; Space focuses the selected army. Cyan rings and the HUD expose selection, while an empty owned army remains identified by its army number, base and rebuild quote. The second army has a lighter team-color tint. Source: `CommandPlayerController.cpp`, `CommandHUD.cpp`, and `ArmyUnit.cpp`.

## Proof selection

Selection needs actual input and inspected selection indicators; no dedicated live selection scenario currently covers all entry points. For a targeted change choose the affected mouse/hotkey/empty-selection probes; a baseline covers both input routes and independent armies. Keep captures showing distinct selection states, not duplicate frames between every command. Order logs can show an unintended command, but cannot prove the correct selection rings rendered.

## How to get to it (user POV)

In the packaged arena, start with army 1 visible. Use 1 then Space to restore this state after camera checks. Army 2 starts 1000 Unreal units farther along negative X and has its own home and orders; use 2 then Space to find it.

## Targeted desktop probes

1. Capture `selection-before`. From the image choose empty floor away from HUD, units and obstacles. Send `click left --x <fraction> --y <fraction>` with the observed fractions. Capture `selection-empty`: all cyan unit selection rings disappear and the HUD says no army selected.
2. Choose the body of one visible unit, not a home-marker disc or its shadow. Click it. Capture `selection-unit`: rings appear around every member of that army, and the HUD shows its correct army number, unit count, and order.
3. Clear selection again, then send `key 1`. Capture `selection-hotkey`: army 1 is selected without a unit click. Send `key 2`, then `key space`, and capture `selection-army-two`: the HUD reads Army 2 and the other formation is centered.
4. Click a member of each group and confirm the HUD switches between Army 1 and Army 2. Selection must not issue an order or select both armies together.
5. Pan away, send `key space`, capture `selection-focus`: the selected army is centered. When selection is empty, Space must not acquire an army implicitly.
6. If a real encounter wipes an owned army, press its 1/2 hotkey and inspect its `EMPTY`/base-only rebuild quote; clicking a nonexistent unit cannot recover selection. Do not claim packaged empty-selection proof unless a wipe actually occurred; `economy` asserts the persistent group and owned rebuild path in a live Boot world.

## Gotchas

The space between formation members is ground, not a unit hit. Do not call hotkey selection proof of mouse selection; exercise both entry points. Both armies belong to one local player; this scene does not prove enemy/other-player selection rejection or multiplayer ownership. The white outline marks the selected army's home; the older filled map discs are not separate controllable groups.
