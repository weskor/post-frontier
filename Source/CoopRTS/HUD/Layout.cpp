#include "HUDPanels.h"
#include "CommandPlayerController.h"
#include "CommandGameState.h"
#include "Content/MatchContent.h"
#include "Engine/World.h"

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
	return Context;
}

constexpr EHUDAction BuildActions[] = {
	EHUDAction::BuildSlot0, EHUDAction::BuildSlot1, EHUDAction::BuildSlot2,
	EHUDAction::BuildSlot3, EHUDAction::BuildSlot4, EHUDAction::BuildSlot5
};
constexpr EHUDAction RecipeActions[] = {
	EHUDAction::RecipeSlot0, EHUDAction::RecipeSlot1, EHUDAction::RecipeSlot2,
	EHUDAction::RecipeSlot3, EHUDAction::RecipeSlot4, EHUDAction::RecipeSlot5
};

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
	Layout.Minimap = { Margin, Layout.Height - Margin - MinimapSize, MinimapSize, MinimapSize };
	const float X = Layout.Minimap.Right() + Gap;
	Layout.Construction = { X, Layout.Height - Margin - DeckHeight, BuildWidth, 28.f };
	Layout.Build = { X, Layout.Construction.Bottom() + Gap, BuildWidth, DeckHeight - 28.f - Gap };
	const float InspectorX = Layout.Build.Right() + Gap;
	Layout.Inspector = { InspectorX, Layout.Height - Margin - DeckHeight,
		FMath::Min(InspectorWidth, Layout.Width - InspectorX - Margin), DeckHeight };
	Layout.Bottom = Context.bExpanded ? Layout.Inspector
									  : FRect{ InspectorX, Layout.Height - Margin - ModeHeight, Layout.Inspector.W, ModeHeight };
	Layout.bFeedback = Context.Controller && !Context.Controller->GetOrderFeedback().IsEmpty();
	Layout.Feedback = { Layout.Bottom.X, Layout.Bottom.Y - Gap * .5f - FeedbackHeight, Layout.Bottom.W, FeedbackHeight };
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
	const float Height = FMath::Min(40.f, (Build.H - 2.f * Pad + 2.f - (Count - 1) * 6.f) / FMath::Max(1, Count));
	return { Build.X + Pad, Build.Y + Pad + Index * (Height + 6.f), Build.W - 2.f * Pad, Height };
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
EHUDAction HitTest(const FContext& Context, const FLayout& Layout, const FVector2D& VirtualPoint)
{
	EHUDAction Result = EHUDAction::None;
	ForEachButton(Context, Layout, [&Result, &VirtualPoint](const FButton& Button) {
		if (Button.Available() && Button.Rect.Contains(VirtualPoint))
			Result = Button.Action;
	});
	return Result;
}

}
