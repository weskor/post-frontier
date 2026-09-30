#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyCommander.generated.h"

class ACommandBuilding;
class ACommandGameState;
enum class EBuildingKind : uint8;

UCLASS()
class COOPRTS_API AEnemyCommander : public AActor
{
	GENERATED_BODY()
public:
	AEnemyCommander();
	virtual void Tick(float DeltaSeconds) override;
	void EvaluatePlan();
private:
	ACommandBuilding* BuildNear(ACommandGameState* State, EBuildingKind Kind, const FVector& Center);
	float CommitUntil = 0.f;
	FVector CommittedFront = FVector::ZeroVector;
	float EvaluateElapsed = 0.f;
};
