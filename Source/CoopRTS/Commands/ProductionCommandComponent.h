#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CommandService.h"
#include "ProductionCommandComponent.generated.h"

UCLASS()
class COOPRTS_API UProductionCommandComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProductionCommandComponent();
	UFUNCTION(Server, Reliable)
	void ServerConfigureProduction(ACommandBuilding* Building, EUnitRole Recipe, bool bEnabled);
	UFUNCTION(Server, Reliable)
	void ServerResearch(ACommandBuilding* Building, EArmyDoctrine Choice);
	UFUNCTION(Client, Reliable)
	void ClientConstructionFeedback(const FString& Message, bool bAccepted);
};
