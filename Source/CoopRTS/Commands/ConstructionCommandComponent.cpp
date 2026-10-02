#include "ConstructionCommandComponent.h"
#include "CommandBuilding.h"
#include "CommandPlayerController.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
#include "Engine/World.h"

UConstructionCommandComponent::UConstructionCommandComponent()
{
	SetIsReplicatedByDefault(true);
}

void UConstructionCommandComponent::ServerPlaceBuilding_Implementation(int32 BuildingIndex, FVector Location)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FCommandService::PlaceBuilding(Controller->GetPlayerState<ACommandPlayerState>(), BuildingIndex, Location);
	UNetDriver* Driver = Controller->GetWorld()->GetNetDriver();
	const uint64 BuildingNetGUID = Result.Building && Driver && Driver->GetNetGuidCache()
		? Driver->GetNetGuidCache()->GetOrAssignNetGUID(Result.Building).ObjectId
		: 0;
	ClientPlacementFeedback(Result.Message, Result.IsAccepted(), Result.Building, BuildingNetGUID);
}

void UConstructionCommandComponent::ServerCancelBuilding_Implementation(ACommandBuilding* Building)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FCommandService::CancelBuilding(Controller->GetPlayerState<ACommandPlayerState>(), Building);
	ClientConstructionFeedback(Result.Message, Result.IsAccepted());
}

void UConstructionCommandComponent::ClientPlacementFeedback_Implementation(const FString& Message, bool bAccepted, ACommandBuilding* Building, uint64 BuildingNetGUID)
{
	CastChecked<ACommandPlayerController>(GetOwner())->SetPlacementFeedback(Message, bAccepted, Building, BuildingNetGUID);
}

void UConstructionCommandComponent::ClientConstructionFeedback_Implementation(const FString& Message, bool bAccepted)
{
	CastChecked<ACommandPlayerController>(GetOwner())->SetCommandFeedback(Message, bAccepted);
}
