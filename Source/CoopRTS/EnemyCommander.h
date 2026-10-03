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
struct FJevTurn;
struct FJevForceStep;

// The ticket the executor holds for one force between evaluations.
struct FJevCommittedForce
{
	TWeakObjectPtr<AArmyGroup> Force;
	JevPlanner::FPlan Plan;
	int32 TicketNumber = 0;
	bool bRecovering = false;
	bool bCommandsRejected = false;
};

// Executor for JEV: EvaluatePlan summarises the match for the pure planner
// (Rules/JevPlanner), turns its choices into commands and publishes them.
// The steps live in EnemyCommanderWorld/Economy/Execute/Publish.cpp.
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
	bool BeginTurn(FJevTurn& Turn);
	void ExecuteForces(FJevTurn& Turn);
	void ExecuteForce(FJevTurn& Turn, AArmyGroup* Force);
	void Commit(FJevTurn& Turn, FJevForceStep& Step);
	void Publish(FJevTurn& Turn, const FJevForceStep& Step);
	TArray<FJevCommittedForce, TInlineAllocator<8>> CommittedForces;
	FJevMemoTemplates MemoTemplates;
	bool bMemoLoadAttempted = false;
	bool bMemosLoaded = false;
	int32 NextTicketNumber = 1;
	float EvaluateElapsed = 0.f;
};
