# CoopRTS HUD style guide

StarCraft 2-inspired HUD for the native Canvas UI (`Source/CoopRTS/CommandHUD.cpp`). This is a **design proposal**: nothing here is wired into `Source/`. How to build it is in [`IMPLEMENTATION.md`](IMPLEMENTATION.md); the pictures are in [`mockups/`](mockups/).

Reference mockups (1920×1080, rendered from HTML that uses the exported frame and glyph PNGs, so the art is the real art):

| Mockup | Shows |
| --- | --- |
| [`01-gameplay`](mockups/01-gameplay.png) | Main HUD: top strip, minimap placeholder, construct cards, portrait + force panel, command card |
| [`02-barracks-frontline`](mockups/02-barracks-frontline.png) | Barracks, Frontline 6-slot roster, per-unit reinforce (hovered), tooltip |
| [`03-barracks-ranged`](mockups/03-barracks-ranged.png) | Ranged 4-slot roster, producing, unaffordable states |
| [`04-barracks-siege`](mockups/04-barracks-siege.png) | Siege 2-slot roster, force complete |
| [`05-barracks-configure`](mockups/05-barracks-configure.png) | One-time permanent Frontline / Ranged / Siege choice |
| [`06-barracks-constructing`](mockups/06-barracks-constructing.png) | Under construction |
| [`07-placement`](mockups/07-placement.png) | Placement mode |
| [`08-enemy-intel`](mockups/08-enemy-intel.png) | Machine-skinned enemy intel: "Thinking…" line and a scouted enemy Barracks |
| [`09-victory`](mockups/09-victory.png), [`10-defeat`](mockups/10-defeat.png) | Terminal screens |
| [`11-style-sheet`](mockups/11-style-sheet.png) | Tokens, type ramp, button states, roster states, glyph set |

## 1. Principles

1. **World first.** Everything sits on the top edge or the bottom deck. The keep-clear zone for the battlefield is the whole area between the top strip and the deck (about 60 % of the screen height). Only two transient things may enter it: the placement ghost (which is world) and the terminal banner.
2. **Metal frame, glass inside.** Bevelled gunmetal frames with angled corners hold dark translucent panels. Colour is spent in three places only: the thin player-colour trim, state colour on bars and pills, and portraits.
3. **Plain word first, joke second.** Every label starts with the function word (`BARRACKS · DRILL SHED`). Themed tags from `Docs/World.md` live in tooltips, sub-labels and the mode bar. One joke per surface.
4. **Role and state never rely on colour alone.** Force type has a glyph and a word next to the colour; production state has a word next to the bar.
5. **The Machine is visibly a different skin.** Enemy information uses the pearl skin, cyan trim, mono readout and one red lens, so intel can never be mistaken for one of your own panels.

## 2. Reference size, scale and geometry

All numbers are **reference pixels at 1400×788**. The HUD scales uniformly to the viewport (`Scale = min(W/1400, H/788)`, clamp 0.75 to 2.5, as in the current `MakeLayout`). At 1920×1080 the scale is 1.371; text sizes below become ×1.371 real pixels. Frame textures are authored at **2 texels per reference pixel**, so they stay sharp up to scale 2.

> The working copy of `CommandHUD.cpp` currently authors at 1280×720 with `MaxScale = 1`. Convert with ×0.914 (1280/1400) or move the reference back to 1400×788 as `hud-deck-20260929/RESULTS.md` describes. See IMPLEMENTATION.md, section 8.

Layout grid: **4 px base**. Gaps 4 / 8 / 12 / 16. Screen margin 8. Gap between deck panels 6.

| Region | Rect (x, y, w, h) | Notes |
| --- | --- | --- |
| Top strip, left plate | 8, 0, 428, 52 | Commander, Power + income, Sectors |
| Top strip, centre plate | 500, 0, 400, 52 | Objective label + two HQ bars |
| Top strip, right plate | 964, 0, 428, 52 | Army, Specialization, Team chips |
| Minimap frame | 8, 568, 212, 212 | 176 px interior |
| Construct panel | 226, 568, 232, 212 | Header + 3 cards (52 px each) |
| Info panel (portrait, roster, stats) | 464, 568, 638, 212 | Content box 606 × 180 |
| Command card | 1108, 568, 284, 212 | 4 × 3 cells of 60 × 56, gap 4 |
| Tooltip | 1108, 568 − 6 − h, 284, 112–132 | Above the command card; only on hover |
| Feedback strip | 464, 542, 638, 22 | Above the info panel |
| Mode bar (placement, front targeting) | 464, 694, 638, 86 | Replaces the deck; the Construct header stays as a 232 × 40 restore button |
| Enemy intel panel | 990, 62, 402, 196 | Machine skin, below the right plate |
| Terminal banner | 390, 130, 620, 246 | About 17 % from the top, as today |

