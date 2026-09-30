#include "CommandPlayerController.h"
#include "ArenaBounds.h"
#include "CommandBuilding.h"
#include "CapturePoint.h"
#include "CommandCamera.h"
#include "CommandGameMode.h"
#include "CommandGameState.h"
#include "Content/MatchContent.h"
#include "CommandHUD.h"
#include "Headquarters.h"
#include "DrawDebugHelpers.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"

ACommandPlayerController::ACommandPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
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
	bAssigningFront = false;
	bHUDExpanded = true;
	bPlacementPending = false;
	Feedback.Reset();
	bInitialFocusPending = true;
	PendingPan = FVector2D::ZeroVector;
	PreviousDragPosition = FVector2D::ZeroVector;
	bDragging = false;
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
	Bind(TEXT("CancelMode"), EKeys::Escape, &ThisClass::CancelMode, ETriggerEvent::Started);
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
	if (SelectedBuilding && !IsOwnedBuilding(SelectedBuilding)) { SelectedBuilding = nullptr; bAssigningFront = false; }
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
	if (bPlacingBuilding)
	{
		FVector Location;
		FString Reason;
		bool bCanPlace = false;
		const UBuildingDefinition* Placement = GetPlacementDefinition();
		if (Placement && GetPlacementPreview(Location, Reason, bCanPlace))
		{
			const float Radius = ACommandBuilding::GetFootprintRadius(*Placement);
			DrawDebugCircle(GetWorld(), Location + FVector(0, 0, 12), Radius, 48,
				bCanPlace ? FColor::Green : FColor::Red, false, -1.f, 0, 4.f,
				FVector::ForwardVector, FVector::RightVector, false);
		}
		const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		if (Placement && State)
		{
			const float Footprint = ACommandBuilding::GetFootprintRadius(*Placement);
			if (!Placement->bEstablishesSector && IsValid(State->FriendlyHeadquarters))
				DrawDebugCircle(GetWorld(), State->FriendlyHeadquarters->GetActorLocation() + FVector(0, 0, 10),
					900.f - Footprint, 64, FColor::Cyan, false, -1.f, 0, 2.f,
					FVector::ForwardVector, FVector::RightVector, false);
			for (const ACapturePoint* Site : State->CaptureSites)
				if (IsValid(Site) && Site->ControllingTeam == 0
					&& (Placement->bEstablishesSector || Site->IsEstablishedForTeam(0)))
					DrawDebugCircle(GetWorld(), Site->GetActorLocation() + FVector(0, 0, 10),
						ACapturePoint::TerritoryRadius - Footprint, 64,
						Site->IsEstablishedForTeam(0) ? FColor::Green : FColor::Yellow,
						false, -1.f, 0, 2.f, FVector::ForwardVector, FVector::RightVector, false);
		}
	}
	if (bAssigningFront && IsOwnedBuilding(SelectedBuilding))
	{
		FVector Location;
		if (CursorGround(Location))
			DrawDebugCircle(GetWorld(), Location + FVector(0, 0, 13), 160.f, 32, FColor::Cyan,
				false, -1.f, 0, 3.f, FVector::ForwardVector, FVector::RightVector, false);
	}
	if (IsValid(SelectedBuilding) && SelectedBuilding->GetDefinition())
		DrawDebugCircle(GetWorld(), SelectedBuilding->GetActorLocation() + FVector(0, 0, 10),
			ACommandBuilding::GetFootprintRadius(*SelectedBuilding->GetDefinition()) + 30.f, 40, FColor::Cyan,
			false, -1.f, 0, 3.f, FVector::ForwardVector, FVector::RightVector, false);
	if (IsValid(SelectedBuilding) && SelectedBuilding->IsProducer()
		&& SelectedBuilding->HasConfiguredFront())
		DrawDebugCircle(GetWorld(), SelectedBuilding->FrontLocation + FVector(0, 0, 16), 125.f, 32,
			SelectedBuilding->FrontOrder == EFrontOrder::Secure ? FColor::Red
			: SelectedBuilding->FrontOrder == EFrontOrder::Defend ? FColor::Green : FColor::Yellow,
			false, -1.f, 0, 3.f, FVector::ForwardVector, FVector::RightVector, false);
}

