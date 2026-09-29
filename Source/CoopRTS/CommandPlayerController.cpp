#include "CommandPlayerController.h"
#include "ArmyUnit.h"
#include "CommandCamera.h"
#include "CommandGameMode.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "Headquarters.h"
#include "DrawDebugHelpers.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
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
		// RTS pointing uses a visible absolute cursor, not a confined FPS cursor.
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
	}
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
	Bind(TEXT("Move"), EKeys::RightMouseButton, &ThisClass::MoveUnderCursor, ETriggerEvent::Started);
	Bind(TEXT("Attack"), EKeys::Q, &ThisClass::AttackUnderCursor, ETriggerEvent::Started);
	Bind(TEXT("Hold"), EKeys::H, &ThisClass::Hold, ETriggerEvent::Started);
	Bind(TEXT("Retreat"), EKeys::R, &ThisClass::Retreat, ETriggerEvent::Started);
	Bind(TEXT("ArmyOne"), EKeys::One, &ThisClass::SelectArmyOne, ETriggerEvent::Started);
	Bind(TEXT("ArmyTwo"), EKeys::Two, &ThisClass::SelectArmyTwo, ETriggerEvent::Started);
	Bind(TEXT("DoctrineSiegeOptics"), EKeys::F1, &ThisClass::ChooseSiegeOptics, ETriggerEvent::Started);
	Bind(TEXT("DoctrineFieldRepairs"), EKeys::F2, &ThisClass::ChooseFieldRepairs, ETriggerEvent::Started);
	Bind(TEXT("DoctrineEntrenchedFrontline"), EKeys::F3, &ThisClass::ChooseEntrenchedFrontline, ETriggerEvent::Started);
	Bind(TEXT("Reinforce"), EKeys::N, &ThisClass::Reinforce, ETriggerEvent::Started);
	Bind(TEXT("Restart"), EKeys::Enter, &ThisClass::RequestRestart, ETriggerEvent::Started);
	Bind(TEXT("FocusArmy"), EKeys::SpaceBar, &ThisClass::FocusArmy, ETriggerEvent::Started);
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(Mapping, 0);
	}
}

void ACommandPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController()) return;
	if (const ACommandPlayerState* OwnState = GetPlayerState<ACommandPlayerState>())
	{
		if (OwnState->Doctrine != LastObservedDoctrine)
		{
			LastObservedDoctrine = OwnState->Doctrine;
			DoctrineFeedback.Reset();
		}
	}
	if (SelectedArmy && !IsOwnedArmy(SelectedArmy)) SelectedArmy = nullptr;
	if (!PendingPan.IsNearlyZero())
	{
		bInitialFocusPending = false;
	}
	if (ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn())) Camera->Pan(PendingPan, DeltaTime);
	PendingPan = FVector2D::ZeroVector;
	if (PendingArmyIndex >= 0 && !SelectedArmy && (SelectionRetry -= DeltaTime) <= 0.f)
	{
		SelectionRetry = .25f;
		for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
		{
			if (It->ArmyIndex == PendingArmyIndex && IsOwnedArmy(*It))
			{
				SelectedArmy = *It;
				PendingArmyIndex = -1;
				break;
			}
		}
	}
	if (bInitialFocusPending && SelectedArmy)
	{
		if (ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn()))
		{
			Camera->FocusOn(SelectedArmy->GetCenter());
			bInitialFocusPending = false;
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
	else bDragging = false;
	if (!IsValid(SelectedArmy)) return;
	for (AArmyUnit* Unit : SelectedArmy->Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive()) continue;
		FVector Position = Unit->GetActorLocation();
		Position.Z = 8.f;
		DrawDebugCircle(GetWorld(), Position, 60.f, 32, FColor::Cyan, false, -1.f, 0, 3.f, FVector::ForwardVector, FVector::RightVector, false);
	}
	FVector Home = SelectedArmy->HomeLocation;
	Home.Z = 12.f;
	DrawDebugCircle(GetWorld(), Home, 240.f, 40, FColor::White, false, -1.f, 0, 3.f, FVector::ForwardVector, FVector::RightVector, false);
	FVector Destination = SelectedArmy->Destination;
	Destination.Z = 10.f;
	const FColor Color = SelectedArmy->Order == EArmyOrder::Retreat ? FColor::Orange
		: SelectedArmy->Order == EArmyOrder::Attack ? FColor::Red : FColor::Green;
	DrawDebugCircle(GetWorld(), Destination, 110.f, 40, Color, false, -1.f, 0, 5.f, FVector::ForwardVector, FVector::RightVector, false);
	DrawDebugLine(GetWorld(), SelectedArmy->GetCenter() + FVector(0, 0, 20), Destination, Color, false, -1.f, 0, 2.f);
	if (SelectedArmy->Order == EArmyOrder::Attack && IsValid(SelectedArmy->AttackTarget))
	{
		const AActor* Target = SelectedArmy->AttackTarget.Get();
		const AArmyUnit* Unit = Cast<AArmyUnit>(Target);
		const AHeadquarters* HQ = Cast<AHeadquarters>(Target);
		if ((Unit && Unit->IsAlive()) || (HQ && HQ->IsAlive()))
		{
			FVector TargetPosition = Target->GetActorLocation();
			TargetPosition.Z = 16.f;
			DrawDebugCircle(GetWorld(), TargetPosition, 85.f, 32, FColor::Red, false, -1.f, 0, 4.f,
				FVector::ForwardVector, FVector::RightVector, false);
			DrawDebugLine(GetWorld(), Destination, TargetPosition, FColor::Red, false, -1.f, 0, 2.f);
		}
	}
}

