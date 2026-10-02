#include "OrderCommandComponent.h"
#include "CommandPlayerController.h"

UOrderCommandComponent::UOrderCommandComponent()
{
	SetIsReplicatedByDefault(true);
}

void UOrderCommandComponent::ServerAssignGoal_Implementation(ACommandBuilding* Building, EForceGoal Goal, int32 RegionIndex)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FCommandService::AssignGoal(Controller->GetPlayerState<ACommandPlayerState>(), Building, Goal, RegionIndex);
	ClientConstructionFeedback(Result.Message, Result.IsAccepted());
}

void UOrderCommandComponent::ServerAssignFront_Implementation(ACommandBuilding* Building, EFrontOrder Order, FVector Location)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FCommandService::AssignFront(Controller->GetPlayerState<ACommandPlayerState>(), Building, Order, Location);
	ClientConstructionFeedback(Result.Message, Result.IsAccepted());
}

void UOrderCommandComponent::ClientConstructionFeedback_Implementation(const FString& Message, bool bAccepted)
{
	CastChecked<ACommandPlayerController>(GetOwner())->SetCommandFeedback(Message, bAccepted);
}
