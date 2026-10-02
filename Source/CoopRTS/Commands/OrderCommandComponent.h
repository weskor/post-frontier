#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CommandService.h"
#include "OrderCommandComponent.generated.h"

UCLASS()
class COOPRTS_API UOrderCommandComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UOrderCommandComponent();
	UFUNCTION(Server, Reliable)
	void ServerAssignGoal(ACommandBuilding* Building, EForceGoal Goal, int32 RegionIndex);
	UFUNCTION(Client, Reliable)
	void ClientConstructionFeedback(const FString& Message, bool bAccepted);
};