void ACommandPlayerController::PanForward() { PendingPan.X += 1.; }
void ACommandPlayerController::PanBackward() { PendingPan.X -= 1.; }
void ACommandPlayerController::PanLeft() { PendingPan.Y -= 1.; }
void ACommandPlayerController::PanRight() { PendingPan.Y += 1.; }
void ACommandPlayerController::ZoomIn()
{
	bInitialFocusPending = false;
	if (auto* C = Cast<ACommandCamera>(GetPawn())) C->Zoom(1);
}
void ACommandPlayerController::ZoomOut()
{
	bInitialFocusPending = false;
	if (auto* C = Cast<ACommandCamera>(GetPawn())) C->Zoom(-1);
}

bool ACommandPlayerController::CursorHit(FHitResult& Hit) const
{
	return GetHitResultUnderCursor(ECC_Visibility, false, Hit);
}
bool ACommandPlayerController::IsMatchTerminal() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return State && State->MatchResult != EMatchResult::Ongoing;
}

bool ACommandPlayerController::CanIssueGameplayCommand()
{
	if (!IsMatchTerminal()) return true;
	OrderFeedback = TEXT("Match over: press Enter to restart.");
	return false;
}

void ACommandPlayerController::RequestRestart()
{
	if (IsMatchTerminal())
	{
		DoctrineFeedback.Reset();
		ServerRequestRestart();
	}
}

void ACommandPlayerController::ServerRequestRestart_Implementation()
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (State && State->MatchResult != EMatchResult::Ongoing)
		if (ACommandGameMode* Mode = GetWorld()->GetAuthGameMode<ACommandGameMode>())
			Mode->RequestRestart(this);
}


void ACommandPlayerController::ChooseSiegeOptics() { ChooseDoctrine(EArmyDoctrine::SiegeOptics); }
void ACommandPlayerController::ChooseFieldRepairs() { ChooseDoctrine(EArmyDoctrine::FieldRepairs); }
void ACommandPlayerController::ChooseEntrenchedFrontline() { ChooseDoctrine(EArmyDoctrine::EntrenchedFrontline); }

void ACommandPlayerController::ChooseDoctrine(EArmyDoctrine Choice)
{
	DoctrineFeedback = TEXT("Doctrine choice sent; awaiting server.");
	ServerChooseDoctrine(Choice);
}

bool ACommandPlayerController::IsOwnedArmy(const AArmyGroup* Army) const
{
	const ACommandPlayerState* OwnState = GetPlayerState<ACommandPlayerState>();
	return IsValid(Army) && Army->GetWorld() == GetWorld() && Army->TeamIndex == 0
		&& !Army->bOpposingArmy && IsValid(OwnState) && OwnState->CommanderIndex >= 0
		&& OwnState->CommanderIndex < 5 && Army->OwningPlayerState == OwnState;
}

bool ACommandPlayerController::IsCursorOverDoctrinePanel() const
{
	float MouseX, MouseY;
	const ACommandHUD* CommandHUD = Cast<ACommandHUD>(GetHUD());
	return CommandHUD && GetMousePosition(MouseX, MouseY)
		&& CommandHUD->IsDoctrinePanelPoint(FVector2D(MouseX, MouseY));
}

void ACommandPlayerController::ServerChooseDoctrine_Implementation(EArmyDoctrine Choice)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State || State->MatchResult != EMatchResult::Ongoing)
	{
		ClientDoctrineFeedback(TEXT("Doctrine rejected: match over or unavailable."));
		return;
	}
	ACommandPlayerState* OwnState = GetPlayerState<ACommandPlayerState>();
	if (!IsValid(OwnState) || OwnState->GetWorld() != GetWorld())
	{
		ClientDoctrineFeedback(TEXT("Doctrine rejected: player state unavailable."));
		return;
	}
	if (Choice != EArmyDoctrine::SiegeOptics && Choice != EArmyDoctrine::FieldRepairs
		&& Choice != EArmyDoctrine::EntrenchedFrontline)
	{
		ClientDoctrineFeedback(TEXT("Doctrine rejected: invalid choice."));
		return;
	}
	if (OwnState->Doctrine != EArmyDoctrine::None)
	{
		ClientDoctrineFeedback(TEXT("Doctrine rejected: already chosen for this match."));
		return;
	}
	if (!OwnState->TryChooseDoctrine(Choice))
		ClientDoctrineFeedback(TEXT("Doctrine rejected: choice unavailable."));
}

