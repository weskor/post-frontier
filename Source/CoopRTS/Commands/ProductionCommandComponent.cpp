#include "ProductionCommandComponent.h"
#include "CommandPlayerController.h"

UProductionCommandComponent::UProductionCommandComponent()
{
	SetIsReplicatedByDefault(true);
}

void UProductionCommandComponent::ServerConfigureProduction_Implementation(ACommandBuilding* Building, EUnitRole Recipe, bool bEnabled)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FCommandService::ConfigureProduction(Controller->GetPlayerState<ACommandPlayerState>(), Building, Recipe, bEnabled);
	ClientConstructionFeedback(Result.Message, Result.IsAccepted());
}

void UProductionCommandComponent::ServerResearch_Implementation(ACommandBuilding* Building, EArmyDoctrine Choice)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FCommandService::Research(Controller->GetPlayerState<ACommandPlayerState>(), Building, Choice);
	ClientConstructionFeedback(Result.Message, Result.IsAccepted());
}

void UProductionCommandComponent::ClientConstructionFeedback_Implementation(const FString& Message, bool bAccepted)
{
	CastChecked<ACommandPlayerController>(GetOwner())->SetCommandFeedback(Message, bAccepted);
}
