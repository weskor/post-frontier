# Camera

## Sub-features

WASD pan, normalized diagonal input, middle-button ground-plane drag, discrete wheel zoom, Space building/HQ focus and clickable live minimap. Source: `CommandCamera.cpp`, `CommandMinimap.cpp`, `CommandHUD.cpp` and `CommandPlayerController.cpp`.

## Proof selection

Camera and cursor behavior require actual native input and inspected rendering; there is no existing numeric camera regression. Exercise only affected controls below for a targeted change, all relevant controls for a baseline. Retain a before/after pair for each distinct visual claim; share a baseline when geometry is unchanged. Unchanged combat/navigation assertions are not a substitute and need not be repeated for a camera-only change.

## How to get to it (user POV)

Launch a current package. Space focuses a selected owned building or friendly HQ. The bottom-left minimap shows HQs, living buildings/units/squads, sectors, contested markers, the selected barracks front and the actual camera ground footprint; left-click pans. World +X is map up and +Y is map right. It remains visible in hidden/placement/front modes and never sends a gameplay command.

## Targeted desktop probes

1. Capture `camera-before`. Send `key w --hold 350`, capture `camera-pan`; stationary world geometry must move relative to the viewport. Check A/S/D similarly for changes to pan mapping. Opposite keys should reverse their direction; a pan alone must not issue an army order.
2. Send `key space`: with no selection HQ returns to center, with an owned building selected that building returns. Click distinct minimap locations and inspect changed world landmarks and footprint. Repeat during placement/front targeting and with the deck hidden; clicks must move only the camera.
3. Send `drag 100 50`, capture `camera-drag`; the same ground landmarks follow the pointer motion, rather than moving only a tiny sensitivity-scaled amount or reversing direction. The 1-pixel initial pointer motion is for compositor delivery, not a camera pan.
4. Send `scroll 3`, capture `camera-out`; units/landmarks become smaller. Send `scroll -3`, capture `camera-in`; they become larger. Space restores center, not zoom.
5. For zoom-limit changes, send repeated `scroll 12` commands, capture before/after an additional positive tick, then test the negative limit with repeated `scroll -12`. Scene scale must stop changing at each limit. For pan-limit/diagonal changes, exercise those explicitly; the basic pan smoke does not prove their numeric bounds.
6. For cursor/capture changes, capture the arrow, then use unwarped `move -200 0` / `move 200 0` within the window. Inspect each side, middle-drag and release, then move again. Cursor must remain visible/free; camera gestures must not issue squad orders. Right-click outside targeting mode has no gameplay effect.

## Gotchas

Desktop tiling can override requested size. Use doctor geometry/scale rather than screenshot pixels. Drag near the viewport center. Distinguish camera movement from squad movement using stationary landmarks and automatic-front state. Offscreen shared HUD clicks can prove minimap coordinate mapping and camera-only dispatch, but native pointer delivery needs a separate guarded window run.

The RTS controller deliberately uses `EMouseLockMode::DoNotLock`. On this Linux/Wayland desktop, forced viewport confinement caused the cursor to stick at the window edge. The pointer may leave the window; the verification driver's focus guard must remain enabled.
