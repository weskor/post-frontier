#include "ArmyGroup.h"

#include "AIController.h"
#include "ArmyGroupInternal.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

using namespace ArmyGroupInternal;

void AArmyGroup::AbandonReinforcementMove(AArmyUnit& Unit, AAIController& AI)
{
	AI.StopMovement();
	Unit.GetCharacterMovement()->StopMovementImmediately();
	Unit.bHasReinforcementPath = false;
}

// False when the new route was rejected; the unit retries on a later tick.
bool AArmyGroup::RetargetReinforcement(AArmyUnit& Unit, AAIController& AI, UNavigationSystemV1& Navigation, const FVector& Goal)
{
	FPreparedMove Move;
	Move.Controller = &AI;
	if (!PrepareMove(Navigation, Unit.GetNavAgentPropertiesRef(), &AI, *AI.GetPathFollowingComponent(),
			Unit.GetNavAgentLocation(), Goal, Move, 75.f))
	{
		AbandonReinforcementMove(Unit, AI);
		return false;
	}
	Unit.ReinforcementGoal = Move.Goal;
	Unit.ReinforcementRendezvous = Goal;
	Unit.bHasReinforcementPath = true;
	if (FVector::DistSquared2D(Unit.GetNavAgentLocation(), Move.Goal) > FMath::Square(35.f)
		&& !StartPreparedMove(Move))
	{
		AI.StopMovement();
		Unit.bHasReinforcementPath = false;
		return false;
	}
	return true;
}

namespace
{
// Predictive avoidance can stop before a vacant slot because it predicts
// continuing through the goal into members beyond it. Use a precise final
// approach; the validated corridor and physical capsule collisions remain.
void SetFinalApproachAvoidance(const AArmyUnit& Unit, UCrowdFollowingComponent& Crowd, double DistanceToGoalSquared)
{
	const float PrecisionRadius = 2.f * Unit.GetSimpleCollisionRadius() + 35.f;
	Crowd.SetCrowdObstacleAvoidance(DistanceToGoalSquared > FMath::Square(PrecisionRadius), true);
}
}

bool AArmyGroup::ReinforcementTarget(const AArmyUnit& Unit, FVector& Goal) const
{
	if (IsHoldingRegion() && HoldPostIndex != INDEX_NONE && !bHoldResponding)
	{
		// Holding slots are individually clipped/projected, not a rigid formation.
		Goal = HoldPostLocation + FormationOffset(Unit.CompositionSlot);
		return ClipHoldingDestination(Goal);
	}
	// A depleted formation's member center is biased toward its occupied slots.
	// Recover its moving anchor so an empty slot does not target an existing member.
	FVector Anchor = FVector::ZeroVector;
	int32 Joined = 0;
	for (const AArmyUnit* Member : Units)
	{
		if (!IsValid(Member) || !Member->IsAlive() || Member->bReinforcing)
			continue;
		Anchor += Member->GetActorLocation() - FormationOffset(Member->CompositionSlot);
		++Joined;
	}
	if (Joined > 0)
		Anchor /= Joined;
	else
		Anchor = AppliedWaypoint != INDEX_NONE ? Destination : GetActorLocation();
	Goal = Anchor + FormationOffset(Unit.CompositionSlot);
	return ClipHoldingDestination(Goal);
}

bool AArmyGroup::JoinFormation(AArmyUnit& Unit, AAIController& AI, UNavigationSystemV1& Navigation)
{
	// Arrival is physical and follows a complete accepted route. Adopt current
	// intent, not the front/order that happened to exist when this recruit spawned.
	if (Order == EArmyOrder::Hold)
	{
		AI.StopMovement();
		return true;
	}
	FPreparedMove Formation;
	Formation.Controller = &AI;
	FVector FormationGoal = Destination + FormationOffset(Unit.CompositionSlot);
	return ClipHoldingDestination(FormationGoal)
		&& PrepareMove(Navigation, Unit.GetNavAgentPropertiesRef(), &AI, *AI.GetPathFollowingComponent(),
			Unit.GetNavAgentLocation(), FormationGoal, Formation)
		&& StartPreparedMove(Formation);
}

void AArmyGroup::UpdateReinforcement(AArmyUnit& Unit, UNavigationSystemV1& Navigation)
{
	AAIController* AI = GetReadyController(&Unit);
	if (!AI)
		return;
	Unit.Target = nullptr;
	Unit.bPursuing = false;
	FVector Goal;
	if (!ReinforcementTarget(Unit, Goal))
	{
		AbandonReinforcementMove(Unit, *AI);
		return;
	}
	const bool bRetarget = !Unit.bHasReinforcementPath
		|| FVector::DistSquared2D(Goal, Unit.ReinforcementRendezvous) > FMath::Square(55.f)
		|| AI->GetMoveStatus() == EPathFollowingStatus::Idle;
	if (bRetarget && !RetargetReinforcement(Unit, *AI, Navigation, Goal))
		return;
	const double DistanceToGoalSquared = FVector::DistSquared2D(Unit.GetNavAgentLocation(), Unit.ReinforcementGoal);
	UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(AI->GetPathFollowingComponent());
	if (Crowd)
		SetFinalApproachAvoidance(Unit, *Crowd, DistanceToGoalSquared);
	if (!Unit.bHasReinforcementPath
		|| DistanceToGoalSquared > FMath::Square(35.f)
		|| FVector::DistSquared2D(Goal, Unit.ReinforcementRendezvous) > FMath::Square(55.f)
		|| FMath::Abs(Unit.GetNavAgentLocation().Z - Unit.ReinforcementGoal.Z) > 110.f)
		return;
	if (Crowd)
		Crowd->SetCrowdObstacleAvoidance(true, true);
	if (!JoinFormation(Unit, *AI, Navigation))
		return;
	Unit.bReinforcing = false;
	Unit.bHasReinforcementPath = false;
	Unit.ForceNetUpdate();
	ForceNetUpdate();
}

void AArmyGroup::UpdateReinforcements()
{
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Navigation)
		return;
	for (AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive() && Unit->bReinforcing)
			UpdateReinforcement(*Unit, *Navigation);
}
