# Camera

## Sub-features

WASD pan, normalized diagonal input, middle-button ground-plane drag, discrete wheel zoom, Space focus. Source: `CommandCamera.cpp` and input bindings/PlayerTick in `CommandPlayerController.cpp`.

## Proof selection

Camera and cursor behavior require actual native input and inspected rendering; there is no existing numeric camera regression. Exercise only affected controls below for a targeted change, all relevant controls for a baseline. Retain a before/after pair for each distinct visual claim; share a baseline when geometry is unchanged. Unchanged combat/navigation assertions are not a substitute and need not be repeated for a camera-only change.

## How to get to it (user POV)

Launch the arena. The local army is selected and focused automatically. Keep the game window focused; no editor viewport is involved.

## Targeted desktop probes

1. Capture `camera-before`. Send `key w --hold 350`, capture `camera-pan`; stationary world geometry must move relative to the viewport. Check A/S/D similarly for changes to pan mapping. Opposite keys should reverse their direction; a pan alone must not issue an army order.
2. Send `key space`, capture `camera-focus`; the selected army returns to the viewport center.
3. Send `drag 100 50`, capture `camera-drag`; the same ground landmarks follow the pointer motion, rather than moving only a tiny sensitivity-scaled amount or reversing direction. The 1-pixel initial pointer motion is for compositor delivery, not a camera pan.
4. Send `scroll 3`, capture `camera-out`; units/landmarks become smaller. Send `scroll -3`, capture `camera-in`; they become larger. Space restores center, not zoom.
5. For zoom-limit changes, send repeated `scroll 12` commands, capture before/after an additional positive tick, then test the negative limit with repeated `scroll -12`. Scene scale must stop changing at each limit. For pan-limit/diagonal changes, exercise those explicitly; the basic pan smoke does not prove their numeric bounds.
6. For cursor/capture changes, capture the visible arrow, then use several `move -200 0` and `move 200 0` steps within the observed window. Do not use coordinate-targeted clicks between steps: they warp the pointer and can mask confinement bugs. Capture each side, issue `click right --here` on clear ground, and inspect the pointer and destination. Send Hold, then compare captures around `drag 100 50 --here`; landmarks must follow the drag while the order remains unchanged. Finally use `move` again and confirm the cursor is visible and free after release.

## Gotchas

Desktop tiling can override requested 1600x900 size. Doctor reports actual logical geometry and scale; use those rather than physical screenshot coordinates. Drag from the viewport center to avoid cursor clipping. Do not confuse camera movement with army movement: compare the army against the home marker and check that the HUD order serial is unchanged. Screenshots are required; the headless order regression cannot cover this feature.

The RTS controller deliberately uses `EMouseLockMode::DoNotLock`. On this Linux/Wayland desktop, forced viewport confinement caused the cursor to stick at the window edge. The pointer may leave the window; the verification driver's focus guard must remain enabled.
