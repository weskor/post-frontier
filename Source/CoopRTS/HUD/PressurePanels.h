#pragma once

#include "HUDPanels.h"
#include "Rules/PressureHud.h"

class ADepositSite;

// The economy and pressure surfaces of step 1b (Design/ui.md surfaces 1, 6 and 8): the top bar's supply-cut chip
// and battle clock, a building's stun chip, and the clicks that focus what they point at. Layout is a function of the
// window alone, so drawing and hit testing agree without measuring text.
namespace CommandHUDPanels
{
// The top bar from left: the economy text up to EconomyTextWidth, the cut chip, the battle clock, the key hint.
constexpr float EconomyTextWidth = 400.f;
constexpr float CutChipMaxWidth = 250.f;
constexpr float ClockWidth = 120.f;
constexpr float TopHintWidth = 160.f;
constexpr float ChipHeight = 22.f;

// Regions the human team holds but no longer reaches, and this commander's share of what they paid.
// CutRegions (optional) receives their indices, ascending.
PressureHud::FCutLoss ReadCutLoss(const FContext& Context, TArray<int32>* CutRegions = nullptr);
// The chip's click area: the full bar height. Zero width with nothing cut.
FRect CutChipRect(const FLayout& Layout, const PressureHud::FCutLoss& Loss);
FRect BattleClockRect(const FLayout& Layout);
void DrawDataGlyph(const FPainter& Paint, float X, float Y, const FLinearColor& Color);
void DrawCutChip(const FPainter& Paint, const FLayout& Layout, const PressureHud::FCutLoss& Loss);
// "JEV v1.1" and the count-up clock; nothing until JEV's schedule has replicated.
void DrawBattleClock(const FPainter& Paint, const FContext& Context, const FLayout& Layout);

// The timeline's cell Index inside Bar (cells are equal; the header sits at the left).
FRect JevTimelineCell(const FRect& Bar, int32 Index);
// The pressure actions at a point: the cut chip and the timeline cells. None elsewhere.
EHUDAction HitTestPressure(const FContext& Context, const FLayout& Layout, const FVector2D& VirtualPoint);
// Where a pressure action sends the camera. False when there is nothing to focus (the cut ended, the cell is gone).
bool PressureFocusTarget(const FContext& Context, EHUDAction Action, FVector& World);

// A building's stun chip above its name plate, drawn as part of the building overlay.
void DrawStunChip(const FPainter& Paint, const FRect& Plate, const PressureHud::FStunChip& Stun);
// The stun a building's overlay shows (hidden when it has none).
PressureHud::FStunChip BuildingStun(const FContext& Context, const ACommandBuilding& Building);
// The yellow STUNNED pill in an inspector header's status slot (where UPGRADING sits).
void DrawStunPill(const FPainter& Paint, const FRect& Inspector);

// The plate a deposit's label sits on, in virtual pixels; false when it is off screen. DrawDeposits fills exactly this rect.
constexpr float DepositLabelWidth = 148.f;
// The offline rig's glyph tile is centred this far left of the plate's left edge and reaches this far.
constexpr float OfflineGlyphOffset = 16.f;
constexpr float OfflineGlyphReach = 28.f;
bool DepositLabelRect(const FPainter& Paint, const FContext& Context, const ADepositSite& Deposit, FRect& Out);
// Every deposit label on screen (plus the offline glyphs): world text a badge must not cover.
void DepositLabelRects(const FPainter& Paint, const FContext& Context, TArray<FRect, TInlineAllocator<16>>& Out);
// Rect moved off every obstacle it covers and kept where Fits holds: straight up first, then beside the first obstacle,
// else where it was.
FRect PlaceClearOf(FRect Rect, TConstArrayView<FRect> Obstacles, TFunctionRef<bool(const FRect&)> Fits);
}