void ACommandPlayerController::PanForward() { PendingPan.X += 1.f; }
void ACommandPlayerController::PanBackward() { PendingPan.X -= 1.f; }
void ACommandPlayerController::PanLeft() { PendingPan.Y -= 1.f; }
void ACommandPlayerController::PanRight() { PendingPan.Y += 1.f; }
void ACommandPlayerController::ZoomIn() { bInitialFocusPending = false; if (auto* Camera = Cast<ACommandCamera>(GetPawn())) Camera->Zoom(1); }
void ACommandPlayerController::ZoomOut() { bInitialFocusPending = false; if (auto* Camera = Cast<ACommandCamera>(GetPawn())) Camera->Zoom(-1); }

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
	if (!IsMatchTerminal()) return true;
	Feedback = TEXT("Match over: press Enter to restart.");
	return false;
}

void ACommandPlayerController::RequestRestart()
{
	if (IsMatchTerminal()) ServerRequestRestart();
}

void ACommandPlayerController::ServerRequestRestart_Implementation()
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (State && State->MatchResult != EMatchResult::Ongoing)
		if (ACommandGameMode* Mode = GetWorld()->GetAuthGameMode<ACommandGameMode>()) Mode->RequestRestart(this);
}

bool ACommandPlayerController::IsOwnedArmy(const AArmyGroup* Army) const
{
	const ACommandPlayerState* OwnState = GetPlayerState<ACommandPlayerState>();
3: 			Hostile->Initialize(FArmyGroupSpawn{5, State->EnemyCommander, -1, nullptr, Transform.GetLocation()});
4: 		if (Count > 6 || Production->ForceGroup != Recovery.Get() || Recovery->GetProductionBuilding() != Production.Get()
			|| State->EnemyCommander->Resources != 600 - ConstructionSpend - Count * 20
5: 	Group->Initialize({Owner ? 0 : 5, Wallet, Index, nullptr, Home});
6: 		if (It->GetTeamIndex() == Team) continue;
		for (const AArmyUnit* Unit : It->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive()) EnemyTroops.Add(Unit->GetActorLocation());
7: 	return IsValid(Army) && Army->GetWorld() == GetWorld() && Army->GetTeamIndex() == 0
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
	bAssigningFront = false;
	bPlacementPending = false;
	bHUDExpanded = true;
}

void ACommandPlayerController::ToggleHUD()
{
	bHUDExpanded = !bHUDExpanded;
}

bool ACommandPlayerController::HandleHUDClick(const FVector2D& Position)
{
	const ACommandHUD* HUD = Cast<ACommandHUD>(GetHUD());
	if (!HUD) return false;
	FVector WorldPosition;
	if (HUD->GetMinimapWorldPosition(Position, WorldPosition))
	{
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
	SelectedBuilding = IsOwnedBuilding(Building) ? Building : nullptr;
	if (SelectedBuilding) bHUDExpanded = true;
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
			return;
		}
		bPlacementPending = true;
		ServerPlaceBuilding(PlacementIndex, Location);
		Feedback = TEXT("Placement sent; server checks navigation and cost.");
		return;
	}
	if (bAssigningFront)
	{
		if (!CanIssueGameplayCommand()) return;
		FVector Location;
		if (IsOwnedBuilding(SelectedBuilding) && CursorGround(Location))
		{
			ServerAssignFront(SelectedBuilding, PendingFrontOrder, Location);
			bAssigningFront = false;
			bHUDExpanded = true;
			Feedback = TEXT("Front sent; awaiting server.");
		}
		else Feedback = TEXT("Point at ground to assign a front.");
		return;
	}
	FHitResult Hit;
	if (CursorHit(Hit)) SelectActor(Hit.GetActor());
}

