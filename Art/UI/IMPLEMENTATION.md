# Implementing the HUD design in the Canvas HUD (proposal)

[Candidate] Proposal for the gameplay team. Look-and-feel is in [STYLE.md](STYLE.md); target pictures are in [mockups/](mockups/) (`hud.css` and `hud.js` are the layout spec in code form).

Read against the working copy of `Source/CoopRTS/CommandHUD.cpp` as of 2026-09-30 (mid-change: it already has `bForceConfigured`, `START & LOCK`, `FORCE TYPE`, `Vacant/Travelling`). Line references are to that copy.

## 1. What to import

All paths are relative to the repo root. `./x gen export-ui-assets` produces frames/glyphs and `./x gen render-ui-icons` produces portraits/masks; generation procedures are in [`./x help gen`](../../x). Proposed Unreal asset imports use `./x editor` ([`./x help editor`](../../x)); the table records the intended import contract, not a second launch recipe.

| Asset | Files | Unreal name | Import settings | Source |
| --- | --- | --- | --- | --- |
| Frames | `Art/UI/frames/*.png` (25 files) | `/Game/UI/Frames/T_UI_<Name>` (`panel_base` → `T_UI_PanelBase`) | Compression `UserInterface2D (RGBA)`, sRGB on, mips off, `NeverStream`, address Clamp, filter Bilinear. **Exception `hazard_tile`: address Wrap** | `./x gen export-ui-assets` |
| Command glyphs | `Art/UI/icons/commands/*.png` (16 files, 128 px, white) | `/Game/UI/Icons/T_Cmd_<Name>` | Same. They are tinted at draw time, so keep them white | same script |
| Portraits | `Art/UI/icons/portraits/units/*.png`, `buildings/*.png` (`environment/` optional) | `/Game/UI/Portraits/T_Portrait_<SM_name>` | Same | `./x gen render-ui-icons` |
| Team masks | the `*_team.png` next to each portrait | `T_Portrait_<SM_name>_Team` | Same | same script |
| Header font | `Engine/Content/Slate/Fonts/Roboto-BoldCondensed.ttf` | `F_HUD_Cond` (Font Face + Composite Font with one typeface named `Bold`) | | ships with Unreal |
| Mono font | `Engine/Content/Slate/Fonts/DroidSansMono.ttf` | `F_HUD_Mono` | | ships with Unreal |

9-slice margins for every frame are in `Art/UI/frames/slices.txt` (texels, top right bottom left). Textures are 2 texels per reference pixel, so **a slice of 32 texels is 16 reference px**.

Portrait selection (all names are the `SM_` meshes from `Art/Buildings` and `Art/Units`):

| Situation | Portrait |
| --- | --- |
| Unit / force of role R, faction F | `SM_<F>_<Frontline|Ranged|Siege>` (`Human` for teams 0–4, `Machine` for team 5) |
| Barracks, `bForceConfigured == false` | `SM_<F>_Barracks` |
| Barracks, configured as role R | `SM_<F>_Barracks_<R>` |
| Any building under construction | `SM_Construction_<Barracks|Outpost|Workshop>` |
| Outpost / Workshop (built) | `SM_<F>_Outpost` / `SM_<F>_Workshop` |
| HQ | `SM_<F>_HQ` |

Loading: `ACommandHUD` has no constructor today. Add one with `ConstructorHelpers::FObjectFinder<UTexture2D>` for the frame and glyph textures (about 40 references), and lazy `LoadObject<UTexture2D>` for portraits by role, cached in a `TMap<FName, TObjectPtr<UTexture2D>>` member. Textures loaded lazily by path are not pulled into the cook by references, so add `+DirectoriesToAlwaysCook=(Path="/Game/UI")` under `[/Script/UnrealEd.ProjectPackagingSettings]` in `Config/DefaultGame.ini` (the file currently lists only `+MapsToCook`; not edited here).

## 2. Painter additions

`FPainter` (line 360) already does snapped fills, outlines, bars, text with ellipsis, and key caps. Add textured primitives beside it. Everything stays in reference coordinates, so `MakeLayout`, `ForEachButton` and hit testing do not change.