Frame thickness (border box, reference px) and 9-slice margins (texels): panel 16 / 32, minimap 18 / 36, portrait 14 / 28, button 7 / 14, roster slot 10 / 20, strip 4·20·14·20 / 8·40·28·40, Machine panel 18 / 36, banner 24 / 48, bar track 4 / 8. Full table: `frames/slices.txt`.

## 3. Type

Only fonts that already ship with Unreal (`Engine/Content/Slate/Fonts/`) are used, so there is nothing to license or download. None are OFL: Roboto and Droid Sans Mono are Apache 2.0. The mockup folder holds copies (`mockups/fonts/`) only so the HTML renders offline.

| Role | Font (Unreal file) |
| --- | --- |
| Headers, numerals, labels, buttons | **Roboto Bold Condensed** (`Roboto-BoldCondensed.ttf`), caps, tracked |
| Body, values, tooltips | **Roboto** Regular / Medium / Bold (`Roboto-*.ttf`, already the Canvas small font) |
| Machine readout only | **Droid Sans Mono** (`DroidSansMono.ttf`) |

| Style | Size | Font, tracking | Used for |
| --- | --- | --- | --- |
| Banner | 72 | Condensed bold, +14 % | `VICTORY` / `DEFEAT` |
| H2 panel title | 15 (16 on mode bar) | Condensed bold caps, +7 % | `BARRACKS · DRILL SHED`, `CONSTRUCT` |
| Big numeral | 22 (19 in force counter) | Condensed bold, +3 %, tabular | Power, `FORCE 5 / 6` |
| H3 label | 10 | Condensed bold caps, +12 %, `text-3` | `ROSTER · LUDDITE ×6`, top-strip labels |
| Value | 13 | Roboto Medium, tabular | Top-strip values |
| Body | 11 (10.5 in dense rows) | Roboto Regular | Copy, tooltips, kv rows |
| Caption | 9.5 | Roboto Regular | Hints. **Minimum for prose.** |
| Command label | 8.5 | Condensed bold caps, +6 % | Under command-card glyphs. Minimum for anything, and only for one or two short words |
| Key cap | 8.5 | Roboto Bold | Hotkeys |
| Machine readout | 10.5 (11.5 for the live line) | Droid Sans Mono | `Thinking…` |

Line height 1.25 (body 13.5 in tooltips). Text is drawn top-aligned in boxes of `LineHeight`. Ellipsis-clip long strings, as `FPainter::Text` already does.

## 4. Colour tokens

**All hex values are sRGB display values.** The existing code passes `FLinearColor` components, and the Canvas gamma-encodes them, so pasting a hex triple as `FLinearColor(r, g, b)` washes it out. Convert with `FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("38BBFF")))`. The player colours below are the current `AArmyUnit::GetCommanderColor` linear values already encoded, so the HUD trim matches the team plates in the world exactly.

### Offline skin (gunmetal)

| Token | Hex | Use |
| --- | --- | --- |
| `void` | `#05080C` | Deepest fill, silhouette edge of frames |
| `panel` | `#0B1118` at 88 % | Panel fill (baked into `panel_base`) |
| `steel-900` … `steel-300` | `#10161D` `#1A222C` `#26303B` `#354250` `#4A5A6B` `#6D8094` `#93A6B9` | Bevel ramp, dividers, key caps. Bevel light comes from the upper left |
| `text` | `#E9EFF6` | Primary text (contrast on `panel`: 16.4:1) |
| `text-2` | `#A9B8C8` | Secondary text (9.4:1) |
| `text-3` | `#74869A` | Labels, hints (5.1:1) |
| `text-off` | `#4B596A` | Disabled only (2.7:1, never for information that matters) |

### Signal

