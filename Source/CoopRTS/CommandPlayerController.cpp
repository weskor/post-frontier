#include "CommandPlayerController.h"
#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CapturePoint.h"
#include "CommandCamera.h"
#include "CommandGameMode.h"
#include "CommandGameState.h"
#include "Content/MatchContent.h"
#include "CommandHUD.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "Components/BoxComponent.h"
#include "WorldOverlay.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "CommandMenuGameMode.h"
#include "CoopAudioSubsystem.h"
#include "CoopSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Rules/PlacementPolicy.h"

ACommandPlayerController::ACommandPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	bShouldPerformFullTickWhenPaused = true;
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
	if (InputSubsystem.IsValid() && Mapping) InputSubsystem->RemoveMappingContext(Mapping);
	InputSubsystem.Reset();
	Super::EndPlay(EndPlayReason);
}

void ACommandPlayerController::ResetLocalMatchView()
{
	if (!IsLocalController()) return;
	SelectedBuilding = nullptr;
	bPlacingBuilding = false;
	bAssigningGoal = false;
	bHUDExpanded = true;
	bPlacementPending = false;
	Feedback.Reset();
	bInitialFocusPending = true;
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
	if (!bToEntry) ResetLocalMatchView();
}

void ACommandPlayerController::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();
	ResetLocalMatchView();
}

void ACommandPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!IsLocalController()) return;
	UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(InputComponent);
	Mapping = NewObject<UInputMappingContext>(this);
	auto Bind = [this, Input](const TCHAR* Name, FKey Key, void (ACommandPlayerController::*Method)(), ETriggerEvent Event)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = EInputActionValueType::Boolean;
		Action->bTriggerWhenPaused = Key == EKeys::LeftMouseButton || Key == EKeys::Escape || Key == EKeys::Enter;
		Actions.Add(Action);
		Mapping->MapKey(Action, Key);
		Input->BindAction(Action, Event, this, Method);
	};
	Bind(TEXT("PanForward"), EKeys::W, &ThisClass::PanForward, ETriggerEvent::Triggered);
	Bind(TEXT("PanBackward"), EKeys::S, &ThisClass::PanBackward, ETriggerEvent::Triggered);
	Bind(TEXT("PanLeft"), EKeys::A, &ThisClass::PanLeft, ETriggerEvent::Triggered);
	Bind(TEXT("PanRight"), EKeys::D, &ThisClass::PanRight, ETriggerEvent::Triggered);
	Bind(TEXT("ZoomIn"), EKeys::MouseScrollUp, &ThisClass::ZoomIn, ETriggerEvent::Started);
	Bind(TEXT("ZoomOut"), EKeys::MouseScrollDown, &ThisClass::ZoomOut, ETriggerEvent::Started);
	Bind(TEXT("Select"), EKeys::LeftMouseButton, &ThisClass::SelectUnderCursor, ETriggerEvent::Started);
	Bind(TEXT("CancelPointerMode"), EKeys::RightMouseButton, &ThisClass::CancelPointerMode, ETriggerEvent::Started);
	Bind(TEXT("FocusSelection"), EKeys::SpaceBar, &ThisClass::FocusSelection, ETriggerEvent::Started);
	Bind(TEXT("MenuOrCancel"), EKeys::Escape, &ThisClass::Escape, ETriggerEvent::Started);
	Bind(TEXT("ToggleHUD"), EKeys::F4, &ThisClass::ToggleHUD, ETriggerEvent::Started);
	Bind(TEXT("Restart"), EKeys::Enter, &ThisClass::RequestRestart, ETriggerEvent::Started);
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		InputSubsystem = Subsystem;
		Subsystem->AddMappingContext(Mapping, 0);
	}
}

void ACommandPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController()) return;
	if (GetUIScreen() != ECommandScreen::Game)
	{
		PendingPan = FVector2D::ZeroVector;
		bDragging = false;
		return;
	}
	if (SelectedBuilding && !IsOwnedBuilding(SelectedBuilding)) { SelectedBuilding = nullptr; bAssigningGoal = false; }
	if (!PendingPan.IsNearlyZero()) bInitialFocusPending = false;
	if (ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn())) Camera->Pan(PendingPan, DeltaTime);
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
			if (ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn())) Camera->Drag(Position - PreviousDragPosition);
		}
		PreviousDragPosition = Position;
		bDragging = true;
	}
	else bDragging = false;
	AWorldOverlay* Overlay = AWorldOverlay::Get(this);
	if (!Overlay) return;
	if (bPlacingBuilding)
	{
		FVector Location;
		FString Reason;
		bool bCanPlace = false;
		const UBuildingDefinition* Placement = GetPlacementDefinition();
		const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		if (Placement && State && GetPlacementPreview(Location, Reason, bCanPlace))
		{
			const float CellSize = PlacementPolicy::BuildGridCellSize;
			const int32 Cells = PlacementPolicy::FootprintCells(ACommandBuilding::GetFootprintRadius(*Placement));
			// Eight extra cells per side, capped at 22 x 22 territory queries per frame.
			const int32 WindowCells = FMath::Min(Cells + 16, 22);
			const int32 Margin = (WindowCells - Cells) / 2;
			const FVector FootprintMin = Location - FVector(Cells * CellSize * .5f, Cells * CellSize * .5f, 0.f);
			const FVector WindowMin = FootprintMin - FVector(Margin * CellSize, Margin * CellSize, 0.f);
			for (int32 X = 0; X < WindowCells; ++X)
				for (int32 Y = 0; Y < WindowCells; ++Y)
				{
					const FVector Center = WindowMin + FVector((X + .5f) * CellSize, (Y + .5f) * CellSize, 0.f);
					const bool bTerritory = State->IsInBuildTerritory(PlacementIndex, 0, Center);
					Overlay->Cell(Center + FVector(0.f, 0.f, 4.f),
						FVector2D(CellSize * .5f - 1.f, CellSize * .5f - 1.f),
						bTerritory ? FColor(20, 65, 30, 40) : FColor(65, 20, 20, 40));
				}
			for (int32 Line = 0; Line <= WindowCells; ++Line)
			{
				const FVector Offset = WindowMin + FVector(0.f, 0.f, 8.f);
				Overlay->Line(Offset + FVector(Line * CellSize, 0.f, 0.f),
					Offset + FVector(Line * CellSize, WindowCells * CellSize, 0.f), FColor(80, 100, 110), 1.f);
				Overlay->Line(Offset + FVector(0.f, Line * CellSize, 0.f),
					Offset + FVector(WindowCells * CellSize, Line * CellSize, 0.f), FColor(80, 100, 110), 1.f);
			}
			for (int32 X = 0; X < Cells; ++X)
				for (int32 Y = 0; Y < Cells; ++Y)
					Overlay->Cell(FootprintMin + FVector((X + .5f) * CellSize, (Y + .5f) * CellSize, 12.f),
						FVector2D(CellSize * .5f - 2.f, CellSize * .5f - 2.f),
						bCanPlace ? FColor(0, 220, 45, 180) : FColor(240, 25, 20, 180));
		}
	}
	const auto OutlineRegion = [Overlay](const AMapRegion* Region, FColor Color)
	{
		if (!IsValid(Region) || Region->Polygon.Num() < 3) return;
		for (int32 Index = 0; Index < Region->Polygon.Num(); ++Index)
		{
			const FVector2D& A = Region->Polygon[Index];
			const FVector2D& B = Region->Polygon[(Index + 1) % Region->Polygon.Num()];
			Overlay->Line(FVector(A.X, A.Y, 13.f), FVector(B.X, B.Y, 13.f), Color, 3.f);
		}
	};
	const AMapRegion* HoveredRegion = bAssigningGoal && IsOwnedBuilding(SelectedBuilding) ? CursorGoalRegion() : nullptr;
	if (IsOwnedBuilding(SelectedBuilding) && SelectedBuilding->IsProducer())
	{
		const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		if (State)
			for (const AMapRegion* Region : State->Regions)
				if (IsValid(Region) && Region->RegionIndex == SelectedBuilding->GoalRegionIndex)
				{
					if (Region != HoveredRegion)
						OutlineRegion(Region, SelectedBuilding->ForceGoal == EForceGoal::Assault ? FColor::Red
							: SelectedBuilding->ForceGoal == EForceGoal::FallBack ? FColor::Yellow
							: SelectedBuilding->ForceGoal == EForceGoal::Expand ? FColor(82, 204, 255) : FColor::Green);
					break;
				}
	}
	OutlineRegion(HoveredRegion, FColor::Cyan);
	if (IsValid(SelectedBuilding) && SelectedBuilding->GetDefinition())
	{
		if (const UBoxComponent* Footprint = Cast<UBoxComponent>(SelectedBuilding->GetRootComponent()))
		{
			const FVector Extent = Footprint->GetScaledBoxExtent();
			Overlay->Square(Footprint->GetComponentLocation() + FVector(0.f, 0.f, 10.f - Extent.Z),
				FVector2D(Extent.X + 30.f, Extent.Y + 30.f), FColor::Cyan, 3.f);
		}
	}
	if (IsValid(SelectedBuilding) && SelectedBuilding->IsProducer()
		&& SelectedBuilding->HasConfiguredFront())
	{
		const FVector Base = SelectedBuilding->FrontLocation + FVector(0.f, 0.f, 16.f);
		const FColor Color = SelectedBuilding->FrontOrder == EFrontOrder::Secure ? FColor::Red
			: SelectedBuilding->FrontOrder == EFrontOrder::Defend ? FColor::Green : FColor::Yellow;
		Overlay->Line(Base, Base + FVector(0.f, 0.f, 250.f), Color, 3.f);
		Overlay->Square(Base, FVector2D(75.f, 75.f), Color, 3.f);
	}
	if (GetNetMode() != NM_DedicatedServer && IsOwnedBuilding(SelectedBuilding)
		&& SelectedBuilding->IsProducer() && IsValid(SelectedBuilding->ForceGroup))
	{
		const FVector Center = SelectedBuilding->ForceGroup->GetCenter() + FVector(0.f, 0.f, 24.f);
		Overlay->Line(SelectedBuilding->GetActorLocation() + FVector(0.f, 0.f, 24.f),
			Center, FColor::Cyan, 2.f);
		if (SelectedBuilding->HasConfiguredFront())
			Overlay->Line(Center, SelectedBuilding->FrontLocation + FVector(0.f, 0.f, 24.f),
				SelectedBuilding->FrontOrder == EFrontOrder::Secure ? FColor::Red
				: SelectedBuilding->FrontOrder == EFrontOrder::Defend ? FColor::Green : FColor::Yellow, 2.f);
	}
}

