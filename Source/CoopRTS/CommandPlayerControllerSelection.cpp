#include "CommandPlayerController.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "Commands/ForceCapState.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Rules/ControllerInputPolicy.h"
#include "Rules/ForceSelectionPolicy.h"

bool ACommandPlayerController::IsOwnedBuilding(const ACommandBuilding* Building) const
{
	const ACommandPlayerState* OwnState = GetPlayerState<ACommandPlayerState>();
	return IsValid(Building) && Building->GetWorld() == GetWorld() && Building->IsAlive()
		&& Building->TeamIndex == 0 && IsValid(OwnState) && OwnState->TeamIndex == 0 && OwnState->CommanderIndex >= 0
		&& OwnState->CommanderIndex < 5 && Building->OwningPlayerState == OwnState;
}

bool ACommandPlayerController::IsSelectableForce(const AArmyGroup* Force) const
{
	const ACommandPlayerState* OwnState = GetPlayerState<ACommandPlayerState>();
	if (!IsValid(Force) || Force->IsActorBeingDestroyed() || Force->GetWorld() != GetWorld()
		|| !IsValid(OwnState) || !IsValid(Force->GetOwningPlayerState()))
		return false;
	const ACommandPlayerState* Owner = Force->GetOwningPlayerState();
	const ACommandBuilding* Producer = Force->GetProductionBuilding();
	bool bAlive = IsValid(Producer) && Producer->IsAlive();
	if (!bAlive)
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive())
			{
				bAlive = true;
				break;
			}
	return ForceSelectionPolicy::ResolveAccess(OwnState->CommanderIndex, Owner->CommanderIndex,
			   OwnState->TeamIndex == 0 && Force->GetTeamIndex() == OwnState->TeamIndex, bAlive)
		!= ForceSelectionPolicy::EAccess::None;
}

bool ACommandPlayerController::IsOwnedForce(const AArmyGroup* Force) const
{
	return IsSelectableForce(Force) && Force->GetOwningPlayerState() == GetPlayerState<ACommandPlayerState>();
}

bool ACommandPlayerController::IsForceSelected(const AArmyGroup* Force) const
{
	return IsOwnedForce(Force) && SelectedForces.Contains(Force);
}

bool ACommandPlayerController::IsForceHighlighted(const AArmyGroup* Force) const
{
	return IsForceSelected(Force) || (IsOwnedBuilding(SelectedBuilding) && SelectedBuilding->ForceGroup == Force);
}

void ACommandPlayerController::PruneSelection()
{
	if (SelectedBuilding && !IsOwnedBuilding(SelectedBuilding))
	{
		SelectedBuilding = nullptr;
		bAssigningOrder = false;
	}
	SelectedForces.RemoveAll([this](const TObjectPtr<AArmyGroup>& Force) { return !IsOwnedForce(Force); });
	if (InspectedForce && !IsSelectableForce(InspectedForce))
		InspectedForce = nullptr;
}

void ACommandPlayerController::SelectForce(AArmyGroup* Force, bool bToggle)
{
	if (GetUIScreen() != ECommandScreen::Game || !IsSelectableForce(Force))
		return;
	bInitialFocusPending = false;
	PendingPlacedBuilding.Reset();
	PendingPlacedBuildingNetGUID = 0;
	bAssigningOrder = false;
	bFortifyTargeting = false;
	bPlacingBuilding = false;
	bHUDExpanded = true;
	SelectedBuilding = nullptr;
	InspectedForce = Force;
	if (!IsOwnedForce(Force))
	{
		if (!bToggle)
			SelectedForces.Reset();
		return;
	}
	if (!bToggle)
		SelectedForces.Reset();
	if (bToggle && SelectedForces.Contains(Force))
	{
		SelectedForces.Remove(Force);
		InspectedForce = SelectedForces.IsEmpty() ? nullptr : SelectedForces.Last().Get();
	}
	else
		SelectedForces.AddUnique(Force);
	PlayUISound(TEXT("Select"));
}

void ACommandPlayerController::SelectForceNumber(int32 Number)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (GetUIScreen() != ECommandScreen::Game || !State
		|| !ForceSelectionPolicy::IsNumberAvailable(Number, CommandForceCap::HumanCommanderCount(*State)))
		return;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
		if (IsOwnedForce(*It) && It->ForceNumber == Number)
		{
			const double Now = GetWorld()->GetRealTimeSeconds();
			const bool bFocus = LastForceKey == Number && ControllerInputPolicy::IsDoubleClick(Now, LastForceKeyTime) && IsForceSelected(*It);
			SelectForce(*It);
			LastForceKey = bFocus ? 0 : Number;
			LastForceKeyTime = Now;
			if (bFocus)
				FocusSelection();
			return;
		}
}

