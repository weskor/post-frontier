#include "CommandHUD.h"

#include "ArenaBounds.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandMinimap.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineFontServices.h"
#include "HUD/HUDPanels.h"
#include "ObjectiveAnnouncer.h"
#include "Rules/ForceSelectionPolicy.h"

using namespace CommandHUDPanels;

int32 BuildSlot(EHUDAction Action)
{
	switch (Action)
	{
	case EHUDAction::BuildSlot0:
		return 0;
	case EHUDAction::BuildSlot1:
		return 1;
	case EHUDAction::BuildSlot2:
		return 2;
	case EHUDAction::BuildSlot3:
		return 3;
	case EHUDAction::BuildSlot4:
		return 4;
	case EHUDAction::BuildSlot5:
		return 5;
	default:
		return INDEX_NONE;
	}
}

int32 RecipeSlot(EHUDAction Action)
{
	switch (Action)
	{
	case EHUDAction::RecipeSlot0:
		return 0;
	case EHUDAction::RecipeSlot1:
		return 1;
	case EHUDAction::RecipeSlot2:
		return 2;
	case EHUDAction::RecipeSlot3:
		return 3;
	case EHUDAction::RecipeSlot4:
		return 4;
	case EHUDAction::RecipeSlot5:
		return 5;
	default:
		return INDEX_NONE;
	}
}

bool ACommandHUD::IsPanelPoint(const FVector2D& Position) const
{
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller)
		return false;
	if (Controller->GetUIScreen() != ECommandScreen::Game)
		return true;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	if (Layout.Scale <= 0.f)
		return false;
	return CommandHUDPanels::IsPanelPoint(Context, Layout, Position / Layout.Scale);
}

EHUDAction ACommandHUD::GetActionAtScreenPosition(const FVector2D& Position) const
{
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller)
		return EHUDAction::None;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	return Layout.Scale > 0.f ? HitTest(Context, Layout, Position / Layout.Scale) : EHUDAction::None;
}

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
bool ACommandHUD::FindActionScreenPosition(EHUDAction Action, FVector2D& OutPosition) const
{
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller)
		return false;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	bool bFound = false;
	ForEachButton(Context, Layout, [&](const FButton& Button) {
		if (Button.Action == Action)
		{
			OutPosition = Button.Rect.Center() * Layout.Scale;
			bFound = true;
		}
	});
	return bFound;
}
#endif

static void VisitForceBadges(const ACommandPlayerController* Controller,
	TFunctionRef<void(AArmyGroup*, const FRect&, float)> Visit)
{
	if (!Controller || !GEngine || Controller->GetUIScreen() != ECommandScreen::Game
		|| !FEngineFontServices::IsInitialized())
		return;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	const UFont* Font = GEngine->GetSmallFont();
	if (Layout.Scale <= 0.f || !Font)
		return;
	const FPainter Paint{ nullptr, Layout.Scale, Font, FEngineFontServices::Get().GetFontMeasure() };
	ForEachForceBadge(Paint, Context, Layout, [&](AArmyGroup* Force, const FRect& Rect) {
		Visit(Force, Rect, Layout.Scale);
	});
}

AArmyGroup* ACommandHUD::GetForceAtScreenPosition(const FVector2D& Position) const
{
	AArmyGroup* Result = nullptr;
	VisitForceBadges(Cast<ACommandPlayerController>(GetOwningPlayerController()),
		[&](AArmyGroup* Force, const FRect& Rect, float Scale) {
			// Last drawn badge wins when forces overlap.
			if (Rect.Contains(Position / Scale))
				Result = Force;
		});
	return Result;
}

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
bool ACommandHUD::FindForceScreenPosition(const AArmyGroup* Force, FVector2D& OutPosition) const
{
	bool bFound = false;
	VisitForceBadges(Cast<ACommandPlayerController>(GetOwningPlayerController()),
		[&](AArmyGroup* Candidate, const FRect& Rect, float Scale) {
			if (Candidate == Force)
			{
				OutPosition = Rect.Center() * Scale;
				bFound = true;
			}
		});
	return bFound && GetForceAtScreenPosition(OutPosition) == Force;
}
#endif

void ACommandHUD::GetForcesInScreenBox(const FVector2D& Start, const FVector2D& End, TArray<AArmyGroup*>& OutForces) const
{
	OutForces.Reset();
	VisitForceBadges(Cast<ACommandPlayerController>(GetOwningPlayerController()),
		[&](AArmyGroup* Force, const FRect& Rect, float Scale) {
			if (ForceSelectionPolicy::IsInScreenBox(Rect.Center() * Scale, Start, End))
				OutForces.Add(Force);
		});
}

bool ACommandHUD::GetMinimapScreenRect(FVector2D& OutOrigin, float& OutSize) const
{
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller)
		return false;
	if (Controller->GetUIScreen() != ECommandScreen::Game)
		return false;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FLayout Layout = MakeLayout(MakeContext(Controller), Width, Height);
	if (Layout.Scale <= 0.f)
		return false;
	OutOrigin = FVector2D(Layout.Minimap.X, Layout.Minimap.Y) * Layout.Scale;
	OutSize = Layout.Minimap.W * Layout.Scale;
	return true;
}