void ACommandPlayerController::PanForward() { PendingPan.X += 1.f; }
void ACommandPlayerController::PanBackward() { PendingPan.X -= 1.f; }
void ACommandPlayerController::PanLeft() { PendingPan.Y -= 1.f; }
void ACommandPlayerController::PanRight() { PendingPan.Y += 1.f; }
void ACommandPlayerController::ZoomIn() { if (GetUIScreen() != ECommandScreen::Game) return; bInitialFocusPending = false; if (auto* Camera = Cast<ACommandCamera>(GetPawn())) Camera->Zoom(1); }
void ACommandPlayerController::ZoomOut() { if (GetUIScreen() != ECommandScreen::Game) return; bInitialFocusPending = false; if (auto* Camera = Cast<ACommandCamera>(GetPawn())) Camera->Zoom(-1); }

bool ACommandPlayerController::CursorHit(FHitResult& Hit) const
{
	return GetHitResultUnderCursor(ECC_Visibility, false, Hit);
}

bool ACommandPlayerController::CursorGround(FVector& Location) const
{
	float X, Y;
	FVector Origin, Direction;
	if (!GetMousePosition(X, Y) || !DeprojectScreenPositionToWorld(X, Y, Origin, Direction)
		|| FMath::Abs(Direction.Z) < KINDA_SMALL_NUMBER) return false;
	const float Time = -Origin.Z / Direction.Z;
	if (Time <= 0.f || !FMath::IsFinite(Time)) return false;
	Location = Origin + Direction * Time;
	Location.Z = 0.f;
	return !Location.ContainsNaN();
}

const AMapRegion* ACommandPlayerController::CursorGoalRegion() const
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const ACommandHUD* HUD = Cast<ACommandHUD>(GetHUD());
	float X, Y;
	FVector Location;
	if (!State || !GetMousePosition(X, Y)) return nullptr;
	const FVector2D Position(X, Y);
	if (HUD && HUD->GetMinimapWorldPosition(Position, Location)) return State->FindRegionAt(Location);
	if (HUD && HUD->IsPanelPoint(Position)) return nullptr;
	return CursorGround(Location) ? State->FindRegionAt(Location) : nullptr;
}

void ACommandPlayerController::AssignGoalAt(const FVector& Location)
{
	if (!CanIssueGameplayCommand() || !IsOwnedBuilding(SelectedBuilding)) return;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const AMapRegion* Region = State ? State->FindRegionAt(Location) : nullptr;
	if (!Region)
	{
		Feedback = TEXT("Choose a region on the ground or minimap.");
		PlayUISound(TEXT("Reject"));
		return;
	}
	bAssigningGoal = false;
	bHUDExpanded = true;
	Feedback = TEXT("Goal sent; awaiting server.");
	ServerAssignGoal(SelectedBuilding, PendingGoal, Region->RegionIndex);
}

const UBuildingDefinition* ACommandPlayerController::GetPlacementDefinition() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return State && IsValid(State->Content) ? State->Content->Building(PlacementIndex) : nullptr;
}

bool ACommandPlayerController::GetPlacementPreview(FVector& Location, FString& Reason, bool& bCanPlace) const
{
	bCanPlace = false;
	if (!bPlacingBuilding) return false;
	if (!CursorGround(Location)) { Reason = TEXT("Point at ground to place."); return false; }
	if (const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>())
		Location = State->ResolveBuildingLocation(PlacementIndex, Location);
	bCanPlace = CanPlaceBuildingAt(PlacementIndex, Location, Reason);
	return true;
}

bool ACommandPlayerController::CanPlaceBuildingAt(int32 BuildingIndex, const FVector& Location, FString& Reason) const
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const ACommandPlayerState* Wallet = GetPlayerState<ACommandPlayerState>();
	if (!State || !Wallet || Wallet->CommanderIndex < 0)
	{
		Reason = TEXT("Territory and wallet syncing.");
		return false;
	}
	if (!State->ValidateBuildingPlacement(BuildingIndex, 0, Location, Reason)) return false;
	const int32 Cost = ACommandBuilding::GetBuildCost(*State->Content->Building(BuildingIndex));
	if (Wallet->Resources < Cost)
	{
		Reason = FString::Printf(TEXT("Need %d more resources."), Cost - Wallet->Resources);
		return false;
	}
	return true;
}

