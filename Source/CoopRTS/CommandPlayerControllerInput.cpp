#include "CommandPlayerController.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "Content/MatchContent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GroundHeight.h"
#include "HUD/HUDPanels.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Rules/ControllerInputPolicy.h"

void ACommandPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!IsLocalController())
		return;
	UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(InputComponent);
	Mapping = NewObject<UInputMappingContext>(this);
	auto Bind = [this, Input](const TCHAR* Name, FKey Key, void (ACommandPlayerController::*Method)(), ETriggerEvent Event) {
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = EInputActionValueType::Boolean;
		Action->bTriggerWhenPaused = true; // Active pause preserves all selection, camera and order input.
		Actions.Add(Action);
		Mapping->MapKey(Action, Key);
		Input->BindAction(Action, Event, this, Method);
	};
	Bind(TEXT("PanForward"), EKeys::Up, &ThisClass::PanForward, ETriggerEvent::Triggered);
	Bind(TEXT("PanBackward"), EKeys::Down, &ThisClass::PanBackward, ETriggerEvent::Triggered);
	Bind(TEXT("PanLeft"), EKeys::Left, &ThisClass::PanLeft, ETriggerEvent::Triggered);
	Bind(TEXT("PanRight"), EKeys::Right, &ThisClass::PanRight, ETriggerEvent::Triggered);
	Bind(TEXT("ZoomIn"), EKeys::MouseScrollUp, &ThisClass::ZoomIn, ETriggerEvent::Started);
	Bind(TEXT("ZoomOut"), EKeys::MouseScrollDown, &ThisClass::ZoomOut, ETriggerEvent::Started);
	Bind(TEXT("Select"), EKeys::LeftMouseButton, &ThisClass::SelectUnderCursor, ETriggerEvent::Started);
	Bind(TEXT("FinishSelection"), EKeys::LeftMouseButton, &ThisClass::FinishSelectionDrag, ETriggerEvent::Completed);
	Bind(TEXT("SelectForce1"), EKeys::One, &ThisClass::SelectForce1, ETriggerEvent::Started);
	Bind(TEXT("SelectForce2"), EKeys::Two, &ThisClass::SelectForce2, ETriggerEvent::Started);
	Bind(TEXT("SelectForce3"), EKeys::Three, &ThisClass::SelectForce3, ETriggerEvent::Started);
	Bind(TEXT("SelectForce4"), EKeys::Four, &ThisClass::SelectForce4, ETriggerEvent::Started);
	Bind(TEXT("SelectForce5"), EKeys::Five, &ThisClass::SelectForce5, ETriggerEvent::Started);
	Bind(TEXT("SmartOrder"), EKeys::RightMouseButton, &ThisClass::RightClickAtCursor, ETriggerEvent::Started);
	Bind(TEXT("FocusAlert"), EKeys::SpaceBar, &ThisClass::FocusAlert, ETriggerEvent::Started);
	Bind(TEXT("FocusSelection"), EKeys::F, &ThisClass::FocusSelection, ETriggerEvent::Started);
	Bind(TEXT("MenuOrCancel"), EKeys::Escape, &ThisClass::Escape, ETriggerEvent::Started);
	Bind(TEXT("ToggleHUD"), EKeys::F4, &ThisClass::ToggleHUD, ETriggerEvent::Started);
	Bind(TEXT("Restart"), EKeys::Enter, &ThisClass::RequestRestart, ETriggerEvent::Started);
	Bind(TEXT("ActivePause"), EKeys::P, &ThisClass::ToggleActivePause, ETriggerEvent::Started);
	Bind(TEXT("Ping"), EKeys::G, &ThisClass::PingAtCursor, ETriggerEvent::Started);
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		InputSubsystem = Subsystem;
		Subsystem->AddMappingContext(Mapping, 0);
	}
}

bool ACommandPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (GetUIScreen() == ECommandScreen::Game && Params.Event == IE_Pressed)
	{
		if (HandleBuildChordKey(Params.Key))
			return true;
		if (Params.Key == EKeys::A)
		{
			BeginForceAttack();
			return true;
		}
		if (Params.Key == EKeys::R)
		{
			RetreatSelectedForces(IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
			return true;
		}
	}
	return Super::InputKey(Params);
}

bool ACommandPlayerController::HandleBuildChordKey(const FKey& Key)
{
	if (!IsBuildHotkeyPending())
	{
		bBuildHotkeyPending = false;
		if (Key != EKeys::B)
			return false;
		BeginBuildChord();
		return true;
	}
	if (Key == EKeys::Escape || Key == EKeys::RightMouseButton)
	{
		CancelPointerMode();
		return true;
	}
	bBuildHotkeyPending = false;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	for (const auto& Hotkey : CommandHUDPanels::BuildHotkeys)
		if (Key == Hotkey.Key && State && IsValid(State->Content) && State->Content->Building(BuildSlot(Hotkey.Action)))
		{
			HandleHUDAction(Hotkey.Action);
			return true;
		}
	return false;
}

void ACommandPlayerController::BeginBuildChord()
{
	bBuildHotkeyPending = true;
	BuildHotkeyStarted = GetWorld()->GetRealTimeSeconds();
	PendingPan = FVector2D::ZeroVector;
	TStringBuilder<256> Hint;
	Hint << TEXT("Build: ");
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	bool bFirst = true;
	for (const auto& Hotkey : CommandHUDPanels::BuildHotkeys)
		if (const UBuildingDefinition* Definition = State && IsValid(State->Content) ? State->Content->Building(BuildSlot(Hotkey.Action)) : nullptr)
		{
			if (!bFirst)
				Hint << TEXT(" / ");
			Hint << Hotkey.Letter << TEXT(" ") << Definition->DisplayName.ToString();
			bFirst = false;
		}
	Hint << TEXT(". Choose within 2 s; any other key cancels.");
	SetFeedback(Hint.ToString());
}

bool ACommandPlayerController::IsBuildHotkeyPending() const
{
	return bBuildHotkeyPending && ControllerInputPolicy::IsBuildHotkeyLive(GetWorld()->GetRealTimeSeconds(), BuildHotkeyStarted);
}

bool ACommandPlayerController::CursorHit(FHitResult& Hit) const
{
	return GetHitResultUnderCursor(ECC_Visibility, false, Hit);
}

bool ACommandPlayerController::CursorGround(FVector& Location) const
{
	float X, Y;
	FVector Origin, Direction;
	if (!GetMousePosition(X, Y) || !DeprojectScreenPositionToWorld(X, Y, Origin, Direction)
		|| FMath::Abs(Direction.Z) < KINDA_SMALL_NUMBER)
		return false;
	// Height-correct pick: the first plateau, ramp or floor under the cursor; maps without tagged ground keep the z = 0 plane.
	if (GroundHeight::Ray(*GetWorld(), Origin, Direction, Location))
		return true;
	// Single precision, as before the controller was split; the shared GroundPoint rule is double.
	const float Time = -Origin.Z / Direction.Z;
	if (Time <= 0.f || !FMath::IsFinite(Time))
		return false;
	Location = Origin + Direction * Time;
	Location.Z = 0.f;
	return !Location.ContainsNaN();
}
