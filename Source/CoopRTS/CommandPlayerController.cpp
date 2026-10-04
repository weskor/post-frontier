#include "CommandPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Commands/ConstructionCommandComponent.h"
#include "Commands/ProductionCommandComponent.h"
#include "Commands/OrderCommandComponent.h"
#include "Commands/MatchCommandComponent.h"
#include "Commands/PingCommandComponent.h"
#include "Commands/AbilityCommandComponent.h"
#include "WorldOverlay.h"

ACommandPlayerController::ACommandPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	bShouldPerformFullTickWhenPaused = true;
	ConstructionCommands = CreateDefaultSubobject<UConstructionCommandComponent>(TEXT("ConstructionCommands"));
	ProductionCommands = CreateDefaultSubobject<UProductionCommandComponent>(TEXT("ProductionCommands"));
	OrderCommands = CreateDefaultSubobject<UOrderCommandComponent>(TEXT("OrderCommands"));
	MatchCommands = CreateDefaultSubobject<UMatchCommandComponent>(TEXT("MatchCommands"));
	PingCommands = CreateDefaultSubobject<UPingCommandComponent>(TEXT("PingCommands"));
	AbilityCommands = CreateDefaultSubobject<UAbilityCommandComponent>(TEXT("AbilityCommands"));
}

void ACommandPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		Screen = IsMenuWorld() ? ECommandScreen::MainMenu : ECommandScreen::Game;
	}
}

void ACommandPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (InputSubsystem.IsValid() && Mapping)
		InputSubsystem->RemoveMappingContext(Mapping);
	InputSubsystem.Reset();
	Super::EndPlay(EndPlayReason);
}

void ACommandPlayerController::ResetLocalMatchView()
{
	PingCommands->ResetForMatch();
	AbilityCommands->ResetForMatch();
	if (!IsLocalController())
		return;
	SelectedBuilding = nullptr;
	SelectedForces.Reset();
	InspectedForce = nullptr;
	LastClickedBuilding.Reset();
	LastBuildingClickTime = -1.;
	LastForceKey = 0;
	LastForceKeyTime = -1.;
	bSelectionDragging = false;
	bPlacingBuilding = false;
	bAssigningOrder = false;
	bFortifyTargeting = false;
	bFortifyCastPending = false;
	bHUDExpanded = true;
	bDeckPinned = false;
	bPlacementPending = false;
	bPlacementCancelled = false;
	PendingPlacedBuilding.Reset();
	PendingPlacedBuildingNetGUID = 0;
	Feedback.Reset();
	bBuildHotkeyPending = false;
	bRepeatPlacement = false;
	bInitialFocusPending = true;
	FocusedAlertSequence = 0;
	LatestAlertSequence = 0;
	PendingPan = FVector2D::ZeroVector;
	PreviousDragPosition = FVector2D::ZeroVector;
	bDragging = false;
	Screen = ECommandScreen::Game;
	ReturnScreen = ECommandScreen::Game;
	bTravelPending = false;
}

void ACommandPlayerController::GetSeamlessTravelActorList(bool bToEntry, TArray<AActor*>& ActorList)
{
	Super::GetSeamlessTravelActorList(bToEntry, ActorList);
	if (!bToEntry)
		ResetLocalMatchView();
}

void ACommandPlayerController::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();
	ResetLocalMatchView();
}

void ACommandPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController())
		return;
	if (!Feedback.IsEmpty() && GetFeedbackOpacity() <= 0.f)
		Feedback.Reset();
	if (bBuildHotkeyPending && !IsBuildHotkeyPending())
		bBuildHotkeyPending = false;
	if (GetUIScreen() != ECommandScreen::Game)
	{
		PendingPan = FVector2D::ZeroVector;
		bDragging = false;
		bSelectionDragging = false;
		return;
	}
	SelectPlacedBuilding();
	PruneSelection();
	UpdateCamera(DeltaTime);
	if (AWorldOverlay* Overlay = AWorldOverlay::Get(this))
		DrawWorldOverlay(*Overlay);
}
