#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BranchCommandComponent.generated.h"

class ACommandBuilding;

// Owner RPCs of the tier-2 branch purchase; the rules live in FBranchCommands.
UCLASS()
class COOPRTS_API UBranchCommandComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBranchCommandComponent();
	UFUNCTION(Server, Reliable)
	void ServerPurchaseBranch(ACommandBuilding* Building);
	UFUNCTION(Client, Reliable)
	void ClientBranchFeedback(const FString& Message, bool bAccepted);
};