```cpp
struct FSlice { float Top, Right, Bottom, Left; };            // texels, from frames/slices.txt

// 9-slice: corners fixed, edges and centre stretched. Texels are 2 per reference px.
void NineSlice(const UTexture2D* Tex, const FRect& R, const FSlice& S, const FLinearColor& Tint, bool bFillCentre = true) const
{
	if (!Tex || !Tex->GetResource()) return;
	const float TW = Tex->GetSizeX(), TH = Tex->GetSizeY();
	const float X[4] = {R.X, R.X + S.Left * .5f, R.Right() - S.Right * .5f, R.Right()};
	const float Y[4] = {R.Y, R.Y + S.Top * .5f, R.Bottom() - S.Bottom * .5f, R.Bottom()};
	const float U[4] = {0.f, S.Left / TW, 1.f - S.Right / TW, 1.f};
	const float V[4] = {0.f, S.Top / TH, 1.f - S.Bottom / TH, 1.f};
	for (int32 Row = 0; Row < 3; ++Row)
		for (int32 Col = 0; Col < 3; ++Col)
		{
			if (Col == 1 && Row == 1 && !bFillCentre) continue;
			// Snap edges, not sizes (same rule as Fill) so neighbours never leave hairlines.
			const float L = FMath::RoundToFloat(X[Col] * Scale), T = FMath::RoundToFloat(Y[Row] * Scale);
			const float W = FMath::RoundToFloat(X[Col + 1] * Scale) - L, H = FMath::RoundToFloat(Y[Row + 1] * Scale) - T;
			if (W <= 0.f || H <= 0.f) continue;
			FCanvasTileItem Tile(FVector2D(L, T), Tex->GetResource(), FVector2D(W, H),
				FVector2D(U[Col], V[Row]), FVector2D(U[Col + 1], V[Row + 1]), Tint);
			Tile.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Tile);
		}
}

void Glyph(const UTexture2D* Tex, const FRect& R, const FLinearColor& Tint) const;   // one tile, full UV
void Tiled(const UTexture2D* Tex, const FRect& R, float TexelsPerRefPx, const FLinearColor& Tint) const; // hazard_tile, Wrap
```

Panels become `NineSlice(Base, Rect, Slice, White)` followed by `NineSlice(Trim, Rect, Slice, PlayerColour, false)`. `Paint.Panel(Rect)` can keep its signature and forward to the frame, so the call sites in `DrawBuildPanel`, `DrawModeBar`, `DrawFeedback` and `DrawBanner` need only a frame argument. For a stronger glow, draw the trim with `SE_BLEND_Additive`.

**Colour.** Convert every hex from `STYLE.md` once, in `Palette`, with `FLinearColor::FromSRGBColor(FColor::FromHex(...))`. Do not paste hex as linear components (that is how the current palette ended up washed out). `AArmyUnit::GetCommanderColor` already returns linear values and can be used unchanged for the trim: they equal the `team-*` tokens after gamma encoding.

**Fonts.** `FPainter::Info` builds `FSlateFontInfo(Font, Size * Scale, "Regular"|"Bold")` from `GEngine->GetSmallFont()`. Add a `const UFont* HeadFont` (`F_HUD_Cond`) and let `Info(Size, Style)` take an `enum EStyle { Body, BodyBold, Head }`; set `LetterSpacing` (in 1/1000 em, `FSlateFontInfo::LetterSpacing`) to about 70 for headers. `Paint.Text(..., bBold)` call sites for H2/H3/labels switch to `Head`.

## 3. Layout: keep the two geometry authorities

`MakeLayout` stays the only place panels are placed and `ForEachButton` the only place clickable rectangles come from. Changes:

- **Reference size.** Author at 1400×788 (`ReferenceWidth/Height`), clamp scale to 0.75–2.5. The working copy is at 1280×720 with `MaxScale = 1`; if that stays, multiply every number in the STYLE.md geometry table by 0.914.
- **`FLayout` additions:** `TopLeft`, `TopCentre`, `TopRight` (replace the single `Top`), `Construction` stays (now the Construct header, which is also the F4 toggle), `Build` (cards), `Info` (replaces `Inspector`), `Card` (the command card panel), `Tooltip`, `Intel`. `Minimap` becomes 212 square, interior 176 (`Frame.Interior` helper: rect inset by 18).
- **`Bottom`** (mode bar footprint) is the info rect in expanded mode, and 86 high in placement / front targeting. When `bExpanded == false` or a mode is active, keep `Construction` as a 232 × 40 restore button (as mocked in `07-placement`).
- **Hit areas.** `IsPanelPoint` must include `Card`, `Tooltip` (only while shown) and `Intel`. The centre of the screen remains unclaimed.
- **New buttons emitted by `ForEachButton`:** the 12 command cells (`CommandCell(Index)` rect helper = 4 columns × 3 rows of 60 × 56, gap 4, inside `Card` content), the three type cards before configuration, and, for a configured Barracks, one button per vacant roster slot (see section 6).

