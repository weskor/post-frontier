#include "BranchCommandComponent.h"
#include "BranchCommands.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"

UBranchCommandComponent::UBranchCommandComponent()
{
	SetIsReplicatedByDefault(true);
}

void UBranchCommandComponent::ServerPurchaseBranch_Implementation(ACommandBuilding* Building)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FBranchCommands::Purchase(Controller->GetPlayerState<ACommandPlayerState>(), Building);
	ClientBranchFeedback(Result.Message, Result.IsAccepted());
}

void UBranchCommandComponent::ClientBranchFeedback_Implementation(const FString& Message, bool bAccepted)
{
	CastChecked<ACommandPlayerController>(GetOwner())->SetCommandFeedback(Message, bAccepted);
}
