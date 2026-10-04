#include "CommandPlayerController.h"

#include "CommandBuilding.h"
#include "CommandCamera.h"
#include "CommandGameState.h"
#include "Commands/PingCommandComponent.h"
#include "Commands/AbilityCommandComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HUD/TeamPanelFeed.h"
#include "Headquarters.h"
#include "InputCoreTypes.h"
#include "ObjectiveAnnouncer.h"
#include "Rules/ControllerInputPolicy.h"
#include "UnrealClient.h"

FVector2D ACommandPlayerController::GetEdgePanAxis() const
{
	const UGameViewportClient* Client = GetWorld()->GetGameViewport();
	const FViewport* Viewport = Client ? Client->Viewport : nullptr;
	float X, Y;
	if (!Viewport || !Viewport->HasFocus() || !Viewport->IsForegroundWindow() || !GetMousePosition(X, Y))
		return FVector2D::ZeroVector;
	return ControllerInputPolicy::EdgePanAxis(FVector2D(X, Y), Viewport->GetSizeXY());
}

void ACommandPlayerController::PanForward()
{
	if (!IsBuildHotkeyPending())
		PendingPan.X += 1.f;
}

void ACommandPlayerController::PanBackward()
{
	if (!IsBuildHotkeyPending())
		PendingPan.X -= 1.f;
}

void ACommandPlayerController::PanLeft()
{
	if (!IsBuildHotkeyPending())
		PendingPan.Y -= 1.f;
}

void ACommandPlayerController::PanRight()
{
	if (!IsBuildHotkeyPending())
		PendingPan.Y += 1.f;
}

void ACommandPlayerController::ZoomIn()
{
	if (GetUIScreen() != ECommandScreen::Game)
		return;
	bInitialFocusPending = false;
	if (auto* Camera = Cast<ACommandCamera>(GetPawn()))
		Camera->Zoom(1);
}

void ACommandPlayerController::ZoomOut()
{
	if (GetUIScreen() != ECommandScreen::Game)
		return;
	bInitialFocusPending = false;
	if (auto* Camera = Cast<ACommandCamera>(GetPawn()))
		Camera->Zoom(-1);
}

void ACommandPlayerController::UpdateCamera(float DeltaTime)
{
	PendingPan += GetEdgePanAxis();
	if (!PendingPan.IsNearlyZero())
		bInitialFocusPending = false;
	if (ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn()))
		Camera->Pan(PendingPan, DeltaTime);
	PendingPan = FVector2D::ZeroVector;
	if (bInitialFocusPending)
	{
		const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		if (State && IsValid(State->FriendlyHeadquarters))
		{
			if (ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn()))
			{
				Camera->FocusOn(State->FriendlyHeadquarters->GetActorLocation());
				bInitialFocusPending = false;
			}
		}
	}
	float MouseX, MouseY;
	if (IsInputKeyDown(EKeys::MiddleMouseButton) && GetMousePosition(MouseX, MouseY))
	{
		const FVector2D Position(MouseX, MouseY);
		if (bDragging && Position != PreviousDragPosition)
		{
			bInitialFocusPending = false;
			if (ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn()))
				Camera->Drag(Position - PreviousDragPosition);
		}
		PreviousDragPosition = Position;
		bDragging = true;
	}
	else
		bDragging = false;
}

void ACommandPlayerController::FocusSelection()
{
	if (GetUIScreen() != ECommandScreen::Game)
		return;
	FVector Target = FVector::ZeroVector;
	int32 Count = 0;
	for (const AArmyGroup* Force : SelectedForces)
		if (IsOwnedForce(Force))
		{
			Target += Force->GetCenter();
			++Count;
		}
	if (Count > 0)
		Target /= Count;
	else if (IsSelectableForce(InspectedForce))
		Target = InspectedForce->GetCenter();
	else if (IsOwnedBuilding(SelectedBuilding))
		Target = SelectedBuilding->GetActorLocation();
	else
	{
		const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		if (!State || !IsValid(State->FriendlyHeadquarters))
			return;
		Target = State->FriendlyHeadquarters->GetActorLocation();
	}
	bInitialFocusPending = false;
	if (ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn()))
		Camera->FocusOn(Target);
}

bool ACommandPlayerController::FocusAlertSequence(int32 Sequence)
{
	if (GetUIScreen() != ECommandScreen::Game)
		return false;
	// A gift row has no place to focus: it opens the Team panel, where the log is.
	if (GiftFeed::IsGiftSequence(Sequence))
	{
		TeamFlow.bOpen = true;
		return true;
	}
	if (Sequence < 0)
	{
		ACommandCamera* PingCamera = Cast<ACommandCamera>(GetPawn());
		if (!PingCamera)
			return false;
		// Team feed rows keep their own ring and stay out of the Space cycle.
		const bool bAbility = UAbilityCommandComponent::IsAbilitySequence(Sequence);
		for (const FObjectiveEvent& Event : bAbility ? AbilityCommands->GetEvents() : PingCommands->GetEvents())
			if (Event.Sequence == Sequence)
			{
				PingCamera->FocusOn(Event.Location);
				bInitialFocusPending = false;
				if (!bAbility)
					FocusedAlertSequence = Sequence;
				return true;
			}
		return false;
	}
	const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(this);
	ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn());
	if (!Announcer || !Camera)
		return false;
	const auto Events = Announcer->GetEvents();
	for (const FObjectiveEvent& Event : Events)
	{
		if (Event.Sequence != Sequence)
			continue;
		Camera->FocusOn(Event.Location);
		bInitialFocusPending = false;
		FocusedAlertSequence = Sequence;
		LatestAlertSequence = Events.Last().Sequence;
		return true;
	}
	return false;
}

void ACommandPlayerController::FocusAlert()
{
	const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(this);
	if (!Announcer || Announcer->GetEvents().IsEmpty())
		return;
	TArray<int32> Sequences;
	for (const FObjectiveEvent& Event : Announcer->GetEvents())
		Sequences.Add(Event.Sequence);
	FocusAlertSequence(ControllerInputPolicy::AlertCycleSequence(Sequences, LatestAlertSequence, FocusedAlertSequence));
}