bool ACommandPlayerController::IsMatchTerminal() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return State && State->MatchResult != EMatchResult::Ongoing;
}

bool ACommandPlayerController::CanIssueGameplayCommand()
{
	if (GetUIScreen() == ECommandScreen::Game) return true;
	Feedback = TEXT("Match over: press Enter to restart.");
	return false;
}

void ACommandPlayerController::RequestRestart()
{
	if (GetUIScreen() == ECommandScreen::Result) ServerRequestRestart();
	else if (GetUIScreen() == ECommandScreen::MainMenu) HandleHUDAction(EHUDAction::PlaySolo);
}

void ACommandPlayerController::ServerRequestRestart_Implementation()
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (State && State->MatchResult != EMatchResult::Ongoing)
		if (ACommandGameMode* Mode = GetWorld()->GetAuthGameMode<ACommandGameMode>()) Mode->RequestRestart(this);
}

bool ACommandPlayerController::IsMenuWorld() const
{
	return GetWorld() && GetWorld()->GetAuthGameMode<ACommandMenuGameMode>() != nullptr;
}

ECommandScreen ACommandPlayerController::GetUIScreen() const
{
	return Screen == ECommandScreen::Game && IsMatchTerminal() ? ECommandScreen::Result : Screen;
}

float ACommandPlayerController::GetMasterVolume() const
{
	const UGameInstance* Instance = GetGameInstance();
	const UCoopAudioSubsystem* Audio = Instance ? Instance->GetSubsystem<UCoopAudioSubsystem>() : nullptr;
	return Audio ? Audio->GetMasterVolume() : 1.f;
}

void ACommandPlayerController::PlayUISound(FName Event)
{
	if (UGameInstance* Instance = GetGameInstance())
		if (UCoopAudioSubsystem* Audio = Instance->GetSubsystem<UCoopAudioSubsystem>()) Audio->PlayUI(Event);
}

void ACommandPlayerController::ShowScreen(ECommandScreen NewScreen)
{
	Screen = NewScreen;
	PendingPan = FVector2D::ZeroVector;
	bDragging = false;
	// Local menu overlays must never pause a listen host or its remote commanders.
	if (GetNetMode() == NM_Standalone && !IsMenuWorld())
		SetPause(NewScreen != ECommandScreen::Game && NewScreen != ECommandScreen::Result);
	UE_LOG(LogTemp, Display, TEXT("Solo screen=%d paused=%d"), static_cast<int32>(GetUIScreen()), GetWorld()->IsPaused());
}

void ACommandPlayerController::Escape()
{
	if (bPlacingBuilding || bAssigningGoal) { CancelMode(); return; }
	const ECommandScreen Current = GetUIScreen();
	if (Current == ECommandScreen::Game) ShowScreen(ECommandScreen::Pause);
	else if (Current == ECommandScreen::Pause) ShowScreen(ECommandScreen::Game);
	else if (Current == ECommandScreen::MainMenu)
	{
		ReturnScreen = Current;
		ShowScreen(ECommandScreen::ConfirmQuit);
	}
	else if (Current != ECommandScreen::Result) ShowScreen(ReturnScreen);
	PlayUISound(TEXT("Click"));
}

