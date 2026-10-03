#include "OrderCommandComponent.h"
#include "CommandPlayerController.h"

UOrderCommandComponent::UOrderCommandComponent()
{
	SetIsReplicatedByDefault(true);
}

void UOrderCommandComponent::ServerIssueForceOrder_Implementation(const TArray<AArmyGroup*>& Forces, EForceVerb Verb, int32 RegionIndex, AActor* Structure, bool bQueue, uint32 AttackInputId)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FCommandService::IssueForceOrder(Controller->GetPlayerState<ACommandPlayerState>(), Forces, Verb, RegionIndex, Structure, bQueue);
	ClientForceOrderFeedback(Result.Message, Result.IsAccepted(), AttackInputId);
}

void UOrderCommandComponent::ServerSetRetreatThreshold_Implementation(const TArray<AArmyGroup*>& Forces, ERetreatThreshold Threshold)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FCommandService::SetRetreatThreshold(Controller->GetPlayerState<ACommandPlayerState>(), Forces, Threshold);
	ClientConstructionFeedback(Result.Message, Result.IsAccepted());
}

void UOrderCommandComponent::ServerSetRallyPoint_Implementation(ACommandBuilding* Building, int32 RegionIndex)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FCommandService::SetRallyPoint(Controller->GetPlayerState<ACommandPlayerState>(), Building, RegionIndex);
	ClientConstructionFeedback(Result.Message, Result.IsAccepted());
}

void UOrderCommandComponent::ClientConstructionFeedback_Implementation(const FString& Message, bool bAccepted)
{
	CastChecked<ACommandPlayerController>(GetOwner())->SetCommandFeedback(Message, bAccepted);
}

void UOrderCommandComponent::ClientForceOrderFeedback_Implementation(const FString& Message, bool bAccepted, uint32 AttackInputId)
{
	CastChecked<ACommandPlayerController>(GetOwner())->CompleteOrderInput(Message, bAccepted, AttackInputId);
}