## 4. Draw order

Follow `DrawHUD` (line 1105); new layers marked +.

1. World.
2. + Terminal vignette (only when `bTerminal`): full-screen translucent quad, radial (see `.vignette` in `hud.css`).
3. Top strip: for each of the three plates `NineSlice(strip_base)` → content → `NineSlice(strip_trim, PlayerColour)`.
4. Minimap: draw `CommandMinimap::Draw` first, **then** `minimap_base` and `minimap_trim` on top (the frame interior is transparent).
5. Construct panel (`panel_base`) → header → three cards (`btn_*` by state, portrait tile, texts) → trim on top of each card.
6. Info panel (`panel_base`) → header row → hair divider → body → `panel_trim`. Portrait: backing gradient (three-stop vertical, see `.pframe`) → portrait → team mask (tinted) → `portrait_base` → `portrait_trim` in the force-type colour.
7. Roster slots: `slot_base` (or `btn_disabled` for open slots) → portrait → HP / progress bar → text; `slot_trim` on the selected slot.
8. Command card panel → 12 cells: `btn_*` → glyph (tinted) → hotkey chip → cost chip → label → trim.
9. + Tooltip (`panel_base`) for the hovered cell or roster slot, above the card. Drawn after the cells so it never sits under a neighbour.
10. Feedback strip.
11. + Enemy intel panel and scouted-building panel (Machine skin) when data exists.
12. Mode bar (placement / front targeting), or the collapsed Construct restore button.
13. Terminal banner (`banner_base`, `banner_trim` in `ok` / `bad`, text).

The current `ForEachButton` visit at the end of `DrawHUD` (line 1133) already draws buttons after panels; keep that, but make `DrawButton` call `NineSlice` first.

## 5. Mapping: existing element → design