bool ACommandPlayerController::HandleScreenAction(EHUDAction Action)
{
	const bool bMapAction = Action == EHUDAction::MapV2 || Action == EHUDAction::MapClassic;
	if (!bMapAction && (static_cast<uint8>(Action) < static_cast<uint8>(EHUDAction::PlaySolo)
		|| static_cast<uint8>(Action) > static_cast<uint8>(EHUDAction::InviteFriends))) return false;
	if (bTravelPending) return true;
	const ECommandScreen Current = GetUIScreen();
	UCoopSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UCoopSessionSubsystem>();
	switch (Action)
	{
	case EHUDAction::MapV2:
	case EHUDAction::MapClassic:
		if (Current != ECommandScreen::MainMenu || !IsMenuWorld()) return true;
		if (Session) Session->SelectMap(Action == EHUDAction::MapV2);
		break;
	case EHUDAction::PlaySolo:
		if (Current != ECommandScreen::MainMenu || !IsMenuWorld()) return true;
		if (Session && Session->IsBusy()) return true;
		bTravelPending = true;
		UGameplayStatics::OpenLevel(this, Session ? Session->GetSelectedMap() : FName(TEXT("/Game/Maps/AvailabilityZoneV2")));
		break;
	case EHUDAction::HostCoop:
		if (Current != ECommandScreen::MainMenu || !IsMenuWorld()) return true;
		if (Session) Session->Host();
		break;
	case EHUDAction::InviteFriends:
		if (Current != ECommandScreen::Pause && Current != ECommandScreen::Result) return true;
		if (Session) Session->Invite();
		break;
	case EHUDAction::Menu:
		if (Current != ECommandScreen::Game) return true;
		CancelMode();
		ShowScreen(ECommandScreen::Pause);
		break;
	case EHUDAction::Resume:
		if (Current != ECommandScreen::Pause) return true;
		ShowScreen(ECommandScreen::Game);
		break;
	case EHUDAction::Controls:
	case EHUDAction::Audio:
		if (Current != ECommandScreen::MainMenu && Current != ECommandScreen::Pause && Current != ECommandScreen::Result) return true;
		ReturnScreen = Current;
		ShowScreen(Action == EHUDAction::Controls ? ECommandScreen::Controls : ECommandScreen::Audio);
		break;
	case EHUDAction::Back:
		if (Current != ECommandScreen::Controls && Current != ECommandScreen::Audio
			&& Current != ECommandScreen::ConfirmLeave && Current != ECommandScreen::ConfirmQuit) return true;
		ShowScreen(ReturnScreen);
		break;
	case EHUDAction::MainMenu:
	case EHUDAction::Quit:
		if (Current != ECommandScreen::MainMenu && Current != ECommandScreen::Pause && Current != ECommandScreen::Result) return true;
		if (Action == EHUDAction::MainMenu && IsMenuWorld()) return true;
		ReturnScreen = Current;
		ShowScreen(Action == EHUDAction::MainMenu ? ECommandScreen::ConfirmLeave : ECommandScreen::ConfirmQuit);
		break;
	case EHUDAction::ConfirmLeave:
		if (Current != ECommandScreen::ConfirmLeave) return true;
		SetPause(false);
		if (Session) Session->Leave();
		else UGameplayStatics::OpenLevel(this, TEXT("/Game/Maps/Menu"));
		break;
	case EHUDAction::ConfirmQuit:
		if (Current != ECommandScreen::ConfirmQuit) return true;
		if (Session) Session->Leave(true);
		else UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
		break;
	case EHUDAction::Restart:
		if (Current != ECommandScreen::Result) return true;
		SetPause(false);
		RequestRestart();
		break;
	case EHUDAction::VolumeDown:
	case EHUDAction::VolumeUp:
		if (Current != ECommandScreen::Audio) return true;
		if (UGameInstance* Instance = GetGameInstance())
			if (UCoopAudioSubsystem* Audio = Instance->GetSubsystem<UCoopAudioSubsystem>())
				Audio->SetMasterVolume(FMath::Clamp(Audio->GetMasterVolume() + (Action == EHUDAction::VolumeUp ? .1f : -.1f), 0.f, 1.f));
		break;
	default: return true;
	}
	PlayUISound(TEXT("Click"));
	return true;
}

bool ACommandPlayerController::IsOwnedArmy(const AArmyGroup* Army) const
{
	const ACommandPlayerState* OwnState = GetPlayerState<ACommandPlayerState>();
	return IsValid(Army) && Army->GetWorld() == GetWorld() && Army->GetTeamIndex() == 0
		&& IsValid(OwnState) && OwnState->TeamIndex == 0 && OwnState->CommanderIndex >= 0 && OwnState->CommanderIndex < 5
		&& Army->GetOwningPlayerState() == OwnState;
}

bool ACommandPlayerController::IsOwnedBuilding(const ACommandBuilding* Building) const
{
	const ACommandPlayerState* OwnState = GetPlayerState<ACommandPlayerState>();
	return IsValid(Building) && Building->GetWorld() == GetWorld() && Building->IsAlive()
		&& Building->TeamIndex == 0 && IsValid(OwnState) && OwnState->TeamIndex == 0 && OwnState->CommanderIndex >= 0
		&& OwnState->CommanderIndex < 5 && Building->OwningPlayerState == OwnState;
}

bool ACommandPlayerController::IsValidBuildingCommand(const ACommandBuilding* Building) const
{
	return !IsMatchTerminal() && IsOwnedBuilding(Building);
}

void ACommandPlayerController::CancelMode()
{
	bPlacingBuilding = false;
	bAssigningGoal = false;
	bPlacementPending = false;
	bHUDExpanded = true;
}

void ACommandPlayerController::ToggleHUD()
{
	if (GetUIScreen() != ECommandScreen::Game) return;
	bHUDExpanded = !bHUDExpanded;
}

bool ACommandPlayerController::HandleHUDClick(const FVector2D& Position)
{
	const ACommandHUD* HUD = Cast<ACommandHUD>(GetHUD());
	if (!HUD) return false;
	if (GetUIScreen() != ECommandScreen::Game)
	{
		HandleHUDAction(HUD->GetActionAtScreenPosition(Position));
		return true;
	}
	FVector WorldPosition;
	if (HUD->GetMinimapWorldPosition(Position, WorldPosition))
	{
		if (bAssigningGoal)
		{
			AssignGoalAt(WorldPosition);
			return true;
		}
		bInitialFocusPending = false;
		if (ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn())) Camera->FocusOn(WorldPosition);
		return true;
	}
	if (!HUD->IsPanelPoint(Position)) return false;
	HandleHUDAction(HUD->GetActionAtScreenPosition(Position));
	return true;
}