void ACommandPlayerController::ClientDoctrineFeedback_Implementation(const FString& Message)
{
	DoctrineFeedback = Message;
}

void ACommandPlayerController::SelectUnderCursor()
{
	if (IsCursorOverDoctrinePanel())
	{
		float MouseX, MouseY;
		if (GetMousePosition(MouseX, MouseY))
			if (const ACommandHUD* CommandHUD = Cast<ACommandHUD>(GetHUD()))
			{
				const EArmyDoctrine Choice = CommandHUD->GetDoctrineAtScreenPosition(FVector2D(MouseX, MouseY));
				if (Choice != EArmyDoctrine::None) ChooseDoctrine(Choice);
			}
		return;
	}
	FHitResult Hit;
	PendingArmyIndex = -1;
	bInitialFocusPending = false;
	SelectedArmy = nullptr;
	OrderFeedback.Reset();
	if (CursorHit(Hit))
	{
		if (AArmyUnit* Unit = Cast<AArmyUnit>(Hit.GetActor()))
		{
			if (IsValid(Unit) && IsOwnedArmy(Unit->Group)) SelectedArmy = Unit->Group;
		}
	}
}

void ACommandPlayerController::SelectArmyOne() { SelectArmy(0); }
void ACommandPlayerController::SelectArmyTwo() { SelectArmy(1); }

void ACommandPlayerController::SelectArmy(int32 ArmyIndex)
{
	PendingArmyIndex = ArmyIndex;
	bInitialFocusPending = false;
	SelectedArmy = nullptr;
	OrderFeedback.Reset();
	SelectionRetry = 0.f;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
	{
		if (It->ArmyIndex == ArmyIndex && IsOwnedArmy(*It))
		{
			SelectedArmy = *It;
			PendingArmyIndex = -1;
			return;
		}
	}
}

void ACommandPlayerController::FocusArmy()
{
	if (IsOwnedArmy(SelectedArmy))
	{
		bInitialFocusPending = false;
		if (auto* Camera = Cast<ACommandCamera>(GetPawn())) Camera->FocusOn(SelectedArmy->GetCenter());
	}
}

void ACommandPlayerController::MoveUnderCursor()
{
	if (IsCursorOverDoctrinePanel()) return;
	if (!CanIssueGameplayCommand()) return;
	FHitResult Hit;
	if (IsOwnedArmy(SelectedArmy) && CursorHit(Hit)) ServerIssueOrder(SelectedArmy, EArmyOrder::Move, Hit.ImpactPoint);
}

void ACommandPlayerController::AttackUnderCursor()
{
	if (IsCursorOverDoctrinePanel()) return;
	if (!CanIssueGameplayCommand()) return;
	if (!IsOwnedArmy(SelectedArmy))
	{
		OrderFeedback = TEXT("Attack rejected: select your army first.");
		return;
	}
	FHitResult Hit;
	if (!CursorHit(Hit))
	{
		OrderFeedback = TEXT("Attack rejected: point at an enemy unit, enemy HQ or reachable ground.");
		return;
	}
	AActor* HitActor = Hit.GetActor();
	// Boot's traversable arena floor is a flat static mesh at Z=0. Other actor hits
	// remain explicit targets, so unsupported geometry cannot silently become an area order.
	const bool bGround = !HitActor || (Cast<AStaticMeshActor>(HitActor)
		&& FMath::Abs(Hit.ImpactPoint.Z) <= 5.f && Hit.ImpactNormal.Z > .8f);
	AActor* Target = bGround ? nullptr : HitActor;
	ServerIssueAttack(SelectedArmy, Hit.ImpactPoint, Target);
}
void ACommandPlayerController::Hold() { if (CanIssueGameplayCommand() && IsOwnedArmy(SelectedArmy)) ServerIssueOrder(SelectedArmy, EArmyOrder::Hold, FVector::ZeroVector); }
void ACommandPlayerController::Retreat() { if (CanIssueGameplayCommand() && IsOwnedArmy(SelectedArmy)) ServerIssueOrder(SelectedArmy, EArmyOrder::Retreat, FVector::ZeroVector); }
void ACommandPlayerController::Reinforce()
{
	if (!CanIssueGameplayCommand()) return;
	if (!IsOwnedArmy(SelectedArmy))
	{
		OrderFeedback = TEXT("Recovery rejected: select your army with 1 / 2 first.");
		return;
	}
	ServerReinforce(SelectedArmy);
}

