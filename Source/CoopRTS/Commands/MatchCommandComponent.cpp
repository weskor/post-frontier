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

void UMatchCommandComponent::ServerPause_Implementation()
{
	const FCommandResult Result = FCommandService::Pause(CastChecked<ACommandPlayerController>(GetOwner()));
	ClientPauseFeedback(Result.Message, Result.IsAccepted());
}

void UMatchCommandComponent::ServerResume_Implementation()
{
	const FCommandResult Result = FCommandService::Resume(CastChecked<ACommandPlayerController>(GetOwner()));
	ClientPauseFeedback(Result.Message, Result.IsAccepted());
}

void UMatchCommandComponent::ClientPauseFeedback_Implementation(const FString& Message, bool bAccepted)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	++PauseFeedbackSerial;
	bLastPauseAccepted = bAccepted;
#endif
	CastChecked<ACommandPlayerController>(GetOwner())->SetCommandFeedback(Message, bAccepted);
}
