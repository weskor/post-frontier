#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CommandService.h"
#include "ConstructionCommandComponent.generated.h"

UCLASS()
class COOPRTS_API UConstructionCommandComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UConstructionCommandComponent();
	UFUNCTION(Server, Reliable)
	void ServerPlaceBuilding(int32 BuildingIndex, FVector Location);
	UFUNCTION(Server, Reliable)
	void ServerCancelBuilding(ACommandBuilding* Building);
	UFUNCTION(Client, Reliable)
	void ClientPlacementFeedback(const FString& Message, bool bAccepted, ACommandBuilding* Building, uint64 BuildingNetGUID);
	UFUNCTION(Client, Reliable)
	void ClientConstructionFeedback(const FString& Message, bool bAccepted);
};
