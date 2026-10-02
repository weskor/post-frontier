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
	const FVector2D Point = Position / Layout.Scale;
	FVector AlertWorld;
	int32 AlertSequence;
	return HitTestAlert(Context, Layout, Point, AlertWorld, AlertSequence)
		|| Layout.Top.Contains(Point) || Layout.Objectives.Contains(Point)
		|| Layout.Menu.Contains(Point) || Layout.Pause.Contains(Point) || Layout.Minimap.Contains(Point) || Layout.Construction.Contains(Point)
		|| (Context.bExpanded && Layout.Build.Contains(Point)) || Layout.Bottom.Contains(Point)
		|| (Layout.bFeedback && Layout.Feedback.Contains(Point));
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
		if (Button.Action == Action && Button.Available())
		{
			OutPosition = Button.Rect.Center() * Layout.Scale;
			bFound = true;
		}
	});
	return bFound;
}
#endif

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
		return;
	}
	const FForces Forces = CountForces(Context);
	DrawUnitHealthBars(Paint, Context);
	DrawHeadquartersOverlays(Paint, Context);
	DrawBuildingOverlays(Paint, Context);
	DrawSectorOverlays(Paint, Context);
	DrawForceLabels(Paint, Context);

	DrawTopBar(Paint, Context, Forces, Layout);
	DrawMinimap(Paint, Controller, Layout);
	DrawObjectiveAlerts(Paint, Context, Layout);
	if (Context.bExpanded)
	{
		DrawBuildPanel(Paint, Layout);
		Paint.Panel(Layout.Inspector);
		if (Context.Building)
			DrawBuildingInspector(Paint, Context, Layout.Inspector);
		else
			DrawOverview(Paint, Context, Forces, Layout.Inspector);
	}
	else
		DrawModeBar(Paint, Context, Layout);
	ForEachButton(Context, Layout, [&Paint, &Context, Hover](const FButton& Button) {
		if (Button.Action == EHUDAction::Menu)
			DrawScreenButton(Paint, Button, Button.Action == Hover);
		else
			DrawButton(Paint, Context, Button, Button.Action == Hover);
	});
	if (Layout.bFeedback)
		DrawFeedback(Paint, Context, Layout);
}
