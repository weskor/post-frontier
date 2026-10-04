#include "CommandPlayerController.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Commands/ConstructionCommandComponent.h"
#include "Content/MatchContent.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
#include "InputCoreTypes.h"

const UBuildingDefinition* ACommandPlayerController::GetPlacementDefinition() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return State && IsValid(State->Content) ? State->Content->Building(PlacementIndex) : nullptr;
}

bool ACommandPlayerController::GetPlacementPreview(FVector& Location, FString& Reason, bool& bCanPlace) const
{
	bCanPlace = false;
	if (!bPlacingBuilding)
		return false;
	if (!CursorGround(Location))
	{
		Reason = TEXT("Point at ground to place.");
		return false;
	}
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
	if (!State->ValidateBuildingPlacement(BuildingIndex, 0, Location, Reason))
		return false;
	const int32 Cost = ACommandBuilding::GetBuildCost(*State->Content->Building(BuildingIndex));
	// A kit piece is free; the server's planning command decides everything else.
	if (!IsPlanningActive() && Wallet->Resources < Cost)
	{
		Reason = FString::Printf(TEXT("Need %d more resources."), Cost - Wallet->Resources);
		return false;
	}
	return true;
}

void ACommandPlayerController::BeginBuildingPlacement(int32 BuildIndex)
{
	if (bPlacementPending)
	{
		SetFeedback(TEXT("Waiting for placement confirmation."));
		return;
	}
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State || !IsValid(State->Content) || !State->Content->Building(BuildIndex))
		return;
	PendingPlacedBuilding.Reset();
	PendingPlacedBuildingNetGUID = 0;
	PlacementIndex = BuildIndex;
	bPlacingBuilding = true;
	bBuildHotkeyPending = false;
	bAssigningOrder = false;
	bFortifyTargeting = false;
	// The planning panel stays up: it carries the placement hints, and kit pieces are free.
	bHUDExpanded = IsPlanningActive();
	SetFeedback(IsPlanningActive() ? TEXT("Left-click valid ground to place or move it; right-click/Esc cancels.")
								   : TEXT("Left-click valid ground; Shift+LMB places another; right-click/Esc cancels."));
}

void ACommandPlayerController::ClickPlacement()
{
	if (!CanIssueGameplayCommand() || bPlacementPending)
		return;
	FVector Location;
	if (!CursorGround(Location))
	{
		SetFeedback(TEXT("Point at ground to place."));
		PlayUISound(TEXT("Reject"));
		return;
	}
	PlaceBuildingAt(Location, IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
}

void ACommandPlayerController::PlaceBuildingAt(const FVector& Requested, bool bRepeat)
{
	if (!bPlacingBuilding || bPlacementPending || !CanIssueGameplayCommand())
		return;
	FString Reason;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const FVector Location = State ? State->ResolveBuildingLocation(PlacementIndex, Requested) : Requested;
	if (IsPlanningActive())
	{
		SendKitPlacement(Location);
		return;
	}
	if (!CanPlaceBuildingAt(PlacementIndex, Location, Reason))
	{
		SetFeedback(Reason);
		PlayUISound(TEXT("Reject"));
		return;
	}
	bPlacementPending = true;
	bRepeatPlacement = bRepeat;
	SetFeedback(TEXT("Placement sent; server checks navigation and cost."));
	ConstructionCommands->ServerPlaceBuilding(PlacementIndex, Location);
}

void ACommandPlayerController::SetPlacementFeedback(const FString& Message, bool bAccepted, ACommandBuilding* Building, uint64 BuildingNetGUID)
{
	bPlacementPending = false;
	SetFeedback(Message);
	if (bPlacementCancelled)
	{
		bPlacementCancelled = false;
		return;
	}
	if (bAccepted)
	{
		if (bPlacingBuilding)
		{
			PendingPlacedBuilding = Building;
			PendingPlacedBuildingNetGUID = BuildingNetGUID;
			SelectPlacedBuilding();
			bPlacingBuilding = bRepeatPlacement;
			bHUDExpanded = !bRepeatPlacement;
		}
	}
	else
		PlayUISound(TEXT("Reject"));
}

void ACommandPlayerController::SelectPlacedBuilding()
{
	ACommandBuilding* Building = PendingPlacedBuilding.Get();
	if (!Building && PendingPlacedBuildingNetGUID == 0)
		return;
	if (!Building)
	{
		UNetDriver* Driver = GetWorld()->GetNetDriver();
		FNetworkGUID Guid;
		Guid.ObjectId = PendingPlacedBuildingNetGUID;
		if (Driver && Driver->GetNetGuidCache())
		{
			Building = Cast<ACommandBuilding>(Driver->GetNetGuidCache()->GetObjectFromNetGUID(Guid, false));
			PendingPlacedBuilding = Building;
		}
	}
	// The reply may arrive before the new actor or its replicated ownership.
	if (!IsOwnedBuilding(Building))
		return;
	SelectActor(Building);
	bHUDExpanded = !bPlacingBuilding;
}

void ACommandPlayerController::CancelMode()
{
	if (bPlacementPending)
		bPlacementCancelled = true;
	bPlacingBuilding = false;
	bAssigningOrder = false;
	bFortifyTargeting = false;
	bSelectionDragging = false;
	bBuildHotkeyPending = false;
	bRepeatPlacement = false;
	bHUDExpanded = true;
	bDeckPinned = false;
	PendingPlacedBuilding.Reset();
	PendingPlacedBuildingNetGUID = 0;
}

void ACommandPlayerController::CancelPointerMode()
{
	if (bPlacingBuilding || bAssigningOrder || bFortifyTargeting || bBuildHotkeyPending || bSelectionDragging)
	{
		CancelMode();
		SetFeedback(TEXT("Mode cancelled."));
	}
}
