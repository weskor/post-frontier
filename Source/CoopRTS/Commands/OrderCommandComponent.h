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
	void ServerIssueForceOrder(const TArray<AArmyGroup*>& Forces, EForceVerb Verb, int32 RegionIndex, AActor* Structure, bool bQueue);
	UFUNCTION(Server, Reliable)
	void ServerSetRetreatThreshold(const TArray<AArmyGroup*>& Forces, ERetreatThreshold Threshold);
	UFUNCTION(Server, Reliable)
	void ServerSetRallyPoint(ACommandBuilding* Building, int32 RegionIndex);
	UFUNCTION(Client, Reliable)
	void ClientConstructionFeedback(const FString& Message, bool bAccepted);
};
