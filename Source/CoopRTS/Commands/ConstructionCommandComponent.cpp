#include "ConstructionCommandComponent.h"
#include "CommandPlayerController.h"

UConstructionCommandComponent::UConstructionCommandComponent()
{
	SetIsReplicatedByDefault(true);
}

void UConstructionCommandComponent::ServerPlaceBuilding_Implementation(int32 BuildingIndex, FVector Location)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FCommandService::PlaceBuilding(Controller->GetPlayerState<ACommandPlayerState>(), BuildingIndex, Location);
	ClientPlacementFeedback(Result.Message, Result.IsAccepted());
}

void UConstructionCommandComponent::ServerCancelBuilding_Implementation(ACommandBuilding* Building)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FCommandService::CancelBuilding(Controller->GetPlayerState<ACommandPlayerState>(), Building);
	ClientConstructionFeedback(Result.Message, Result.IsAccepted());
}

void UConstructionCommandComponent::ClientPlacementFeedback_Implementation(const FString& Message, bool bAccepted)
{
	CastChecked<ACommandPlayerController>(GetOwner())->SetPlacementFeedback(Message, bAccepted);
}

void UConstructionCommandComponent::ClientConstructionFeedback_Implementation(const FString& Message, bool bAccepted)
{
	CastChecked<ACommandPlayerController>(GetOwner())->SetCommandFeedback(Message, bAccepted);
}
