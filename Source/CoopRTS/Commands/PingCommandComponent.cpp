#include "PingCommandComponent.h"
#include "CommandPlayerController.h"
#include "CommandService.h"
#include "CoopAudioSubsystem.h"

UPingCommandComponent::UPingCommandComponent()
{
	SetIsReplicatedByDefault(true);
	Events.Reserve(UObjectiveAnnouncer::HistoryLimit);
}

void UPingCommandComponent::ServerPing_Implementation(const FVector& Location, AArmyGroup* Force)
{
	const FCommandResult Result = FCommandService::Ping(CastChecked<ACommandPlayerController>(GetOwner()), Location, Force);
	ClientPingFeedback(Result.Message, Result.IsAccepted());
}

void UPingCommandComponent::ClientReceivePing_Implementation(const FObjectiveEvent& Event)
{
	const bool bFull = Events.Num() == UObjectiveAnnouncer::HistoryLimit;
	FObjectiveEvent& Stored = bFull ? Events[OldestEvent] : Events.AddDefaulted_GetRef();
	Stored = Event;
	Stored.Sequence = NextSequence--;
	if (bFull)
		OldestEvent = (OldestEvent + 1) % UObjectiveAnnouncer::HistoryLimit;
	if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
	{
		Audio->PlayUI(TEXT("Front"));
		Audio->PlayAnnouncer(Event.Id, Event.ServerTime);
	}
}

void UPingCommandComponent::ClientPingFeedback_Implementation(const FString& Message, bool bAccepted)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	++PingFeedbackSerial;
	bLastPingAccepted = bAccepted;
#endif
	CastChecked<ACommandPlayerController>(GetOwner())->SetCommandFeedback(Message, bAccepted);
}

void UPingCommandComponent::ResetForMatch()
{
	Events.Reset();
	OldestEvent = 0;
	NextSequence = -1;
	Throttle = PingPolicy::FThrottle();
}