void ACommandPlayerController::SelectForceBox(const FVector2D& Start, const FVector2D& End, bool bAdd)
{
	if (GetUIScreen() != ECommandScreen::Game)
		return;
	const ACommandHUD* HUD = Cast<ACommandHUD>(GetHUD());
	if (!HUD)
		return;
	PendingPlacedBuilding.Reset();
	PendingPlacedBuildingNetGUID = 0;
	TArray<AArmyGroup*> Forces;
	HUD->GetForcesInScreenBox(Start, End, Forces);
	bInitialFocusPending = false;
	SelectedBuilding = nullptr;
	bAssigningOrder = false;
	bFortifyTargeting = false;
	bPlacingBuilding = false;
	if (!bAdd)
		SelectedForces.Reset();
	for (AArmyGroup* Force : Forces)
		if (IsOwnedForce(Force))
			SelectedForces.AddUnique(Force);
	InspectedForce = SelectedForces.IsEmpty() ? nullptr : SelectedForces.Last().Get();
	bHUDExpanded = true;
}

void ACommandPlayerController::FinishSelectionDrag()
{
	if (!bSelectionDragging)
		return;
	bSelectionDragging = false;
	float X, Y;
	if (GetMousePosition(X, Y) && FVector2D::DistSquared(SelectionDragStart, FVector2D(X, Y)) > FMath::Square(6.f))
		SelectForceBox(SelectionDragStart, FVector2D(X, Y), bSelectionDragAdd);
}

bool ACommandPlayerController::GetSelectionDrag(FVector2D& Start, FVector2D& End) const
{
	float X, Y;
	if (!bSelectionDragging || !GetMousePosition(X, Y))
		return false;
	Start = SelectionDragStart;
	End = FVector2D(X, Y);
	return FVector2D::DistSquared(Start, End) > FMath::Square(6.f);
}

void ACommandPlayerController::SelectActor(AActor* Actor, bool bToggle)
{
	if (GetUIScreen() != ECommandScreen::Game)
		return;
	PendingPlacedBuilding.Reset();
	PendingPlacedBuildingNetGUID = 0;
	ACommandBuilding* Building = Cast<ACommandBuilding>(Actor);
	const double Now = GetWorld()->GetRealTimeSeconds();
	const bool bDoubleClick = Building && LastClickedBuilding == Building && ControllerInputPolicy::IsDoubleClick(Now, LastBuildingClickTime);
	LastClickedBuilding = bDoubleClick ? nullptr : Building;
	LastBuildingClickTime = Now;
	SelectActorWithModifiers(Actor, bToggle, bDoubleClick);
}

void ACommandPlayerController::SelectActorWithModifiers(AActor* Actor, bool bToggle, bool bDoubleClick)
{
	if (GetUIScreen() != ECommandScreen::Game)
		return;
	bInitialFocusPending = false;
	if (AArmyGroup* Force = Cast<AArmyGroup>(Actor))
	{
		SelectForce(Force, bToggle);
		return;
	}
	if (const AArmyUnit* Unit = Cast<AArmyUnit>(Actor))
	{
		if (IsValid(Unit) && Unit->IsAlive() && Unit->GetWorld() == GetWorld())
			SelectForce(Unit->GetGroup(), bToggle);
		return;
	}
	ACommandBuilding* Building = Cast<ACommandBuilding>(Actor);
	if (IsValid(Building) && Building->GetWorld() == GetWorld() && Building->IsAlive() && Building->IsProducer()
		&& IsSelectableForce(Building->ForceGroup) && !IsOwnedForce(Building->ForceGroup))
	{
		SelectForce(Building->ForceGroup, bToggle);
		return;
	}
	if (bDoubleClick && IsOwnedBuilding(Building) && Building->IsProducer() && IsSelectableForce(Building->ForceGroup))
	{
		SelectForce(Building->ForceGroup, bToggle);
		return;
	}
	if (!bToggle)
	{
		SelectedForces.Reset();
		InspectedForce = nullptr;
		SelectedBuilding = IsOwnedBuilding(Building) ? Building : nullptr;
		bAssigningOrder = false;
		bFortifyTargeting = false;
	}
	if (SelectedBuilding)
	{
		bHUDExpanded = true;
		PlayUISound(TEXT("Select"));
	}
}

void ACommandPlayerController::SelectUnderCursor()
{
	float MouseX, MouseY;
	bSelectionDragging = false;
	if (GetMousePosition(MouseX, MouseY))
	{
		const FVector2D Position(MouseX, MouseY);
		const ACommandHUD* HUD = Cast<ACommandHUD>(GetHUD());
		bSelectionDragging = GetUIScreen() == ECommandScreen::Game && !bPlacingBuilding && !bAssigningOrder && !bFortifyTargeting
			&& (!HUD || !HUD->IsPanelPoint(Position));
		if (bSelectionDragging)
		{
			SelectionDragStart = Position;
			bSelectionDragAdd = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
		}
		if (HandleHUDClick(Position))
			return;
	}
	if (bPlacingBuilding)
	{
		ClickPlacement();
		return;
	}
	if (bAssigningOrder)
	{
		float X, Y;
		if (GetMousePosition(X, Y))
			HandleAttackTargetClick(FVector2D(X, Y));
		return;
	}
	if (bFortifyTargeting)
	{
		float X, Y;
		if (GetMousePosition(X, Y))
			HandleFortifyClick(FVector2D(X, Y));
		return;
	}
	const bool bToggle = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	FHitResult Hit;
	AActor* Actor = CursorHit(Hit) ? Hit.GetActor() : nullptr;
	SelectActor(Actor, bToggle);
}
