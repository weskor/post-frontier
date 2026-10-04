#include "HUDPanels.h"
#include "CommandPlayerController.h"
#include "CommandGameState.h"
#include "Content/MatchContent.h"
#include "MapRegion.h"
#include "PlanningPanel.h"
#include "Rules/PlanningHudPolicy.h"

namespace CommandHUDPanels
{
const FBuildHotkey BuildHotkeys[6] = {
	{ EKeys::Q, TEXT("Q"), EHUDAction::BuildSlot0 },
	{ EKeys::W, TEXT("W"), EHUDAction::BuildSlot1 },
	{ EKeys::E, TEXT("E"), EHUDAction::BuildSlot2 },
	{ EKeys::R, TEXT("R"), EHUDAction::BuildSlot3 },
	{ EKeys::T, TEXT("T"), EHUDAction::BuildSlot4 },
	{ EKeys::A, TEXT("A"), EHUDAction::BuildSlot5 }
};

// The line under a KIT card's name: amber while the piece is still to place, green once placed, muted when it cannot change.
static FLinearColor KitCardColor(PlanningHud::EKitCard State)
{
	return State == PlanningHud::EKitCard::Free ? Palette::Warn : State == PlanningHud::EKitCard::Placed ? Palette::Good
																										 : Palette::Muted;
}

// A KIT card (ui.md surface 10): the Barracks and Drill Rig are free and placed before 0:00 through the normal placement
// mode; every other building is greyed until the battle starts.
static void DrawKitCard(const FPainter& Paint, const FContext& Context, const FButton& Button, const UBuildingDefinition& Definition, bool bHover)
{
	const FRect& Rect = Button.Rect;
	const bool bKit = IsKitBuilding(Definition);
	const bool bRig = Definition.bRequiresDeposit;
	const FPlanningView View = ReadPlanning(Context);
	const ACommandBuilding* Piece = !View.Kit ? nullptr : bRig ? View.Kit->Rig.Get()
															   : View.Kit->Barracks.Get();
	const PlanningHud::EKitCard State = PlanningHud::KitCard(IsValid(Piece), Context.bKitReady, bRig ? View.bRigSite : true);
	Paint.Fill(Rect, !Button.Available() ? Palette::CardOff : bHover ? Palette::CardHover
																	 : Palette::Card);
	if (Button.bActive)
		Paint.Outline(Rect, Definition.Accent, 2.f);
	else if (bKit && Button.Available())
		Paint.Outline(Rect, KitCardColor(State));
	Paint.Fill({ Rect.X, Rect.Y, 3.f, Rect.H }, Definition.Accent);
	TStringBuilder<64> Title;
	Title.Appendf(TEXT("B %s  %s"), BuildHotkeys[BuildSlot(Button.Action)].Letter, *Definition.DisplayName.ToString().ToUpper());
	Paint.Text(Title.ToView(), Rect.X + 8.f, Rect.Y + 4.f, 10.f, bKit ? Palette::Text : Palette::Muted, true, EAlign::Left, Rect.W - 16.f);
	TStringBuilder<64> Line;
	if (bKit)
		PlanningHud::AppendKitCard(Line, State);
	else
		Line << TEXT("after 0:00");
	Paint.Text(Line.ToView(), Rect.X + 8.f, Rect.Y + 21.f, 9.f, bKit ? KitCardColor(State) : Palette::Muted, true, EAlign::Left, Rect.W - 16.f);
	TStringBuilder<96> Place;
	if (!bKit)
		Place << TEXT("locked in planning");
	else if (IsValid(Piece))
		if (const AMapRegion* Region = Context.State->FindRegionAt(Piece->GetActorLocation()))
			Place << TEXT("at ") << Region->DisplayName.ToString();
	Paint.Text(Place.ToView(), Rect.X + 8.f, Rect.Y + 34.f, 9.f, Palette::Muted, false, EAlign::Left, Rect.W - 16.f);
}

void DrawBuildCard(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	const UMatchContent* Content = MatchContent(Context);
	const UBuildingDefinition* Definition = Content ? Content->Building(BuildSlot(Button.Action)) : nullptr;
	if (!Definition)
		return;
	if (Context.bPlanning)
	{
		DrawKitCard(Paint, Context, Button, *Definition, bHover);
		return;
	}
	const FRect& Rect = Button.Rect;
	Paint.Fill(Rect, !Button.Available() ? Palette::CardOff : bHover ? Palette::CardHover
																	 : Palette::Card);
	if (Button.bActive)
		Paint.Outline(Rect, Definition->Accent);
	Paint.Fill({ Rect.X, Rect.Y, 3.f, Rect.H }, Definition->Accent);
	const FString Title = Definition->DisplayName.ToString().ToUpper();
	TStringBuilder<64> Label;
	Label.Appendf(TEXT("B %s  %s"), BuildHotkeys[BuildSlot(Button.Action)].Letter, *Title);
	Paint.Text(Label.ToView(), Rect.X + 8.f, Rect.Y + 4.f, 10.f, Palette::Text, true, EAlign::Left, Rect.W - 16.f);
	TStringBuilder<48> Detail;
	Detail.Appendf(TEXT("%d  /  %.0fs"), Definition->BuildCost, Definition->BuildDuration);
	if (Definition->bRequiresDeposit)
		Detail << TEXT("  /  deposit");
	Paint.Text(Detail.ToView(), Rect.X + 8.f, Rect.Y + 21.f, 9.f, Palette::Gold);
	if (!Button.Available())
		DrawBlockReason(Paint, Button, Rect.X + 8.f, Rect.Y + 34.f, 9.f, Rect.W - 16.f);
}
void DrawBuildPanel(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	if (Context.bPlanning)
		DrawPlanningGhosts(Paint, Context, Layout);
	Paint.Panel(Layout.Build);
	const bool bHotkeyPending = Context.Controller && Context.Controller->IsBuildHotkeyPending();
	const float Right = Layout.Build.Right() - Pad;
	const float PendingWidth = bHotkeyPending ? Paint.Text(TEXT("B ACTIVE"), Right, Layout.Build.Y + 9.f, 9.f,
													Palette::Gold, true, EAlign::Right)
											  : 0.f;
	if (bHotkeyPending)
		Paint.Outline(Layout.Build, Palette::Gold);
	if (Context.bPlanning)
	{
		Paint.Text(TEXT("KIT \u00B7 free \u00B7 placed before 0:00"), Layout.Build.X + Pad, Layout.Build.Y + 9.f, 9.f, Palette::Muted, true);
		return;
	}
	const UMatchContent* Content = MatchContent(Context);
	const int32 BuildCount = Content ? FMath::Min(Content->Buildings.Num(), static_cast<int32>(UE_ARRAY_COUNT(BuildHotkeys))) : 0;
	for (int32 Index = 0; Index < BuildCount; ++Index)
	{
		const UBuildingDefinition* Definition = Content->Building(Index);
		if (!Definition)
			continue;
		const EBuildingKind Kind = Definition->GetKind();
		const TCHAR* Category = Kind == EBuildingKind::Barracks ? TEXT("PRODUCTION")
			: Kind == EBuildingKind::Extractor                  ? TEXT("ECONOMY")
																: TEXT("TECH");
		const FRect Button = BuildCard(Layout.Build, Index, BuildCount);
		const float CategoryRight = bHotkeyPending ? FMath::Min(Button.Right(), Right - PendingWidth - Gap) : Button.Right();
		if (CategoryRight > Button.X)
		{
			TStringBuilder<48> Label;
			Label << Category;
			if (Definition->bProducesForces && Context.ForceSlots.Limit > 0)
				Label.Appendf(TEXT("  %d/%d"), Context.ForceSlots.Count, Context.ForceSlots.Limit);
			Paint.Text(Label.ToView(), Button.X, Layout.Build.Y + 9.f, 9.f,
				Context.ForceSlots.IsFull() && Definition->bProducesForces ? Palette::Warn : Palette::Muted,
				true, EAlign::Left, CategoryRight - Button.X);
		}
	}
}

}
