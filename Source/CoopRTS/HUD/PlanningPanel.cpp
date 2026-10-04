#include "PlanningPanel.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "Content/MatchContent.h"
#include "MapRegion.h"
#include "Rules/PlanningHudPolicy.h"

namespace CommandHUDPanels
{
namespace
{
// Opaque: the deck's own overview is drawn under it before the buttons are.
const FLinearColor PanelFill(.020f, .030f, .045f, 1.f);
constexpr float ChipGap = 4.f;
constexpr float StepBadge = 18.f;

FRect StepBody(const FRect& Row)
{
	return { Row.X + PlanDetailX - Pad, Row.Y, Row.W - (PlanDetailX - Pad), Row.H };
}

int32 KitIndex(const FContext& Context, bool bRig)
{
	const UMatchContent* Content = MatchContent(Context);
	for (int32 Index = 0; Content && Index < FMath::Min(Content->Buildings.Num(), static_cast<int32>(UE_ARRAY_COUNT(BuildHotkeys))); ++Index)
		if (const UBuildingDefinition* Building = Content->Building(Index);
			Building && (bRig ? Building->bRequiresDeposit : Building->bProducesForces))
			return Index;
	return INDEX_NONE;
}

void AppendRegion(const FContext& Context, const FVector& Location, FStringBuilderBase& Out)
{
	if (const AMapRegion* Region = Context.State->FindRegionAt(Location))
		Out << Region->DisplayName.ToString();
}

void DrawBadge(const FPainter& Paint, const FRect& Row, int32 Number, bool bDone)
{
	const FRect Badge{ Row.X, Row.Y + (Row.H - StepBadge) * .5f, StepBadge, StepBadge };
	Paint.Fill(Badge, bDone ? Palette::Good.CopyWithNewOpacity(.22f) : FLinearColor(.07f, .20f, .30f, 1.f));
	Paint.Outline(Badge, bDone ? Palette::Good : Palette::Friendly);
	TStringBuilder<4> Text;
	if (bDone)
		Text << TEXT("\u2713");
	else
		Text.Appendf(TEXT("%d"), Number);
	Paint.TextIn(Text.ToView(), Badge, 10.f, bDone ? Palette::Good : Palette::Text, true, EAlign::Center);
}

void DrawStepLabel(const FPainter& Paint, const FRect& Row, int32 Number, bool bDone, FStringView Label)
{
	DrawBadge(Paint, Row, Number, bDone);
	Paint.TextIn(Label, { Row.X + 28.f, Row.Y, PlanDetailX - Pad - 28.f, Row.H }, 10.5f, Palette::Text, true);
}

// Steps 1 and 2: the piece's state in one line, with its build-bar keys.
void DrawPieceStep(const FPainter& Paint, const FContext& Context, const FPlanningView& View, const FRect& Row, int32 Number, bool bRig)
{
	const ACommandBuilding* Piece = bRig ? View.Kit->Rig.Get() : View.Kit->Barracks.Get();
	const bool bPlaced = IsValid(Piece);
	TStringBuilder<64> Label;
	Label << TEXT("Place ") << PlanningPieceName(Context, bRig);
	DrawStepLabel(Paint, Row, Number, bPlaced, Label.ToView());
	const int32 Index = KitIndex(Context, bRig);
	TStringBuilder<160> Detail;
	const FPlanningGhosts& Ghosts = Context.Controller->GetPlanningGhosts();
	const bool bGhost = bRig ? Ghosts.bRig : Ghosts.bBarracks;
	if (bPlaced)
	{
		Detail << TEXT("placed at ");
		AppendRegion(Context, Piece->GetActorLocation(), Detail);
		Detail << (Context.bKitReady ? TEXT(" \u00B7 locked") : TEXT(" \u00B7 click to move"));
	}
	else if (bRig && !View.bRigSite)
		Detail << TEXT("no free deposit \u00B7 Power back at 0:00");
	else if (bGhost)
	{
		Detail << TEXT("default: ");
		AppendRegion(Context, bRig ? Ghosts.Rig : Ghosts.Barracks, Detail);
	}
	else
		Detail << TEXT("not placed");
	if (!bPlaced && Index != INDEX_NONE && !(bRig && !View.bRigSite))
		Detail << TEXT(" \u00B7 B ") << BuildHotkeys[Index].Letter;
	const FRect Body = StepBody(Row);
	Paint.TextIn(Detail.ToView(), Body, 10.f, bPlaced ? Palette::Good : Palette::Warn, false, EAlign::Left, 0.f, Body.W - 8.f);
}

// Step 4: the queued first orders, or what to do to queue one.
void DrawOrderStep(const FPainter& Paint, const FContext& Context, const FPlanningView& View, const FPlanningGeometry& G)
{
	const FRect& Row = G.Steps[3];
	DrawStepLabel(Paint, Row, 4, !View.Kit->Orders.IsEmpty(), TEXT("First order"));
	FRect Body = StepBody(Row);
	Body.W -= G.Clear.W + 8.f;
	TStringBuilder<256> Text;
	FLinearColor Color = Palette::Muted;
	if (Context.Controller->IsAssigningOrder())
	{
		Text << TEXT("Attack: LMB a region on the ground or the minimap \u00B7 Shift queues \u00B7 RMB / Esc cancels");
		Color = Palette::Warn;
	}
	else if (View.Kit->Orders.IsEmpty())
		Text << TEXT("optional \u00B7 RMB a region (Shift queues) \u00B7 A then LMB attacks \u00B7 none yet");
	else
	{
		Color = Palette::Text;
		int32 Number = 0;
		for (const FPlanningOrder& Order : View.Kit->Orders)
		{
			++Number;
			Text.Appendf(TEXT("%s%d %s "), Number > 1 ? TEXT("  \u00B7  ") : TEXT(""), Number, OrderTitle(Order.Verb));
			Text << JevIntentView::RegionName(*Context.State, Order.RegionIndex);
		}
	}
	Paint.TextIn(Text.ToView(), Body, 10.f, Color, false, EAlign::Left, 0.f, Body.W);
}

// Where the mouse would put the piece being placed, or why not.
void DrawPlacementStatus(const FPainter& Paint, const FContext& Context, const FRect& Header)
{
	const ACommandPlayerController* Controller = Context.Controller;
	FVector Location;
	FString Reason;
	bool bCanPlace = false;
	const bool bGround = Controller->GetPlacementPreview(Location, Reason, bCanPlace);
	TStringBuilder<192> Text;
	Text << TEXT("LMB place \u00B7 RMB / Esc cancel \u00B7 ")
		 << (bGround ? FStringView(Reason) : FStringView(TEXT("point at ground to place")));
	Paint.TextIn(Text.ToView(), { Header.X + 230.f, Header.Y, Header.W - 230.f, Header.H }, 10.f,
		bCanPlace ? Palette::Good : Palette::Warn, false, EAlign::Right, 0.f, Header.W - 230.f);
}

void DrawPanelHeader(const FPainter& Paint, const FContext& Context, const FPlanningView& View, const FRect& Header)
{
	const float Width = Paint.TextIn(TEXT("PLANNING"), Header, 13.f, Palette::Text, true);
	TStringBuilder<64> Left;
	PlanningHud::AppendTimeLeft(Left, View.Remaining);
	Paint.TextIn(Left.ToView(), { Header.X + Width + 12.f, Header.Y, 190.f, Header.H }, 10.f, Palette::Warn, true);
	if (Context.Controller->IsPlacingBuilding())
		DrawPlacementStatus(Paint, Context, Header);
}

void DrawTextButton(const FPainter& Paint, const FButton& Button, bool bHover, FStringView Label)
{
	const bool bOn = Button.Available();
	Paint.Fill(Button.Rect, !bOn ? Palette::CardOff : bHover ? Palette::CardHover
															 : Palette::Card);
	Paint.Outline(Button.Rect, bOn && bHover ? Palette::Friendly : Palette::Edge);
	Paint.TextIn(Label, Button.Rect, 9.f, bOn ? Palette::Text : Palette::Muted, true, EAlign::Center);
}

void DrawUnitChip(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	const UArmyUnitDefinition* Unit = PlanningUnit(Context, static_cast<int32>(Button.Action) - static_cast<int32>(EHUDAction::PlanUnit0));
	if (!Unit)
		return;
	const bool bOn = Button.Available();
	Paint.Fill(Button.Rect, Button.bActive ? Tint(Unit->Accent, .22f, .98f) : !bOn ? Palette::CardOff
			: bHover                                                               ? Palette::CardHover
																				   : Palette::Card);
	Paint.Outline(Button.Rect, Button.bActive ? Unit->Accent : bHover && bOn ? Palette::Friendly
																			 : Palette::Edge,
		Button.bActive ? 2.f : 1.f);
	TStringBuilder<48> Name;
	if (Button.bActive)
		Name << TEXT("\u2713 ");
	Name << Unit->DisplayName.ToString().ToUpper();
	Paint.TextIn(Name.ToView(), Button.Rect, 9.5f, bOn || Button.bActive ? Palette::Text : Palette::Muted, true, EAlign::Center, 4.f);
}

void DrawReadySlot(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	const FPlanningView View = ReadPlanning(Context);
	Paint.Fill(Button.Rect, !Button.Available() ? Palette::CardOff : bHover ? Palette::CardHover
																			: Palette::Panel);
	Paint.Outline(Button.Rect, Button.bActive ? Palette::Good : Palette::Friendly, 2.f);
	TStringBuilder<64> Label;
	PlanningHud::AppendReady(Label, Button.bActive, View.ReadyHumans, View.Humans);
	Paint.TextIn(Label.ToView(), Button.Rect, 10.f, Button.bActive ? Palette::Good : Palette::Text, true, EAlign::Center);
}
}

FPlanningGeometry PlanningGeometry(const FRect& Panel)
{
	FPlanningGeometry G;
	G.Panel = Panel;
	const float Left = Panel.X + Pad, Width = Panel.W - 2.f * Pad;
	G.Header = { Left, Panel.Y + 8.f, Width, 20.f };
	G.Steps[0] = { Left, Panel.Y + 32.f, Width, 22.f };
	G.Steps[1] = { Left, Panel.Y + 56.f, Width, 22.f };
	G.Steps[2] = { Left, Panel.Y + 82.f, Width, PlanChipHeight };
	G.Steps[3] = { Left, Panel.Y + 116.f, Width, 28.f };
	const float ChipsX = Panel.X + PlanDetailX;
	const float ChipWidth = FMath::Min(PlanChipWidth, (Panel.Right() - Pad - ChipsX - (PlanUnitCount - 1) * ChipGap) / PlanUnitCount);
	for (int32 Chip = 0; Chip < PlanUnitCount; ++Chip)
		G.Chips[Chip] = { ChipsX + Chip * (ChipWidth + ChipGap), G.Steps[2].Y, ChipWidth, PlanChipHeight };
	G.Clear = { Panel.Right() - Pad - 64.f, G.Steps[3].Y, 64.f, 28.f };
	G.Look = { Panel.Right() - Pad - 170.f, Panel.Y + 150.f, 170.f, 28.f };
	G.Note = { Left, Panel.Y + 150.f, G.Look.X - Left - Pad, 28.f };
	return G;
}

const UArmyUnitDefinition* PlanningUnit(const FContext& Context, int32 Chip)
{
	const UMatchContent* Content = MatchContent(Context);
	int32 Seen = 0;
	for (int32 Index = 0; Content && Index < Content->Units.Num(); ++Index)
		if (const UArmyUnitDefinition* Unit = Content->Unit(Index); Unit && !Unit->IsBranch() && Seen++ == Chip)
			return Unit;
	return nullptr;
}

bool IsPlanningAction(EHUDAction Action)
{
	return Action >= EHUDAction::PlanReady && Action <= EHUDAction::PlanPanel;
}

void ForEachPlanningButton(const FContext& Context, const FLayout& Layout, TFunctionRef<void(const FButton&)> Visit)
{
	const FPlanningView View = ReadPlanning(Context);
	const bool bReady = View.Kit && View.Kit->bReady;
	Visit({ EHUDAction::PlanReady, Layout.Pause, View.Kit ? EBlock::None : EBlock::Planning, bReady, 0 });
	Visit({ EHUDAction::PlanPanel, Layout.Inspector, EBlock::None, false, 0 });
	const FPlanningGeometry G = PlanningGeometry(Layout.Inspector);
	for (int32 Chip = 0; Chip < PlanUnitCount; ++Chip)
		if (const UArmyUnitDefinition* Unit = PlanningUnit(Context, Chip))
			Visit({ static_cast<EHUDAction>(static_cast<int32>(EHUDAction::PlanUnit0) + Chip), G.Chips[Chip],
				bReady ? EBlock::Locked : EBlock::None, View.Kit && View.Kit->UnitRole == Unit->Role, 0 });
	const bool bQueue = View.Kit && !View.Kit->Orders.IsEmpty() && !bReady;
	Visit({ EHUDAction::PlanClearOrders, G.Clear, bQueue ? EBlock::None : EBlock::Locked, false, 0 });
	Visit({ EHUDAction::PlanLookJev, G.Look, EBlock::None, false, 0 });
}

static void DrawPanel(const FPainter& Paint, const FContext& Context, const FRect& Rect)
{
	const FPlanningView View = ReadPlanning(Context);
	Paint.Fill(Rect, PanelFill);
	Paint.Outline(Rect, Palette::Friendly, 2.f);
	Paint.Fill({ Rect.X, Rect.Y, 3.f, Rect.H }, Palette::Warn);
	const FPlanningGeometry G = PlanningGeometry(Rect);
	if (!View.Kit)
	{
		Paint.TextIn(TEXT("Syncing your kit..."), G.Header, 11.f, Palette::Warn);
		return;
	}
	DrawPanelHeader(Paint, Context, View, G.Header);
	DrawPieceStep(Paint, Context, View, G.Steps[0], 1, false);
	DrawPieceStep(Paint, Context, View, G.Steps[1], 2, true);
	DrawStepLabel(Paint, G.Steps[2], 3, true, TEXT("Unit type"));
	DrawOrderStep(Paint, Context, View, G);
	Paint.TextIn(View.Kit->bReady ? TEXT("Ready: kit and unit type are locked. Enter un-readies.")
								  : TEXT("Type and kit stay changeable until Ready or 0:00."),
		G.Note, 9.f, Palette::Muted, false, EAlign::Left, 0.f, G.Note.W);
}

void DrawPlanningButton(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	switch (Button.Action)
	{
	case EHUDAction::PlanPanel:
		DrawPanel(Paint, Context, Button.Rect);
		break;
	case EHUDAction::PlanReady:
		DrawReadySlot(Paint, Context, Button, bHover);
		break;
	case EHUDAction::PlanClearOrders:
		DrawTextButton(Paint, Button, bHover, TEXT("CLEAR"));
		break;
	case EHUDAction::PlanLookJev:
		DrawTextButton(Paint, Button, bHover, TEXT("LOOK AT JEV BASE"));
		break;
	default:
		DrawUnitChip(Paint, Context, Button, bHover);
		break;
	}
}
}
