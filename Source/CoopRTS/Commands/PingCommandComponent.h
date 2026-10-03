#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ObjectiveAnnouncer.h"
#include "Rules/PingPolicy.h"
#include "PingCommandComponent.generated.h"

class AArmyGroup;
class FCommandService;

// Owner-only delivery: ping history never travels through the global GameState.
UCLASS()
class COOPRTS_API UPingCommandComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPingCommandComponent();
	UFUNCTION(Server, Reliable)
	void ServerPing(const FVector& Location, AArmyGroup* Force);
	UFUNCTION(Client, Reliable)
	void ClientReceivePing(const FObjectiveEvent& Event);
	UFUNCTION(Client, Reliable)
	void ClientPingFeedback(const FString& Message, bool bAccepted);
	FObjectiveEventView GetEvents() const { return { Events, OldestEvent }; }
	static constexpr float Lifetime = PingPolicy::LifetimeSeconds;
	void ResetForMatch();
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	uint32 PingFeedbackSerial = 0;
	bool bLastPingAccepted = false;
#endif
private:
	friend class FCommandService;
	PingPolicy::FThrottle Throttle;
	UPROPERTY(Transient)
	TArray<FObjectiveEvent> Events;
	int32 OldestEvent = 0;
	int32 NextSequence = -1;
};
