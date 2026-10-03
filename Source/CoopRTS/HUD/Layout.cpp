#include "HUDPanels.h"
#include "ArmyGroup.h"
#include "CommandPlayerController.h"
#include "CommandGameState.h"
#include "Content/MatchContent.h"
#include "Engine/World.h"
#include "DepositSite.h"
#include "MapRegion.h"
#include "ObjectiveAnnouncer.h"

namespace CommandHUDPanels
{
FContext MakeContext(const ACommandPlayerController* Controller)
{
	FContext Context;
	Context.Controller = Controller;
	if (!Controller)
		return Context;
	const UWorld* World = Controller->GetWorld();
	Context.State = World ? World->GetGameState<ACommandGameState>() : nullptr;
	Context.Wallet = Controller->GetPlayerState<ACommandPlayerState>();
	Context.Balance = Context.Wallet ? Context.Wallet->Resources : 0;
	Context.bTerminal = Context.State && Context.State->MatchResult != EMatchResult::Ongoing;
	Context.bExpanded = Controller->IsHUDExpanded();
	// The controller drops selections that stop being owned, so these are the local commander's.
	const ACommandBuilding* Building = Controller->GetSelectedBuilding();
	Context.Building = IsValid(Building) && Building->IsAlive() ? Building : nullptr;
	const AArmyGroup* Force = Controller->GetInspectedForce();
	Context.Force = IsValid(Force) ? Force : nullptr;
	return Context;
}

bool CanPingInspectedForce(const FContext& Context)
{
	return Context.Controller && IsValid(Context.Wallet)
		&& Context.Controller->IsSelectableForce(Context.Force)
		&& Context.Force->GetTeamIndex() == Context.Wallet->TeamIndex
		&& Context.Force->GetOwningPlayerState() != Context.Wallet;
}

FForces CountForces(const FContext& Context)
{
	FForces Forces;
	if (!Context.State || !Context.Wallet)
		return Forces;
	for (const ACommandBuilding* Building : Context.State->Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive() || Building->TeamIndex != 0
			|| Building->OwningPlayerState != Context.Wallet)
			continue;
		if (!Building->IsComplete())
			++Forces.Constructing;
		const UBuildingDefinition* Definition = Building->GetDefinition();
		if (Building->IsProducer())
		{
			++Forces.Barracks;
			if (Building->IsComplete())
				++Forces.CompletedBarracks;
			if (Building->IsComplete() && Building->GetProductionState() == EProductionState::Producing)
				++Forces.Producing;
			if (Building->bForceConfigured)
				++Forces.ConfiguredForces;
		}
		else if (Definition && Definition->bOffersResearch)
			++Forces.Workshops;
		else if (Definition && Definition->bRequiresDeposit)
			++Forces.Extractors;
	}
	for (const AMapRegion* Region : Context.State->Regions)
		if (IsValid(Region) && Context.State->GetRegionController(Region->RegionIndex) == 0)
			++Forces.ControlledRegions;
	for (const ADepositSite* Deposit : Context.State->Deposits)
		if (IsValid(Deposit) && Deposit->Remaining > 0 && !IsValid(Deposit->Extractor)
			&& Context.State->GetRegionController(Deposit->RegionIndex) == 0
			&& !Context.State->IsRegionContested(Deposit->RegionIndex, 0))
			++Forces.FreeDeposits;
	return Forces;
}

const UMatchContent* MatchContent(const FContext& Context)
{
	return Context.State && IsValid(Context.State->Content) ? Context.State->Content.Get() : nullptr;
}

const UArmyUnitDefinition* ProductionDefinition(const FContext& Context)
{
	if (!Context.Building)
		return nullptr;
	if (const UArmyUnitDefinition* Definition = Context.Building->GetProductionDefinition())
		return Definition;
	// Before the first recipe RPC, Start still uses the controller's legacy role contract.
	const UMatchContent* Content = MatchContent(Context);
	return Content && Context.Building->ProductionUnitIndex == INDEX_NONE
		? Content->Unit(Content->UnitIndexForRole(Context.Building->ProductionRole))
		: nullptr;
}
FLayout MakeLayout(const FContext& Context, float PixelWidth, float PixelHeight)
{
	FLayout Layout;
	if (PixelWidth <= 0.f || PixelHeight <= 0.f)
		return Layout;
	Layout.Scale = FMath::Clamp(FMath::Min(PixelWidth / ReferenceWidth, PixelHeight / ReferenceHeight), MinScale, MaxScale);
	Layout.Width = PixelWidth / Layout.Scale;
	Layout.Height = PixelHeight / Layout.Scale;
	Layout.Top = { Margin, Margin, FMath::Min(980.f, Layout.Width - 120.f), TopHeight };
	Layout.Menu = { Layout.Width - Margin - 90.f, Margin, 90.f, TopHeight };
	Layout.Screen = { (Layout.Width - ScreenWidth) * .5f, (Layout.Height - ScreenHeight) * .5f, ScreenWidth, ScreenHeight };
	Layout.Pause = { Layout.Menu.Right() - 190.f, Layout.Menu.Bottom() + Gap, 190.f, TopHeight };
	Layout.Minimap = { Margin, Layout.Height - Margin - MinimapSize, MinimapSize, MinimapSize };
	const float X = Layout.Minimap.Right() + Gap;
	Layout.Build = { X, Layout.Height - Margin - 88.f, FMath::Min(InspectorWidth, Layout.Width - X - Margin), 88.f };
	Layout.Inspector = { X, Layout.Build.Y - Gap - DeckHeight, Layout.Build.W, DeckHeight };
	Layout.Bottom = Context.bExpanded || CanPingInspectedForce(Context) ? Layout.Inspector
																	  : FRect{ X, Layout.Build.Y - Gap - ModeHeight, Layout.Build.W, ModeHeight };
	Layout.Objectives = { Margin, Layout.Top.Bottom() + Gap, Layout.Width - 2.f * Margin, ObjectiveHeight };
	const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(Context.State);
	if (Announcer && !Announcer->GetEvents().IsEmpty())
	{
		const int32 Columns = ObjectiveForceColumns(Layout.Objectives);
		const int32 Rows = FMath::Min(MaxObjectiveForceRows,
			FMath::DivideAndRoundUp(Announcer->GetEvents().Last().Forces.Num(), Columns));
		Layout.Objectives.H = FMath::Max(ObjectiveHeight, 27.f + Rows * (21.f + RowGap) - RowGap + Pad);
	}
	const float AlertBottom = FMath::Min(Layout.Build.Y, Layout.Bottom.Y) - Gap - FeedbackHeight;
	Layout.Alerts = { Layout.Width - Margin - AlertWidth, Layout.Objectives.Bottom() + Gap, AlertWidth,
		FMath::Max(0.f, AlertBottom - Layout.Objectives.Bottom() - Gap) };
	Layout.bFeedback = Context.Controller && Context.Controller->GetFeedbackOpacity() > 0.f;
	Layout.Feedback = { Layout.Bottom.X, Layout.Bottom.Y - Gap * .5f - FeedbackHeight, Layout.Bottom.W, FeedbackHeight };
	if (Context.Controller && Context.Controller->GetUIScreen() != ECommandScreen::Game)
		Layout.Feedback = { Layout.Screen.X, FMath::Max(Margin, Layout.Screen.Y - Gap - FeedbackHeight), Layout.Screen.W, FeedbackHeight };
	return Layout;
}