| Token | Hex | Use |
| --- | --- | --- |
| `hazard` | `#FFC21A` | Hazard stripe tab and `hazard_tile` only. Keep it small so it never reads as the yellow team |
| `amber` / `warn` | `#FFB02E` | Warning trim, in-progress production, "need X more" |
| `gold` | `#FFD166` | Every cost figure and the Power value |
| `ok` | `#5FE07A` | Healthy HP, running / complete states, valid placement |
| `bad` | `#FF5442` | Unaffordable, blocked, low HP, defeat |

### Player colours (trim, swatches, chips)

| Commander | Token | Hex |
| --- | --- | --- |
| C1 | `team-1` | `#38BBFF` |
| C2 | `team-2` | `#FFDD38` |
| C3 | `team-3` | `#6FED89` |
| C4 | `team-4` | `#D389FF` |
| C5 | `team-5` | `#38EDE7` |

The trim, the commander swatch, the selected-slot ring and the minimap pips use the local commander's colour. `bad`, `warn` and `ok` override it when a state must be shouted.

### Machine skin (enemy intel only)

| Token | Hex | Use |
| --- | --- | --- |
| `pearl` | `#EAF1F6` | Panel title, live text |
| `cyan` | `#6BE6FF` | Trim, cursor, commit bar |
| `lens` | `#FF3B30` | The one red lens, enemy HQ bar, enemy HP |
| `m-panel` | `#06121A` at 86 % | Panel fill |

### Kinds, force types, fronts

| Group | Token | Hex | Glyph |
| --- | --- | --- | --- |
| Building | `k-barracks` | `#FF9440` | none, name is enough |
| | `k-outpost` | `#66E68C` | |
| | `k-workshop` | `#B885FF` | |
| Force type | `r-frontline` | `#FFB454` | `role_frontline` (shield with bar) |
| | `r-ranged` | `#6EE7B7` | `role_ranged` (rifle) |
| | `r-siege` | `#C4A1FF` | `role_siege` (mortar) |
| Front (matches the world rings) | `f-secure` | `#FF5C4D` | `secure` |
| | `f-defend` | `#61EB7A` | `defend` |
| | `f-fallback` | `#FFDB52` | `fall_back` |

## 5. Skins and frame anatomy

**Offline skin** (`frames/panel_*`, `btn_*`, `slot_*`, `portrait_*`, `minimap_*`, `strip_*`, `banner_*`): industrial gunmetal. Anatomy from outside in: 1.4 texel near-black silhouette, a bevel ring lit by edge normal (upper-left edges light, lower-right dark, chamfers brightest and darkest), 1.2 texel groove, translucent `panel` fill with a 22-texel top sheen and a bottom shadow. Corners are chamfered (large cut top-left and bottom-right, small cut on the other two) with rivets; the panel carries one hazard-stripe tab on the lower left. The trim is a separate white glow line inset from the bevel, tinted at draw time.

**Machine skin** (`machine_panel_*`, `machine_btn_*`): pearl shell with rounded corners, a floating second ring separated by a dark gap, cyan trim, and the single red lens in the top-left corner. No hazard stripes, no rivets. It appears on the enemy intel panel and on any scouted enemy building panel, and nowhere else.

Bevel lighting is baked per edge, so a 9-slice can stretch the middle strips without gradients. Texture layers: `*_base` carries metal and fill, `*_trim` is white and tinted with the state or player colour.

## 6. Components and states

Buttons and cards are the same family (`btn_*`, 9-slice 14 texels). State is built from four frame textures plus the tinted `btn_trim`:

| State | Frame | Trim | Glyph | Label | Extra |
| --- | --- | --- | --- | --- | --- |
| Normal | `btn_normal` | off | `text` | `text-2` | |
| Hover | `btn_hover` | tint = player colour, 90 % | `text` | `text` | Tooltip after 0.3 s |
| Pressed | `btn_pressed` (inverted bevel) | player colour, 50 % | `text-2` | `text-2` | Offset content 1 px down |
| Active / selected | `btn_normal` | tint = the thing's colour (front colour, force type colour), 100 % | thing's colour | `text` | Steady, no animation |
| Disabled | `btn_disabled` | off | 40 % opacity | 40 % opacity | Tooltip states why |
| Unaffordable | `btn_normal` | `bad`, 95 % | 55 % opacity | | Cost chip in `bad`; `need N` in `bad` under the name |
| Warning | `btn_normal` | `warn`, 95 % | `warn` | | Irreversible actions (`Start · locks Ranged`), cancel build |

