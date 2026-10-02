#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CommandMenuGameMode.generated.h"

// A real frontend world: no match actors, enemy planner or ticking economy.
UCLASS()
class ACommandMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ACommandMenuGameMode();
};