void ACommandPlayerController::SelectActor(AActor* Actor)
{
	bInitialFocusPending = false;
	ACommandBuilding* Building = Cast<ACommandBuilding>(Actor);
	if (const AArmyUnit* Unit = Cast<AArmyUnit>(Actor))
	{
		const AArmyGroup* Group = Unit->GetGroup();
		const ACommandPlayerState* OwnState = GetPlayerState<ACommandPlayerState>();
		if (IsValid(Unit) && Unit->IsAlive() && Unit->GetWorld() == GetWorld()
			&& Unit->GetTeamIndex() == 0 && IsValid(OwnState) && OwnState->TeamIndex == 0
			&& IsValid(Group) && Group->GetOwningPlayerState() == OwnState)
		{
			Building = Group->GetProductionBuilding();
			if (!IsOwnedBuilding(Building) || Building->IsActorBeingDestroyed() || !Building->IsProducer())
			{
				Building = nullptr;
				Feedback = TEXT("Barracks destroyed; survivors keep their last front.");
			}
		}
	}
	SelectedBuilding = IsOwnedBuilding(Building) ? Building : nullptr;
	if (SelectedBuilding) bHUDExpanded = true;
	if (SelectedBuilding) PlayUISound(TEXT("Select"));
}

void ACommandPlayerController::SelectUnderCursor()
{
	float MouseX, MouseY;
	if (GetMousePosition(MouseX, MouseY) && HandleHUDClick(FVector2D(MouseX, MouseY))) return;
	if (bPlacingBuilding)
	{
		if (!CanIssueGameplayCommand() || bPlacementPending) return;
		FVector Location;
		FString Reason;
		bool bCanPlace = false;
		if (!GetPlacementPreview(Location, Reason, bCanPlace) || !bCanPlace)
		{
			Feedback = Reason.IsEmpty() ? TEXT("Point at ground to place.") : Reason;
			PlayUISound(TEXT("Reject"));
			return;
		}
		bPlacementPending = true;
		ServerPlaceBuilding(PlacementIndex, Location);
		Feedback = TEXT("Placement sent; server checks navigation and cost.");
		return;
	}
	if (bAssigningGoal)
	{
		FVector Location;
		if (CursorGround(Location)) AssignGoalAt(Location);
		else Feedback = TEXT("Choose a region on the ground or minimap.");
		return;
	}
	FHitResult Hit;
	if (CursorHit(Hit)) SelectActor(Hit.GetActor());
}

void ACommandPlayerController::HandleHUDAction(EHUDAction Action)
{
	if (Action == EHUDAction::None) return;
	if (HandleScreenAction(Action)) return;
	if (GetUIScreen() != ECommandScreen::Game) return;
	const bool bGoalAction = Action == EHUDAction::GoalHold || Action == EHUDAction::GoalExpand
		|| Action == EHUDAction::GoalAssault || Action == EHUDAction::GoalFallBack;
	PlayUISound(bGoalAction ? TEXT("Front") : TEXT("Click"));
	if (Action == EHUDAction::Construction) { CancelMode(); return; }
	if (!CanIssueGameplayCommand()) return;
	const int32 BuildIndex = BuildSlot(Action);
	if (BuildIndex != INDEX_NONE)
	{
		const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		if (!State || !IsValid(State->Content) || !State->Content->Building(BuildIndex)) return;
		PlacementIndex = BuildIndex;
		bPlacingBuilding = true;
		bAssigningGoal = false;
		bHUDExpanded = false;
		Feedback = TEXT("Left-click valid ground; right-click/Esc cancels.");
		return;
	}
	if (!IsOwnedBuilding(SelectedBuilding)) { Feedback = TEXT("Select your building first."); return; }
	if (Action == EHUDAction::CancelConstruction)
	{
		ServerCancelBuilding(SelectedBuilding);
		return;
	}
	if (Action == EHUDAction::ResearchSiege || Action == EHUDAction::ResearchRepairs || Action == EHUDAction::ResearchEntrenched)
	{
		ServerResearch(SelectedBuilding, Action == EHUDAction::ResearchSiege ? EArmyDoctrine::SiegeOptics
			: Action == EHUDAction::ResearchRepairs ? EArmyDoctrine::FieldRepairs : EArmyDoctrine::EntrenchedFrontline);
		return;
	}
	if (!SelectedBuilding->IsProducer()) return;
	if (bGoalAction)
	{
		if (!SelectedBuilding->IsComplete() || !SelectedBuilding->bForceConfigured || !IsValid(SelectedBuilding->ForceGroup))
		{
			Feedback = TEXT("Complete this barracks and Start & Lock its force before assigning a goal.");
			PlayUISound(TEXT("Reject"));
			return;
		}
		PendingGoal = Action == EHUDAction::GoalHold ? EForceGoal::Hold
			: Action == EHUDAction::GoalExpand ? EForceGoal::Expand
			: Action == EHUDAction::GoalAssault ? EForceGoal::Assault : EForceGoal::FallBack;
		bPlacingBuilding = false;
		if (PendingGoal == EForceGoal::Assault || PendingGoal == EForceGoal::FallBack)
		{
			bAssigningGoal = false;
			bHUDExpanded = true;
			ServerAssignGoal(SelectedBuilding, PendingGoal, INDEX_NONE);
			return;
		}
		bAssigningGoal = true;
		bHUDExpanded = false;
		Feedback = TEXT("Choose a region on ground or minimap; right-click/Esc cancels.");
		return;
	}
	if (Action == EHUDAction::ToggleProduction)
	{
		ServerConfigureProduction(SelectedBuilding, SelectedBuilding->ProductionRole, !SelectedBuilding->bProductionEnabled);
		return;
	}
	const int32 RecipeIndex = RecipeSlot(Action);
	if (RecipeIndex == INDEX_NONE) return;
	if (SelectedBuilding->bForceConfigured) return; // Locked even while paused; no role-change RPC.
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const UArmyUnitDefinition* Definition = State && IsValid(State->Content) ? State->Content->Unit(RecipeIndex) : nullptr;
	if (!Definition) return;
	ServerConfigureProduction(SelectedBuilding, Definition->Role, false);
}


