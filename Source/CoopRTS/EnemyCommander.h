#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyCommander.generated.h"

class AArmyGroup;
class ACapturePoint;
class AArmyUnit;

UCLASS()
class COOPRTS_API AEnemyCommander : public AActor
{
	GENERATED_BODY()
public:
	AEnemyCommander();
	virtual void Tick(float DeltaSeconds) override;
	void EvaluatePlan();
	UPROPERTY()
	TObjectPtr<AArmyGroup> Army;
private:
	enum class EGoal : uint8 { None, Capture, Contest, DefendHQ, RetreatReinforce, AttackHQ };
	EGoal Goal = EGoal::None;
	TWeakObjectPtr<ACapturePoint> GoalSite;
	float CommitUntil = 0.f;
	float EvaluateElapsed = 0.f;
	bool Choose(EGoal Next, ACapturePoint* Site, AArmyUnit* Threat, const FString& Reason, bool bEmergency);
};
