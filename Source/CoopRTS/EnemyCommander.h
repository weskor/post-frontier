#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rules/JevPlanner.h"
#include "JevMemoTemplates.h"
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
	struct FCommittedForce
	{
		TWeakObjectPtr<AArmyGroup> Force;
		JevPlanner::FPlan Plan;
		int32 TicketNumber = 0;
		bool bRecovering = false;
	};
	TArray<FCommittedForce, TInlineAllocator<8>> CommittedForces;
	FJevMemoTemplates MemoTemplates;
	bool bMemoLoadAttempted = false;
	bool bMemosLoaded = false;
	int32 NextTicketNumber = 1;
	float EvaluateElapsed = 0.f;
};