Other components:

- **Command-card cell** 60 × 56: glyph 26 px centred, top 2; hotkey chip at the top-left corner (8.5 bold on `void`, 1 px `steel-500` border); optional cost chip at the top-right (9.5 medium, `gold` or `bad`, border darkened); label (8.5 condensed caps) on the bottom edge. Empty cells render at 35 % as dark bevel so the grid never reflows. A cell can span two columns for a primary action.
- **Pill**: 15 high, 9.5 condensed caps, +9 % tracking, 1 px border at 55 % of its colour, angled corners. States: `PRODUCING` `ok`, `PAUSED` `text-2`, `UNCONFIGURED` / `CHOOSE ONCE` `warn`, `BLOCKED` `bad`, `FORCE COMPLETE` `ok`, `PERMANENT` `text-2` with the lock glyph, force type in its colour with its glyph, `SCOUTED` `cyan`.
- **Bar** (`bar_track` + drawn fill): 14–16 high with 4 px frame, fill drawn as a quad with a white top sheen (42 % to 0 over the top half) and 28 % dark bottom. Health thresholds: `ok` above 50 %, `warn` 25–50 %, `bad` below 25 %. A thin **tick bar** (9 high, flat) is used for progress under rosters and for the enemy commit timer. Text inside a bar is 9 bold with a 1 px black shadow.
- **Key cap**: 17 high, 8.5 bold, `steel-800` fill, `steel-400` border with a thicker bottom edge.
- **Hair divider**: 1 px, `steel-300` at 50 % fading to 8 % to the right.
- **Feedback strip**: 22 high, 90 % black fill, 3 px left bar (`warn` by default, `bad` for funds), 10.5 medium text in `#FFE6B0`.
- **Tooltip**: panel skin, title (H2 14) + hotkey chip, hair, 10.5 body in `text-2`, one optional footer line (cost, in `gold`; or a consequence, in `text-2`). Trim tint = the thing's colour (`gold` for reinforce, `warn` for irreversible actions).

### Barracks force presentation (6 / 4 / 2)

A Barracks is configured **once and permanently** as Frontline, Ranged or Siege. It is the same building kind, with no level 2. The UI shows this in four ways:

1. **Before configuration** the info panel is three type cards side by side (`typeCard`), one per force type: building variant portrait, force glyph and colour, unit name and count, capacity pips, cost per unit and time, and the one-time setup fee if any (Siege today: 180). The selected card gets the type-colour trim. The command card repeats the choice on `Q W E`, and the primary button reads `Start · locks Ranged` with the Warning state. The tooltip spells out that it cannot be undone.
2. **After configuration** the header carries a `PERMANENT` pill with a lock glyph and the force-type pill. The type cards are gone for good; nothing offers a change.
3. **The roster is the force.** One slot per capacity (6, 4 or 2), left to right, numbered. A slot is exactly one of:

   | Slot | Look | Click |
   | --- | --- | --- |
   | Joined | Unit portrait, HP bar (`ok` / `warn` / `bad`) | Selects the unit |
   | En route | Portrait at 72 %, cyan `›››` chevrons, `EN ROUTE`, cyan HP bar | none |
   | Building | Portrait as a dark silhouette, `BUILDING`, amber progress bar | none |
   | Reinforce (open, affordable) | Dashed gold frame, reinforce glyph, price, `REINFORCE` | Pays for that one unit |
   | Open, unaffordable | Dashed grey frame, `NEED n` in `bad`, price in `bad` | none, tooltip |

   Slot width is `min(118, (482 − 6 × (n − 1)) / n)`: 75 for six, 98 for four, 118 for two. The extra room on a Siege barracks holds a stats list (per-unit price, setup, front, fill).
4. **Force counter** under the building portrait (`FORCE 5 / 6`) counts joined plus en-route units against capacity, in `ok` when full. The shared 8-squad cap is gone: the top strip shows `ARMY 11 units · 2 forces`, not `x/8`.

The Reinforce command-card button is the "fill every open slot" shortcut (`R`) and shows the total cost. In the mockups it shows one slot's price (20) because one slot is open.

## 7. Readability over the battlefield

