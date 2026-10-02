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
};