void ACommandPlayerController::ServerIssueAttack_Implementation(AArmyGroup* Army, FVector Destination, AActor* Target)
{
	bool bAccepted = false;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (State && State->MatchResult == EMatchResult::Ongoing && IsOwnedArmy(Army)
		&& Army->GetOwner() == this)
	{
		if (Target)
		{
			bool bValidTarget = false;
			if (IsValid(Target) && Target->GetWorld() == GetWorld())
			{
				if (const AArmyUnit* Unit = Cast<AArmyUnit>(Target))
					bValidTarget = Unit->IsAlive() && IsValid(Unit->Group)
						&& Unit->Group->GetWorld() == GetWorld() && Unit->Group->TeamIndex == Unit->TeamIndex
						&& Unit->TeamIndex != Army->TeamIndex;
				else if (const AHeadquarters* HQ = Cast<AHeadquarters>(Target))
					bValidTarget = HQ->IsAlive() && HQ->TeamIndex != Army->TeamIndex;
			}
			if (!bValidTarget)
			{
				ClientAttackFeedback(false);
				return;
			}
			Destination = Target->GetActorLocation();
		}
		if (!Destination.ContainsNaN() && FMath::Abs(Destination.X) <= 4500.f
			&& FMath::Abs(Destination.Y) <= 4500.f && FMath::Abs(Destination.Z) <= 1000.f)
		{
			bAccepted = Army->IssueAttack(Destination, Target);
		}
	}
	ClientAttackFeedback(bAccepted);
}

void ACommandPlayerController::ClientAttackFeedback_Implementation(bool bAccepted)
{
	OrderFeedback = bAccepted ? TEXT("Attack order accepted.") :
		TEXT("Attack rejected: choose a live enemy unit, enemy HQ or reachable ground inside the arena.");
}

void ACommandPlayerController::ServerIssueOrder_Implementation(AArmyGroup* Army, EArmyOrder Order, FVector Destination)
{
	bool Accepted = false;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (State && State->MatchResult == EMatchResult::Ongoing && IsOwnedArmy(Army)
		&& Army->GetOwner() == this && !Destination.ContainsNaN()
		&& FMath::Abs(Destination.X) <= 4500.f && FMath::Abs(Destination.Y) <= 4500.f && FMath::Abs(Destination.Z) <= 1000.f)
	{
		switch (Order)
		{
		case EArmyOrder::Move: Accepted = Army->IssueMove(Destination); break;
		case EArmyOrder::Hold: Accepted = Army->IssueHold(); break;
		case EArmyOrder::Retreat: Accepted = Army->IssueRetreat(); break;
		default: break;
		}
	}
	ClientOrderFeedback(Accepted);
}

void ACommandPlayerController::ClientOrderFeedback_Implementation(bool bAccepted)
{
	OrderFeedback = bAccepted ? TEXT("") : TEXT("Order rejected: choose reachable ground inside the arena.");
}

void ACommandPlayerController::ServerReinforce_Implementation(AArmyGroup* Army)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State || State->MatchResult != EMatchResult::Ongoing)
	{
		ClientReinforcementFeedback(TEXT("Recovery rejected: match over."));
		return;
	}
	if (!IsOwnedArmy(Army) || Army->GetOwner() != this)
	{
		ClientReinforcementFeedback(TEXT("Recovery rejected: this is not your army."));
		return;
	}

	const int32 Cost = Army->GetReinforcementCost();
	bool bWasEmpty = true;
	for (const AArmyUnit* Unit : Army->Units)
	{
		if (IsValid(Unit) && Unit->IsAlive()) { bWasEmpty = false; break; }
	}
	FVector Source;
	bool bBase = false;
	const bool bHasSource = Army->GetReinforcementSource(Source, bBase);
	if (Army->TryReinforce())
	{
		ClientReinforcementFeedback(FString::Printf(TEXT("%s at %s for %d resources."),
			bWasEmpty ? TEXT("Army rebuilt") : TEXT("Casualties restored"),
			bHasSource && !bBase ? TEXT("forward site") : TEXT("base"), Cost));
	}
	else
	{
		const FString Status = Army->GetReinforcementStatus();
		ClientReinforcementFeedback(Status == TEXT("READY")
			? TEXT("Recovery rejected: deployment failed; no resources spent.")
			: FString::Printf(TEXT("Recovery rejected: %s"), *Status));
	}
}

void ACommandPlayerController::ClientReinforcementFeedback_Implementation(const FString& Message)
{
	OrderFeedback = Message;
}