void ACommandPlayerController::HandleHUDAction(EHUDAction Action)
{
	if (Action == EHUDAction::None) return;
	if (Action == EHUDAction::Construction) { CancelMode(); return; }
	if (!CanIssueGameplayCommand()) return;
	const int32 BuildIndex = BuildSlot(Action);
	if (BuildIndex != INDEX_NONE)
	{
		const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		if (!State || !IsValid(State->Content) || !State->Content->Building(BuildIndex)) return;
		PlacementIndex = BuildIndex;
		bPlacingBuilding = true;
		bAssigningFront = false;
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
	if (Action == EHUDAction::FrontSecure || Action == EHUDAction::FrontDefend || Action == EHUDAction::FrontFallBack)
	{
		PendingFrontOrder = Action == EHUDAction::FrontSecure ? EFrontOrder::Secure
			: Action == EHUDAction::FrontDefend ? EFrontOrder::Defend : EFrontOrder::FallBack;
		bAssigningFront = true;
		bPlacingBuilding = false;
		bHUDExpanded = false;
		Feedback = TEXT("Choose a front on ground; right-click/Esc cancels.");
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
		if (bPlacingBuilding || bAssigningFront)
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
	if (!IsValidBuildingCommand(Building)) { ClientConstructionFeedback(TEXT("Cancel rejected: not your living building or match ended.")); return; }
	ClientConstructionFeedback(Building->CancelConstruction() ? TEXT("Construction cancelled; unbuilt portion refunded.")
		: TEXT("Cancel rejected: building is complete."));
}

void ACommandPlayerController::ServerConfigureProduction_Implementation(ACommandBuilding* Building, EUnitRole Recipe, bool bEnabled)
{
	if (!IsValidBuildingCommand(Building)) { ClientConstructionFeedback(TEXT("Production rejected: not your living building or match ended.")); return; }
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const int32 UnitIndex = State && IsValid(State->Content) ? State->Content->UnitIndexForRole(Recipe) : -1;
	const bool bAccepted = Building->SetProduction(UnitIndex, bEnabled);
	const FString StateName = StaticEnum<EProductionState>()->GetNameStringByValue(static_cast<int64>(Building->GetProductionState()));
	ClientConstructionFeedback(bAccepted
		? FString::Printf(TEXT("%s: %s"), bEnabled ? TEXT("Enabled") : TEXT("Paused"), *StateName)
		: FString::Printf(TEXT("Production rejected: %s"), *StateName));
}

void ACommandPlayerController::ServerAssignFront_Implementation(ACommandBuilding* Building, EFrontOrder Order, FVector Location)
{
	if (!IsValidBuildingCommand(Building)) { ClientConstructionFeedback(TEXT("Front rejected: not your living building or match ended.")); return; }
	ClientConstructionFeedback(Building->SetFront(Order, Location) ? TEXT("Front assigned to this building's force.")
		: TEXT("Front rejected: select a completed barracks and valid ground inside the arena."));
}


void ACommandPlayerController::ServerResearch_Implementation(ACommandBuilding* Building, EArmyDoctrine Choice)
{
	if (!IsValidBuildingCommand(Building)) { ClientConstructionFeedback(TEXT("Research rejected: not your living building or match ended.")); return; }
	ClientConstructionFeedback(Building->TryResearch(Choice) ? TEXT("Workshop specialization purchased for your forces.")
		: FString::Printf(TEXT("Research rejected: requires completed workshop, no existing specialization, and %d resources."), ACommandBuilding::ResearchCost));
}

void ACommandPlayerController::ClientConstructionFeedback_Implementation(const FString& Message) { Feedback = Message; }

void ACommandPlayerController::ClientPlacementFeedback_Implementation(const FString& Message, bool bAccepted)
{
	Feedback = Message;
	bPlacementPending = false;
	if (bAccepted)
	{
		bPlacingBuilding = false;
		bHUDExpanded = true;
	}
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
