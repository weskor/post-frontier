#include "CommandPlayerController.h"
#include "CoopAudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "Rules/ControllerInputPolicy.h"

void ACommandPlayerController::SetFeedback(const FString& Message)
{
	Feedback = Message;
	FeedbackStarted = GetWorld()->GetRealTimeSeconds();
}

float ACommandPlayerController::GetFeedbackOpacity() const
{
	return Feedback.IsEmpty() ? 0.f : ControllerInputPolicy::FeedbackOpacity(GetWorld()->GetRealTimeSeconds() - FeedbackStarted);
}

void ACommandPlayerController::SetCommandFeedback(const FString& Message, bool bAccepted)
{
	SetFeedback(Message);
	if (!bAccepted)
		PlayUISound(TEXT("Reject"));
}

void ACommandPlayerController::PlayUISound(FName Event)
{
	if (UGameInstance* Instance = GetGameInstance())
		if (UCoopAudioSubsystem* Audio = Instance->GetSubsystem<UCoopAudioSubsystem>())
			Audio->PlayUI(Event);
}
