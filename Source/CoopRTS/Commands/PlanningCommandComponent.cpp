#include "PlanningCommandComponent.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/PlanningCommands.h"

UPlanningCommandComponent::UPlanningCommandComponent()
{
	SetIsReplicatedByDefault(true);
}

ACommandPlayerState* UPlanningCommandComponent::Commander() const
{
	return CastChecked<ACommandPlayerController>(GetOwner())->GetPlayerState<ACommandPlayerState>();
}

void UPlanningCommandComponent::ServerSetReady_Implementation(bool bReady)
{
	const FCommandResult Result = FPlanningCommands::SetReady(Commander(), bReady);
	ClientPlanningFeedback(Result.Message, Result.IsAccepted(), EPlanningEdit::Ready);
}

void UPlanningCommandComponent::ServerPlaceKit_Implementation(EBuildingKind Piece, FVector Location)
{
	const FCommandResult Result = FPlanningCommands::PlaceKit(Commander(), Piece, Location);
	ClientPlanningFeedback(Result.Message, Result.IsAccepted(), EPlanningEdit::Kit);
}

void UPlanningCommandComponent::ServerSetUnitType_Implementation(EUnitRole Role)
{
	const FCommandResult Result = FPlanningCommands::SetUnitType(Commander(), Role);
	ClientPlanningFeedback(Result.Message, Result.IsAccepted(), EPlanningEdit::UnitType);
}

void UPlanningCommandComponent::ServerSetFirstOrder_Implementation(EForceVerb Verb, int32 RegionIndex, AActor* Structure, bool bQueue)
{
	const FCommandResult Result = FPlanningCommands::SetFirstOrder(Commander(), Verb, RegionIndex, Structure, bQueue);
	ClientPlanningFeedback(Result.Message, Result.IsAccepted(), EPlanningEdit::FirstOrder);
}

void UPlanningCommandComponent::ServerClearFirstOrders_Implementation()
{
	const FCommandResult Result = FPlanningCommands::ClearFirstOrders(Commander());
	ClientPlanningFeedback(Result.Message, Result.IsAccepted(), EPlanningEdit::FirstOrder);
}

void UPlanningCommandComponent::ClientPlanningFeedback_Implementation(const FString& Message, bool bAccepted, EPlanningEdit Edit)
{
	CastChecked<ACommandPlayerController>(GetOwner())->CompletePlanningInput(Message, bAccepted, Edit);
}
