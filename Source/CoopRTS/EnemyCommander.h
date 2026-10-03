#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyCommander.generated.h"

class ACommandBuilding;
class AArmyGroup;
class ACommandGameState;
class ACommandPlayerState;

UCLASS()
class COOPRTS_API AEnemyCommander : public AActor
{
	GENERATED_BODY()
public:
	AEnemyCommander();
	virtual void Tick(float DeltaSeconds) override;
	void EvaluatePlan();
	// Team 0 is created only by an explicit autopilot fixture; normal play creates team 5.
	UPROPERTY()
	int32 TeamIndex = 5;
	UPROPERTY()
	TObjectPtr<ACommandPlayerState> Commander;
private:
	ACommandBuilding* BuildNear(ACommandGameState* State, int32 BuildingIndex, const FVector& Center);
	// Retreat execution can finish before Field Repairs reaches the planner's 80% health release.
	TArray<TWeakObjectPtr<AArmyGroup>, TInlineAllocator<8>> RecoveringForces;
	float EvaluateElapsed = 0.f;
};