float BodyTop(const FRect& Inspector) { return Inspector.Y + Pad + HeaderHeight; }

FRect Column(const FRect& Inspector, int32 Index, int32 Count)
{
	const float Width = (Inspector.W - 2.f * Pad - (Count - 1) * ColumnGap) / Count;
	const float Top = BodyTop(Inspector);
	return { Inspector.X + Pad + Index * (Width + ColumnGap), Top, Width, Inspector.Bottom() - Pad - Top };
}

FRect Row(const FRect& ColumnRect, int32 Index, int32 Count)
{
	// Reserve the existing footer; three rows retain their original geometry.
	const float Available = FMath::Min(3.f * RowHeight + 2.f * RowGap, ColumnRect.H - LabelHeight - 18.f);
	const float Height = FMath::Min(RowHeight, (Available - (Count - 1) * RowGap) / FMath::Max(1, Count));
	return { ColumnRect.X, ColumnRect.Y + LabelHeight + Index * (Height + RowGap), ColumnRect.W, Height };
}

FRect BuildCard(const FRect& Build, int32 Index, int32 Count)
{
	const float Width = (Build.W - 2.f * Pad - (Count - 1) * Gap) / FMath::Max(1, Count);
	return { Build.X + Pad + Index * (Width + Gap), Build.Y + 30.f, Width, Build.H - 30.f - Pad };
}

FRect ResearchCard(const FRect& Inspector, int32 Index)
{
	const FRect Area = Column(Inspector, Index, 3);
	return { Area.X, Area.Y + LabelHeight, Area.W, Area.H - LabelHeight };
}

FRect CancelButton(const FRect& Inspector)
{
	return { Inspector.Right() - Pad - 230.f, Inspector.Bottom() - Pad - 36.f, 230.f, 36.f };
}

static void ForEachPanel(const FContext& Context, const FLayout& Layout, TFunctionRef<void(const FRect&)> Visit)
{
	Visit(Layout.Top);
	Visit(Layout.Objectives);
	Visit(Layout.Menu);
	Visit(Layout.Pause);
	Visit(Layout.Minimap);
	Visit(Layout.Construction);
	if (Context.bExpanded)
		Visit(Layout.Build);
	Visit(Layout.Bottom);
	if (Layout.bFeedback)
		Visit(Layout.Feedback);
	ForEachAlert(Context, Layout, [&](const FObjectiveEvent&, const FRect& Alert, float) {
		Visit(Alert);
	});
}

bool IsPanelPoint(const FContext& Context, const FLayout& Layout, const FVector2D& VirtualPoint)
{
	if (Context.Controller && Context.Controller->GetUIScreen() != ECommandScreen::Game)
		return true;
	bool bCovered = false;
	ForEachPanel(Context, Layout, [&](const FRect& Rect) {
		bCovered |= Rect.Contains(VirtualPoint);
	});
	return bCovered;
}

bool OverlayClearsPanels(const FContext& Context, const FLayout& Layout, const FRect& Rect)
{
	if (!Context.Controller || Context.Controller->GetUIScreen() != ECommandScreen::Game
		|| Rect.X < 0.f || Rect.Y < 0.f || Rect.Right() > Layout.Width || Rect.Bottom() > Layout.Height)
		return false;
	bool bCovered = false;
	ForEachPanel(Context, Layout, [&](const FRect& Panel) {
		bCovered |= Rect.Intersects(Panel);
	});
	return !bCovered;
}
EHUDAction HitTest(const FContext& Context, const FLayout& Layout, const FVector2D& VirtualPoint)
{
	EHUDAction Result = EHUDAction::None;
	ForEachButton(Context, Layout, [&Result, &VirtualPoint](const FButton& Button) {
		if (Button.Rect.Contains(VirtualPoint))
			Result = Button.Action;
	});
	return Result;
}

}