| Existing (function, line) | Design element | Notes |
| --- | --- | --- |
| `DrawTopBar` economy string (543) | Left plate (commander swatch + `C1 you`, `POWER` big numeral in `gold` + `+N/s` in `ok`, `SECTORS` diamond pips + `n/3`); right plate (`ARMY`, `SPECIALIZATION`, `TEAM` chips) | Pips: neutral, captured (amber outline), established (player colour filled), contested (`bad`). `ARMY` shows `units · forces` (`ConfiguredForces`); no `x/8` |
| `DrawHQBar` (531) | Centre plate, two bars, labels `HQ · THE BUNKER` and `HQ · THE CLUSTER`, value inside the bar | Player bar in `team-1`, enemy bar in `lens` |
| Objective text (removed from strip in the working copy) | Centre plate label `OBJECTIVE · UNPLUG THE CLUSTER` | Terminal: `VICTORY · MODEL DEPRECATED · ENTER` in `ok`, `DEFEAT · SESSION EXPIRED · ENTER` in `bad` |
| `Layout.Construction` button, `EHUDAction::Construction` | Construct header (`CONSTRUCT`, `your stash`); acts as the F4 toggle | |
| `DrawBuildCard`, `BuildBarracks/Outpost/Workshop` | Construct cards | 52 high: portrait tile 38, `BARRACKS` (H2 13), tag `DRILL SHED` in kind colour, `cost` in `gold` with the `power` glyph + `12s`. Unaffordable: red trim, `need N`. The hint `Build inside the Bunker ring or a sector with a finished Tap Point.` moves into the hover tooltip |
| `KindTitle` | `BARRACKS · DRILL SHED` etc. | Tags: `DRILL SHED`, `TAP POINT`, `THE GARAGE` |
| `DrawBuildingInspector`, not complete | Info panel, `06-barracks-constructing` | Bar 20 high `k-<kind>`, `42% · 7s remaining`, `Bolting it together…`; `CANCEL BUILD` as a 2-wide Warning cell in the command card, refund shown in the body |
| `DrawBuildingInspector`, Barracks, not configured (798) | Info panel: three type cards (`05-barracks-configure`) | `RecipeFrontline/Ranged/Siege` become the type cards **and** command cells `Q W E` (same actions). `ToggleProduction` becomes the 2-wide `Start · locks <Type>` cell with the Warning state; the fee shows as the cost chip on the Siege cell (`180`), read from `GetConfigurationCost` |
| `DrawBuildingInspector`, Barracks, configured | Info panel: header (`PERMANENT` pill, force-type pill, status pill, HP bar), portrait + force counter, roster, production row (`02`–`04`) | See section 6 |
| `ColumnLabel FORCE TYPE` / `LOCKED` | `PERMANENT` pill with lock glyph | |
| `FRONT` column + `FrontSecure/Defend/FallBack` rows | Command cells `Q W E`; the active one gets the front-colour trim and glyph; the header line shows `front: SECURE set` or `front: unset, gathers at barracks` in `warn` | Labels stay plain (`Secure`, `Defend`, `Fall Back`); tooltip titles carry the theme |
| `Remedy` line (836) | Feedback strip (for actionable text: `Need 5 more power…`) and the production row (for state text) | |
| Workshop inspector, `ResearchSiege/Repairs/Entrenched` | **Not mocked.** Reuse the same `btn_*` cards in three columns, `k-workshop` trim, name `Siege Optics — Salvaged Rangefinder` etc. | Ask if you want a mockup |
| Outpost inspector | Info panel, portrait `SM_Human_Outpost`, kv rows: `SECTOR n`, income line, rights line, warning in `warn`; header pill `SECTOR ESTABLISHED · Tap Point live` in `ok` | Not mocked |
| `DrawOverview` (nothing selected) | Info panel with the same three columns; `NEXT STEP` text in `team-1`; `CONTROLS` as key caps | Not mocked. After match end: `SESSION ENDED.` + `Regenerate response? [Enter]` (see `10-defeat`) |
| Historical squad inspector (unit HP, order, auto/manual, target) | Info panel `01-gameplay`: role portrait, `ALIVE n / n`, one slot per unit with HP, `ORDER` kv row | The mockup maps former manual orders to command cells; hotkeys remain a proposal, not current player bindings |
| `DrawModeBar`, placement (958) | Mode bar (`07-placement`) | Kind portrait tile 54, title `PLACE BARRACKS · DRILL SHED`, `220` + `12s build`, validity dot + reason line (`Footprint clear · inside the Bunker ring`, `ok` or `warn`), `LMB Place` and `RMB / Esc Cancel` key caps |
| `DrawModeBar`, front (982) | Same bar, kind portrait replaced by the front glyph, title `SET SECURE FRONT · Take It Offline`, body `Left-click navigable ground; this barracks' persistent force heads there.` | Not mocked |
| `DrawModeBar`, hidden (995) | Collapsed Construct restore button plus a mode bar with `COMMAND DECK HIDDEN` | Same text as now |
| `DrawFeedback` | Feedback strip | Border colour `warn`, or `bad` for funds |
| `DrawBanner` (1020) | Banner (`09`, `10`) | `banner_base` 9-slice, trim `ok`/`bad`, `SESSION ENDED` H3, 72 px title, one-line message, hazard divider, optional stats row, `Enter` key + `Regenerate response? · commands are locked` |
| Enemy plan debug strings (`EnemyPlan`, `EnemyPlanRationale`, both replicated on `ACommandGameState`) | Enemy intel panel (`08`) | See section 9 |
| `CommandMinimap::Draw` | Minimap interior | Unchanged; only the frame is new |

**Strings** (current → proposed; all from `Docs/World.md`, "HUD copy suggestions", non-binding):

| Current | Proposed |
| --- | --- |
| `resources`, `RESOURCES` | `POWER` (single resource is Power) |
| `YOUR HQ` / `ENEMY HQ` | `HQ · THE BUNKER` / `HQ · THE CLUSTER` |
| `OBJECTIVE · DESTROY THE ENEMY HQ` | `OBJECTIVE · UNPLUG THE CLUSTER` |
| `BARRACKS` / `OUTPOST` / `WORKSHOP` | `BARRACKS · DRILL SHED` / `OUTPOST · TAP POINT` / `WORKSHOP · THE GARAGE` |
| `START & LOCK` | cell label `Start`; when unconfigured `Start · locks Ranged`; tooltip `START PRODUCTION · Clock In` |
| `PAUSE` / `RESUME` | cell label `Pause` / `Start`; tooltip `PAUSE PRODUCTION · Smoke Break` / `START PRODUCTION · Clock In` |
| `SECURE` / `DEFEND` / `FALL BACK` rows | Cell labels unchanged; tooltip titles `SECURE — Take It Offline`, `DEFEND — Hold the Line`, `FALL BACK — Soft Reboot` |
| `SET SECURE FRONT` | `SET SECURE FRONT · Take It Offline` |
| `UNDER CONSTRUCTION` | `UNDER CONSTRUCTION · Bolting it together…` |
| `Match over.` / `Press Enter for a fresh match.` | `Session ended.` / `Regenerate response? [Enter]` |
| Banner texts | `Model deprecated.` / `Your session has expired. Humanity has been sunset.` |
| `Syncing commander, wallet and territory...` | `Session started. Syncing…` |
| `Vacant %d`, `Travelling %d` | roster slot states `EN ROUTE`, `REINFORCE`, `NEED n` |
| `Type locked` (block reason) | `PERMANENT` pill; no per-row text needed once slots replace rows |