bool ACommandHUD::GetMinimapWorldPosition(const FVector2D& Position, FVector& OutWorld) const
{
	FVector2D Origin;
	float Size;
	return GetMinimapScreenRect(Origin, Size)
		&& CommandMinimap::ScreenToWorld(AArenaBounds::Find(GetWorld()), Position, Origin, Size, OutWorld);
}

bool ACommandHUD::GetAlertWorldPosition(const FVector2D& Position, FVector& OutWorld, int32& OutSequence) const
{
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller || Controller->GetUIScreen() != ECommandScreen::Game)
		return false;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	return Layout.Scale > 0.f && HitTestAlert(Context, Layout, Position / Layout.Scale, OutWorld, OutSequence);
}

bool ACommandHUD::FindAlertScreenPosition(int32 Sequence, FVector2D& OutPosition) const
{
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller || Controller->GetUIScreen() != ECommandScreen::Game)
		return false;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	if (Layout.Scale <= 0.f)
		return false;
	bool bFound = false;
	ForEachAlert(Context, Layout, [&](const FObjectiveEvent& Event, const FRect& Rect, float) {
		if (Event.Sequence == Sequence)
		{
			OutPosition = Rect.Center() * Layout.Scale;
			bFound = true;
		}
	});
	return bFound;
}

void ACommandHUD::PostRender()
{
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	// Engine draws debug text before DrawHUD, on a separate canvas that would cover modal screens.
	if (Controller && Controller->GetUIScreen() != ECommandScreen::Game)
		DebugTextList.Reset();
	Super::PostRender();
}

static void DrawSelectionBox(const FPainter& Paint, const ACommandPlayerController* Controller, const FLayout& Layout)
{
	FVector2D DragStart, DragEnd;
	if (!Controller->GetSelectionDrag(DragStart, DragEnd))
		return;
	const FVector2D Min = FVector2D(FMath::Min(DragStart.X, DragEnd.X), FMath::Min(DragStart.Y, DragEnd.Y)) / Layout.Scale;
	const FVector2D Size = FVector2D(FMath::Abs(DragEnd.X - DragStart.X), FMath::Abs(DragEnd.Y - DragStart.Y)) / Layout.Scale;
	const FRect Box{ static_cast<float>(Min.X), static_cast<float>(Min.Y), static_cast<float>(Size.X), static_cast<float>(Size.Y) };
	Paint.Fill(Box, Palette::Friendly.CopyWithNewOpacity(.08f));
	Paint.Outline(Box, Palette::Friendly);
}

static void DrawCommandDeck(const FPainter& Paint, const FContext& Context, const FForces& Forces, const FLayout& Layout)
{
	if (Context.bExpanded || CanPingInspectedForce(Context))
	{
		Paint.Panel(Layout.Inspector);
		if (Context.Building)
			DrawBuildingInspector(Paint, Context, Layout.Inspector);
		else if (Context.Force)
			DrawForceInspector(Paint, Context, Layout.Inspector);
		else
			DrawOverview(Paint, Context, Forces, Layout.Inspector);
	}
	else
		DrawModeBar(Paint, Context, Layout);
}

static void DrawWorldOverlays(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	DrawUnitHealthBars(Paint, Context);
	DrawHeadquartersOverlays(Paint, Context);
	DrawBuildingOverlays(Paint, Context);
	DrawSectorOverlays(Paint, Context);
	DrawForceLabels(Paint, Context, Layout);
	DrawSelectionBox(Paint, Context.Controller, Layout);
	DrawPingMarkers(Paint, Context);
}

void ACommandHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine)
		return;
	ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller)
		return;
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Canvas->ClipX, Canvas->ClipY);
	const UFont* Font = GEngine->GetSmallFont();
	if (Layout.Scale <= 0.f || !Font || !FEngineFontServices::IsInitialized())
		return;
	const FPainter Paint{ Canvas, Layout.Scale, Font, FEngineFontServices::Get().GetFontMeasure() };

	EHUDAction Hover = EHUDAction::None;
	float MouseX, MouseY;
	if (Controller->GetMousePosition(MouseX, MouseY))
		Hover = HitTest(Context, Layout, FVector2D(MouseX, MouseY) / Layout.Scale);
	if (Controller->GetUIScreen() != ECommandScreen::Game)
	{
		DrawScreen(Paint, Context, Layout, Hover);
		if (Context.State && Context.State->IsActivePaused())
			DrawButton(Paint, Context, { EHUDAction::ActivePause, Layout.Pause, EBlock::None, true, 0 }, false);
		if (Layout.bFeedback)
			DrawFeedback(Paint, Context, Layout);
		return;
	}
	const FForces Forces = CountForces(Context);
	DrawWorldOverlays(Paint, Context, Layout);

	DrawTopBar(Paint, Context, Forces, Layout);
	DrawMinimap(Paint, Controller, Layout);
	DrawObjectiveAlerts(Paint, Context, Layout);
	DrawBuildPanel(Paint, Context, Layout);
	DrawCommandDeck(Paint, Context, Forces, Layout);
	ForEachButton(Context, Layout, [&Paint, &Context, Hover](const FButton& Button) {
		if (Button.Action == EHUDAction::Menu)
			DrawScreenButton(Paint, Button, Button.Action == Hover);
		else
			DrawButton(Paint, Context, Button, Button.Action == Hover);
	});
	if (Layout.bFeedback)
		DrawFeedback(Paint, Context, Layout);
}