void ACommandPlayerController::FocusSelection()
{
	if (GetUIScreen() != ECommandScreen::Game) return;
	FVector Target;
	if (IsOwnedBuilding(SelectedBuilding)) Target = SelectedBuilding->GetActorLocation();
	else
	{
		const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		if (!State || !IsValid(State->FriendlyHeadquarters)) return;
		Target = State->FriendlyHeadquarters->GetActorLocation();
	}
	bInitialFocusPending = false;
	if (ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn())) Camera->FocusOn(Target);
}

	void ACommandPlayerController::CancelPointerMode()
	{
		if (bPlacingBuilding || bAssigningGoal)
		{
			CancelMode();
			Feedback = TEXT("Mode cancelled.");
		}
	}

void ACommandPlayerController::ServerPlaceBuilding_Implementation(int32 BuildingIndex, FVector Location)
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	ACommandPlayerState* Wallet = GetPlayerState<ACommandPlayerState>();
	FString Reason;
	bool bAccepted = false;
	if (!State || State->MatchResult != EMatchResult::Ongoing || !IsValid(Wallet)
		|| Wallet->GetWorld() != GetWorld() || Wallet->TeamIndex != 0 || Wallet->CommanderIndex < 0 || Wallet->CommanderIndex >= 5)
		Reason = TEXT("Placement rejected: match or commander unavailable.");
	else if (!(bAccepted = State->TryPlaceBuilding(BuildingIndex, Location, Wallet, 0, Reason)))
	{
		if (Reason.IsEmpty()) Reason = TEXT("Placement rejected by server.");
	}
	else Reason = TEXT("Building placed; construction started.");
	ClientPlacementFeedback(Reason, bAccepted);
}

void ACommandPlayerController::ServerCancelBuilding_Implementation(ACommandBuilding* Building)
{
	if (!IsValidBuildingCommand(Building)) { ClientConstructionFeedback(TEXT("Cancel rejected: not your living building or match ended."), false); return; }
	const bool bAccepted = Building->CancelConstruction();
	ClientConstructionFeedback(bAccepted ? TEXT("Construction cancelled; unbuilt portion refunded.")
		: TEXT("Cancel rejected: building is complete."), bAccepted);
}

void ACommandPlayerController::ServerConfigureProduction_Implementation(ACommandBuilding* Building, EUnitRole Recipe, bool bEnabled)
{
	if (!IsValidBuildingCommand(Building)) { ClientConstructionFeedback(TEXT("Production rejected: not your living building or match ended."), false); return; }
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const int32 UnitIndex = State && IsValid(State->Content) ? State->Content->UnitIndexForRole(Recipe) : -1;
	const bool bAccepted = Building->SetProduction(UnitIndex, bEnabled);
	const FString StateName = StaticEnum<EProductionState>()->GetNameStringByValue(static_cast<int64>(Building->GetProductionState()));
	ClientConstructionFeedback(bAccepted
		? FString::Printf(TEXT("%s: %s"), bEnabled ? TEXT("Enabled") : TEXT("Paused"), *StateName)
		: FString::Printf(TEXT("Production rejected: %s"), *StateName), bAccepted);
}

void ACommandPlayerController::ServerAssignGoal_Implementation(ACommandBuilding* Building, EForceGoal Goal, int32 RegionIndex)
{
	if (!IsValidBuildingCommand(Building) || !Building->IsProducer() || !Building->IsComplete()
		|| !Building->bForceConfigured || !IsValid(Building->ForceGroup))
	{
		ClientConstructionFeedback(TEXT("Goal rejected: requires your completed, locked barracks in an ongoing match."), false);
		return;
	}
	const bool bAccepted = Building->SetGoal(Goal, RegionIndex);
	ClientConstructionFeedback(bAccepted ? TEXT("Goal assigned to this building's force.")
		: TEXT("Goal rejected: invalid or unreachable region, enemy main, or unavailable force."), bAccepted);
}