**Icons** (EHUDAction → glyph, tint):

| Action | Glyph | Tint |
| --- | --- | --- |
| `BuildBarracks/Outpost/Workshop` | portrait tile (no glyph); `build` for the mode bar hint | kind colour |
| `CancelConstruction` | `cancel` | `bad` on hover, `warn` trim |
| `RecipeFrontline/Ranged/Siege` | `role_frontline/ranged/siege` | force-type colour |
| `ToggleProduction` | `start` when paused or unconfigured, `pause` when running | `ok` / `amber` |
| `FrontSecure/Defend/FallBack` | `secure` / `defend` / `fall_back` | `f-secure` / `f-defend` / `f-fallback` |
| new `Reinforce` (all open slots) | `reinforce` | `gold`, cost chip |
| squad manual `Move`, `Attack`, `Hold`, `FallBack` | `move`, `attack`, `hold`, `fall_back` | `text` |
| Top strip Power / lock hints | `power`, `lock` | `gold`, `text-2` |
| Research cards | `build` is not used; use the workshop portrait or none | `k-workshop` |

## 6. Barracks roster: data and actions

The design needs only data the current build already replicates, plus one new action.

| Roster element | Source |
| --- | --- |
| Capacity (6 / 4 / 2) | `ACommandBuilding::GetForceCapacity(ProductionRole)` |
| Joined / en-route units and their HP | `ForceGroup->Units` (skip dead; `AArmyUnit::bReinforcing` splits en route from joined; HP from `Health` / `MaxHealth()`) |
| Slot order | Stable: sort the living units by spawn order (or index) and fill slots left to right; open slots after. A dead unit's slot is reused |
| `BUILDING` slot | The first open slot while `GetProductionStatus() == PRODUCING` or `DEPLOYMENT BLOCKED`, with `ProductionProgressSeconds / GetProductionDuration()` |
| Price on open slots | `GetProductionCost()` (20 / 30 / 50); unaffordable when `Balance < cost` → `NEED n` |
| Force counter `FORCE 5 / 6` | `Joined + Travelling` over capacity |
| Status pill | `GetProductionStatus()`: `PRODUCING`, `PAUSED`, `INSUFFICIENT RESOURCES`, `DEPLOYMENT BLOCKED`, `FORCE COMPLETE`, `UNCONFIGURED`, `UNDER CONSTRUCTION`, `MATCH FINISHED`. Colour map in `STYLE.md` section 6 |

**Per-unit reinforce is a new action.** Clicking an open slot needs a way to say "pay for one unit now". Proposal: `EHUDAction::ReinforceSlot` (one action, slot index carried in the button, or 6 enum values) and `EHUDAction::ReinforceAll` for the `R` cell. Design intent: paid at deployment as today, walks to the force, the price is `GetUnitCost(Role)`. If the gameplay rule stays "auto-repeat while enabled", the open-slot buttons would just show `REINFORCE` as a state label (no click) and the `R` cell disappears. That is the main open question below; the frame, slot and card art support either.

## 7. Command card

12 cells, row-major, `Q W E R / A S D F / Z X C V`. Proposal (hotkeys are not in the source today; they are a suggestion so the card matches SC2 muscle memory, `Esc` is real):

| Selection | Cells |
| --- | --- |
| Force (squad) | `Q Move` `W Attack` `E Hold` `R Fall Back` / `A Secure` `S Defend` `D Reinforce` / `Esc Cancel` |
| Barracks, configured | `Q Secure` `W Defend` `E Fall Back` `R Reinforce` / `A Start`\|`Pause` / `Esc Cancel` |
| Barracks, unconfigured | `Q Frontline` `W Ranged` `E Siege` / `A Start · locks <Type>` (2 wide) / `Z Secure` `X Defend` `C Fall Back` / `Esc Cancel` |
| Under construction | `Esc Cancel build` (2 wide, Warning) |
| Enemy building or nothing owned | all cells empty; Machine skin shows `No command access` |

## 8. Scaling, texture quality and testing

