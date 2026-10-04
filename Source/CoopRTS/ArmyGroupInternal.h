#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavigationTypes.h"
#include "CollisionShape.h"

class AAIController;
class AArmyUnit;
class UNavigationSystemV1;
class UPathFollowingComponent;

// Movement helpers shared by the AArmyGroup implementation files. Not part of the public API.
namespace ArmyGroupInternal
{
constexpr int32 MaxUnitCount = 6;

struct FPreparedMove
{
	AAIController* Controller = nullptr;
	FVector Goal = FVector::ZeroVector;
	FNavPathSharedPtr Path;
};

AAIController* GetReadyController(AArmyUnit* Unit);
bool PrepareMove(UNavigationSystemV1& Navigation, const FNavAgentProperties& Agent, UObject* Querier,
	UPathFollowingComponent& Following, const FVector& Start, const FVector& Target,
	FPreparedMove& Prepared, float ProjectionRadius = 35.0f);
bool StartPreparedMove(const FPreparedMove& Move);
void DestroyUnit(AArmyUnit* Unit);
// Lowest free composition slot for a producer's force; INDEX_NONE when full or when a living member
// does not belong to this producer's unit and capacity.
int32 VacantReinforcementSlot(const TArray<TObjectPtr<AArmyUnit>>& Units, int32 UnitIndex, int32 Capacity);
// A standing unit's capsule and the height of its centre above the floor at spawn, both read from the
// unit class default so a capsule change moves recruit placement with it.
FCollisionShape UnitCapsule();
float SpawnLift();
// Centre height of the capsule probe that keeps a unit standing at a producer exit blocking it: the spawn lift
// plus 20 cm of clearance.
float ExitProbeLift();
}
