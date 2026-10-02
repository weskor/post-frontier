#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CommandService.h"
#include "MatchCommandComponent.generated.h"

UCLASS()
class COOPRTS_API UMatchCommandComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UMatchCommandComponent();
	UFUNCTION(Server, Reliable)
	void ServerRequestRestart();
	UFUNCTION(Server, Reliable)
	void ServerPause();
	UFUNCTION(Server, Reliable)
	void ServerResume();
	UFUNCTION(Client, Reliable)
	void ClientPauseFeedback(const FString& Message, bool bAccepted);
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	uint32 PauseFeedbackSerial = 0;
	bool bLastPauseAccepted = false;
#endif
};