- Uniform scale as today. Frames are authored for scale up to 2; above that, bilinear upscaling softens bevels slightly, which is acceptable. Higher-density frame output uses `./x gen export-ui-assets` ([`./x help gen`](../../x)); texel ratio is a source-art choice, not a HUD layout change.
- Snap 9-slice edges to whole pixels (`NineSlice` above does) or seams show at fractional scales such as 0.914 or 1.371.
- `./x verify hud` ([`./x help verify`](../../x)) captures the implemented HUD, not this proposal. Compare actual captures against `mockups/*.png`; the mockups are 1920×1080 with the same layout numbers, so a side-by-side at that size is a fair review.
- `./x gen render-ui-mockups` regenerates the HTML design pictures after a tweak ([`./x help gen`](../../x)). These are proposed-layout references, not Unreal rendering or native-input proof.

## 9. Enemy intel

`08-enemy-intel` has two Machine-skinned surfaces:

1. **Intel panel** (top right, 402 × 196). Header `ENEMY INTEL` + `SCOUTED` pill; `PLAN` chip with `EnemyPlan` (`ESTABLISH BASE`, `EXPAND TERRITORY`, `ASSAULT HQ`, `DEFEND BASE`); mono console with one or two faded previous lines and the live line prefixed `›` with a blinking cursor; `Thought for Ns`; a thin `COMMIT` bar for the 12 s commitment. Line text comes from the `Docs/World.md` "Enemy lines" tables keyed off the plan and `EnemyPlanRationale`.
2. **Scouted building** in the info panel and an empty command card (`No command access`). Roster shows machine unit portraits, `?` slots for units not seen, `SEEN 3 / 6`.

Gap to close: the interim lines (building a farm, queuing SOL 6000, provisioning a second farm) are chosen inside `AEnemyCommander::EvaluatePlan` / `BuildNear` and do not change `EnemyPlan`, so they are **not replicated today**. Only `EnemyPlan` and `EnemyPlanRationale` are. Options: replicate a short `EnemyIntent` string or enum from the commander, or map only the plan-level lines (Capture, Contest, Defend HQ, Retreat, Attack HQ, Emergency interrupt) and leave the construction lines out until then.

When to show it: only while the local player has scouting on the enemy (an owned unit or building near an enemy building). [INFERENCE] No fog of war or scouting mechanic appears in the README or the HUD code, so a simple rule such as "any of my units within N m of a Machine building" is enough. **This visibility rule is a design decision the gameplay team needs to make.**

## 10. Open questions for the gameplay team

1. **Per-unit reinforce.** Is reinforcement a click on an open slot (new `ReinforceSlot` / `ReinforceAll` actions), or does production auto-repeat while enabled? The roster and card art support both (section 6).
2. **Configuration fee.** The mockups show Siege's one-time 180 (`GetConfigurationCost`), since the source has it. Keep it? If it goes, drop the cost chip and the setup line; nothing else changes.
3. **Explicit "Configure" vs "Start locks".** The mockup keeps today's behaviour (first Start locks). A separate `CONFIGURE · <Type>` confirm button would be cleaner but needs a new action and state.
4. **Reference size.** Working copy is 1280×720 / MaxScale 1; this design is 1400×788 / 0.75–2.5. Which one ships?
5. **`POWER` vs `resources`.** World.md says the single resource is Power; the HUD says resources. The mockups use Power and a bolt glyph.
6. **Unit names and role lines.** The roster header uses World.md names (Luddite, Offline Ranger, Unplugger) with plain role lines. Only `Frontline · Melee · Tanky` is in World.md; `Ranged · Rifle · Fragile` and `Siege · Heavy · Slow` are placeholders from the roster table.
7. **Hotkeys.** The QWER/ASDF/ZXCV grid is a proposal; only `F4`, `Esc`, `Enter`, `Tab` exist. Do you want the card hotkeys implemented?
8. **Terminal stats row** (time, units lost, sectors) is optional decoration with invented numbers in the mockups. Include only if the match tracks them.
9. **Enemy intel** interim lines and the visibility rule (section 9).
10. **Portraits per commander.** Portraits are rendered in commander-1 blue, with a `_team` mask for tinting. Confirm that is acceptable, or say if you want five pre-tinted sets.
11. **Workshop / Outpost / Overview / front-targeting** are described but not mocked; say if you want them drawn.
12. `Docs/World.md` and `README.md` still describe barracks level 2 (`Night Shift`, `Enterprise Tier`, `UPGRADE TO L2`) and the 8-squad cap. The design already drops them; those docs are not edited here.