- Panels are 86–96 % opaque. Never put body text directly on the world; if it must be (placement label), use the `world-text` treatment: 1 px black drop shadow plus 6 px soft black glow, bold condensed caps.
- Minimum contrast for information text is 4.5:1 against `panel`. `text-off` is for disabled controls and never carries meaning.
- The trim glow is baked into the texture at 55 % strength. Do not add a second glow in code. Bright world colours (orange dust, team circles) still leave the plates readable because plates are near-black.
- World overlays (placement footprint, HQ ring, front rings) use `ok` / `cyan` / front colours with a dashed 2 px line and a translucent fill at 10 %.
- Keep one clear "danger" colour: `bad` is only for things that fail or hurt. Enemy HQ, enemy HP and the lens use `lens`, which is deliberately a touch more saturated than `bad` so enemy and warning never look the same.
- Bars carry their numbers as text, and pills carry a word, so a colour-blind player loses nothing. Role glyphs differ in silhouette (shield, rifle, mortar).
- Numbers are tabular so cost and timer columns do not shimmer as they tick.
- At minimum scale (0.75) no text drops below 6.4 real pixels only for command labels; captions are 7.1 px. If that proves too small on 720p, drop command labels and rely on the glyph plus tooltip rather than shrinking the frame.

## 8. Copy rules (from `Docs/World.md`)

- Building and unit titles always start with the function word: `BARRACKS · DRILL SHED`, `OUTPOST · TAP POINT`, `WORKSHOP · THE GARAGE`. Roster header: `ROSTER · LUDDITE ×6`. HQ bars: `HQ · THE BUNKER`, `HQ · THE CLUSTER`.
- **Command-card labels are the plain function word only** (`Secure`, `Defend`, `Fall Back`, `Start`, `Pause`, `Reinforce`). Themed tags appear in the tooltip title and the mode bar: `SECURE — Take It Offline`, `DEFEND — Hold the Line`, `FALL BACK — Soft Reboot`, `START PRODUCTION · Clock In`, `PAUSE PRODUCTION · Smoke Break`.
- Objective `OBJECTIVE · UNPLUG THE CLUSTER`; terminal label `VICTORY · MODEL DEPRECATED · ENTER` and `DEFEAT · SESSION EXPIRED · ENTER`; banner copy `Model deprecated.` and `Your session has expired. Humanity has been sunset.`; restart `Regenerate response? · commands are locked`; nothing-selected-after-end `SESSION ENDED.`
- Currency is **Power** (World.md: the single resource is Power). The glyph `power` precedes every cost.
- Machine lines are always `Thinking… <line>` with `Thought for Ns` next to it, in the mono face, and the plan chip (`ESTABLISH BASE`, `EXPAND TERRITORY`, `ASSAULT HQ`, `DEFEND BASE`) beside it.
- There is no `LEVEL 2`, `UPGRADE TO L2`, `Needs level 2` or `Night Shift` anywhere. World.md and the README still describe level 2; the design follows the current rule (one permanent type per Barracks).

## 9. Icons and portraits

Everything is generated; do not edit the PNGs by hand.

| Asset | Where | Made by |
| --- | --- | --- |
| Command glyphs: `move attack hold fall_back secure defend build cancel reinforce`, plus `power lock pause start role_frontline role_ranged role_siege` (SVG + 128 px white PNG) | `icons/commands/` | `python3 Build/ExportUIAssets.py` |
| 9-slice frames, trims, hazard tile | `frames/` | same script |
| Portraits, 256 px transparent PNG, three-quarter view, 40 files (8 units, 15 buildings including the four Barracks states per faction, 17 environment pieces) | `icons/portraits/{units,buildings,environment}/SM_*.png` | `blender -b --factory-startup -P Build/RenderUIIcons.py` |
| Team masks: white where the `Team` slot is painted, alpha = coverage (`SM_*_team.png`) | next to each portrait | same script |

Portraits are rendered with blue Team paint (commander 1). To show another commander, draw the portrait, then draw its `_team` mask over it tinted with the commander colour. The renderer opens **temporary copies** of the three `.blend` files and never saves, so it is safe while other scripts regenerate the sources. Barracks portraits by state: `SM_<Faction>_Barracks` (unconfigured), `SM_<Faction>_Barracks_Frontline|Ranged|Siege` (configured), `SM_Construction_Barracks` (under construction).

Glyph rules: 64-unit grid, one flat colour, bold solid shapes with 6–9 unit strokes, no thin lines, readable at 20 px. They are tinted with the state colour at draw time.
