#include "MatchCommandComponent.h"
#include "CommandPlayerController.h"

UMatchCommandComponent::UMatchCommandComponent()
{
	SetIsReplicatedByDefault(true);
}

void UMatchCommandComponent::ServerRequestRestart_Implementation()
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	FCommandService::Restart(Controller);
}