void ACommandPlayerController::ServerAssignFront_Implementation(ACommandBuilding* Building, EFrontOrder Order, FVector Location)
{
	if (!IsValidBuildingCommand(Building)) { ClientConstructionFeedback(TEXT("Front rejected: not your living building or match ended."), false); return; }
	const bool bAccepted = Building->SetFront(Order, Location);
	ClientConstructionFeedback(bAccepted ? TEXT("Front assigned to this building's force.")
		: TEXT("Front rejected: select a completed barracks and valid ground inside the arena."), bAccepted);
}


void ACommandPlayerController::ServerResearch_Implementation(ACommandBuilding* Building, EArmyDoctrine Choice)
{
	if (!IsValidBuildingCommand(Building)) { ClientConstructionFeedback(TEXT("Research rejected: not your living building or match ended."), false); return; }
	const bool bAccepted = Building->TryResearch(Choice);
	ClientConstructionFeedback(bAccepted ? TEXT("Workshop specialization purchased for your forces.")
		: FString::Printf(TEXT("Research rejected: requires completed workshop, no existing specialization, and %d resources."), ACommandBuilding::ResearchCost), bAccepted);
}

void ACommandPlayerController::ClientConstructionFeedback_Implementation(const FString& Message, bool bAccepted)
{
	Feedback = Message;
	if (!bAccepted) PlayUISound(TEXT("Reject"));
}

void ACommandPlayerController::ClientPlacementFeedback_Implementation(const FString& Message, bool bAccepted)
{
	Feedback = Message;
	bPlacementPending = false;
	if (bAccepted)
	{
		bPlacingBuilding = false;
		bHUDExpanded = true;
	}
	else PlayUISound(TEXT("Reject"));
}

void ACommandPlayerController::ServerIssueAttack_Implementation(AArmyGroup* Army, FVector Destination, AActor* Target)
{
	bool bAccepted = false;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (State && State->MatchResult == EMatchResult::Ongoing && IsOwnedArmy(Army) && Army->GetOwner() == this)
	{
		if (Target)
		{
			bool bValidTarget = false;
			if (IsValid(Target) && Target->GetWorld() == GetWorld())
			{
				if (const AArmyUnit* Unit = Cast<AArmyUnit>(Target))
					bValidTarget = Unit->IsAlive() && IsValid(Unit->GetGroup())
						&& Unit->GetGroup()->GetWorld() == GetWorld() && Unit->GetGroup()->GetTeamIndex() == Unit->GetTeamIndex()
						&& Unit->GetTeamIndex() != Army->GetTeamIndex();
				else if (const AHeadquarters* HQ = Cast<AHeadquarters>(Target))
					bValidTarget = HQ->IsAlive() && HQ->TeamIndex != Army->GetTeamIndex();
				else if (const ACommandBuilding* Building = Cast<ACommandBuilding>(Target))
					bValidTarget = Building->IsAlive() && Building->TeamIndex != Army->GetTeamIndex();
			}
			if (!bValidTarget) { ClientAttackFeedback(false); return; }
			Destination = Target->GetActorLocation();
		}
		if (IsValid(State->Arena) && State->Arena->ContainsTravel(Destination))
			bAccepted = Army->IssueAttack(Destination, Target);
	}
	ClientAttackFeedback(bAccepted);
}

void ACommandPlayerController::ClientAttackFeedback_Implementation(bool bAccepted)
{
	Feedback = bAccepted ? TEXT("Attack order accepted; manual order overrides automatic front.")
		: TEXT("Attack rejected: choose a live enemy unit, building, HQ or reachable ground.");
}

void ACommandPlayerController::ServerIssueOrder_Implementation(AArmyGroup* Army, EArmyOrder Order, FVector Destination)
{
	bool bAccepted = false;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (State && State->MatchResult == EMatchResult::Ongoing && IsOwnedArmy(Army)
		&& Army->GetOwner() == this && IsValid(State->Arena) && State->Arena->ContainsTravel(Destination))
	{
		switch (Order)
		{
		case EArmyOrder::Move: bAccepted = Army->IssueMove(Destination); break;
		case EArmyOrder::Hold: bAccepted = Army->IssueHold(); break;
		case EArmyOrder::Retreat: bAccepted = Army->IssueRetreat(); break;
		default: break;
		}
	}
	ClientOrderFeedback(bAccepted);
}

void ACommandPlayerController::ClientOrderFeedback_Implementation(bool bAccepted)
{
	Feedback = bAccepted ? TEXT("Manual order accepted; automatic front disabled for this squad.")
		: TEXT("Order rejected: choose reachable ground inside the arena.");
}
