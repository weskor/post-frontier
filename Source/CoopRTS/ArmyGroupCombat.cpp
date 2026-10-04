#include "ArmyGroup.h"

#include "AIController.h"
#include "ArmyGroupInternal.h"
#include "ArmyUnit.h"
#include "CombatTarget.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Headquarters.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Rules/PursuitPolicy.h"
#include "Rules/TargetingPolicy.h"

using namespace ArmyGroupInternal;

// Everything an opposing team fields, gathered once per combat tick.
struct FArmyCombatScan
{
	TArray<AArmyUnit*, TInlineAllocator<32>> Enemies;
	AHeadquarters* HostileHQ = nullptr;
	const TArray<TObjectPtr<ACommandBuilding>>* HostileBuildings = nullptr;
};

namespace
{
FArmyCombatScan ScanHostiles(UWorld& World, int32 Team)
{
	FArmyCombatScan Scan;
	for (TActorIterator<AArmyGroup> It(&World); It; ++It)
	{
		if (It->GetTeamIndex() == Team)
			continue;
		for (AArmyUnit* Enemy : It->GetUnits())
			if (IsValid(Enemy) && Enemy->IsAlive())
				Scan.Enemies.Add(Enemy);
	}
	if (const ACommandGameState* State = World.GetGameState<ACommandGameState>())
	{
		Scan.HostileHQ = Team == 5 ? State->FriendlyHeadquarters.Get() : State->EnemyHeadquarters.Get();
		Scan.HostileBuildings = &State->Buildings;
	}
	return Scan;
}

// Pursuit may only follow routes that stay inside the anchor's pursuit radius.
void IssueBoundedPursuit(UWorld& World, AArmyUnit& Unit, AAIController& AI, const FVector& Goal, const FVector& Anchor)
{
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	FPreparedMove Pursuit;
	Pursuit.Controller = &AI;
	if (!Navigation || !PrepareMove(*Navigation, Unit.GetNavAgentPropertiesRef(), &AI, *AI.GetPathFollowingComponent(), Unit.GetNavAgentLocation(), Goal, Pursuit, 75.f))
		return;
	for (const FNavPathPoint& Point : Pursuit.Path->GetPathPoints())
		if (FVector::DistSquared2D(Point.Location, Anchor) > FMath::Square(AArmyGroup::PursuitRadius))
			return;
	if (StartPreparedMove(Pursuit))
	{
		Unit.bPursuing = true;
		Unit.PursuitGoal = Pursuit.Goal;
	}
}
}

bool AArmyGroup::IsEngagementPermitted(const AArmyUnit& Unit, AActor* Enemy) const
{
	if (!CombatTarget::IsAliveHostile(Enemy, TeamIndex))
		return false;
	const FVector UnitLocation = Unit.GetActorLocation();
	const FVector EnemyLocation = Enemy->GetActorLocation();
	ArmyGroupPolicy::FEngagement Engagement;
	Engagement.bAttackOrder = Order == EArmyOrder::Attack;
	Engagement.bMarching = Status == EForceStatus::Marching;
	Engagement.UnitToEnemy = static_cast<float>(FVector::DistSquared2D(UnitLocation, EnemyLocation));
	Engagement.EnemyToAnchor = FVector::DistSquared2D(EnemyLocation, Destination);
	Engagement.UnitToAnchor = FVector::DistSquared2D(UnitLocation, Destination);
	Engagement.WeaponRange = Unit.WeaponRange();
	Engagement.PursuitRadius = PursuitRadius;
	return ArmyGroupPolicy::EngagementPermitted(Engagement);
}

AActor* AArmyGroup::ChooseTarget(AArmyUnit& Unit, const FArmyCombatScan& Scan) const
{
	if (IsEngagementPermitted(Unit, AttackTarget.Get()))
		return AttackTarget.Get();
	if (IsEngagementPermitted(Unit, Unit.Target.Get()))
		return Unit.Target.Get();
	AActor* Chosen = nullptr;
	FTargetSelection Selection;
	int32 CandidateIndex = 0;
	auto Consider = [&](AActor* Candidate) {
		if (!IsEngagementPermitted(Unit, Candidate))
			return;
		const int32 IndexInSelection = CandidateIndex++;
		Selection.Consider(Unit.GetDamageType(), IndexInSelection, CombatTarget::ArmorClass(Candidate),
			FVector::DistSquared2D(Unit.GetActorLocation(), Candidate->GetActorLocation()));
		if (Selection.Index == IndexInSelection)
			Chosen = Candidate;
	};
	for (AArmyUnit* Enemy : Scan.Enemies)
		Consider(Enemy);
	Consider(Scan.HostileHQ);
	if (Scan.HostileBuildings)
		for (ACommandBuilding* Building : *Scan.HostileBuildings)
			Consider(Building);
	return Chosen;
}

void AArmyGroup::UpdateAttackPursuit(AArmyUnit& Unit, AAIController& AI, AActor& Chosen, bool bTargetChanged)
{
	const int32 Slot = Unit.CompositionSlot;
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bActivePursuit = Unit.bPursuing && AI.GetMoveStatus() != EPathFollowingStatus::Idle;
	const FPursuitDecision Decision = PursuitPolicy::Evaluate(Unit.GetActorLocation(),
		Chosen.GetActorLocation(), Unit.WeaponRange(), Unit.GetSimpleCollisionRadius() + 35.f,
		Destination, PursuitRadius, Destination.Z, bActivePursuit, bTargetChanged,
		Unit.PursuitGoal, Now >= PursuitRetryAt(Slot));
	if (bTargetChanged)
		Unit.bPursuing = false;
	if (Decision.bInRange)
	{
		Unit.bPursuing = true; // Engaged: front maintenance and regrouping depend on this flag.
		PursuitRetryAt(Slot) = 0.f;
		if (AI.GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			AI.StopMovement();
			Unit.GetCharacterMovement()->StopMovementImmediately();
		}
	}
	else if (Decision.bIssueMove)
	{
		PursuitRetryAt(Slot) = Now + PursuitPolicy::IdleRetrySeconds;
		IssueBoundedPursuit(*GetWorld(), Unit, AI, Decision.Goal, Destination);
	}
}

void AArmyGroup::UpdateUnitCombat(AArmyUnit& Unit, const FArmyCombatScan& Scan)
{
	AActor* Chosen = ChooseTarget(Unit, Scan);
	const bool bTargetChanged = Unit.Target != Chosen;
	if (bTargetChanged)
	{
		Unit.Target = Chosen;
		Unit.ForceNetUpdate();
	}
	AAIController* AI = Cast<AAIController>(Unit.GetController());
	if (!Chosen)
	{
		if (Unit.bPursuing && AI)
		{
			Unit.bPursuing = false;
			AI->MoveToLocation(Destination + FormationOffset(Unit.CompositionSlot), 35.f, false, true, false, false);
		}
		return;
	}
	if (Order == EArmyOrder::Attack && AI
		&& (Status == EForceStatus::Holding || FVector::DistSquared2D(Unit.GetActorLocation(), Destination) <= FMath::Square(PursuitRadius)))
		UpdateAttackPursuit(Unit, *AI, *Chosen, bTargetChanged);
	Unit.FireAt(Chosen);
}

void AArmyGroup::UpdateCombat()
{
	if (Status == EForceStatus::Retreating)
		return;
	if (IsHoldingRegion())
	{
		UpdateHoldCombat();
		return;
	}
	if (Units.IsEmpty())
	{
		if (AttackTarget)
		{
			AttackTarget = nullptr;
			ForceNetUpdate();
		}
		return;
	}
	const FArmyCombatScan Scan = ScanHostiles(*GetWorld(), TeamIndex);
	if (AttackTarget && (!CombatTarget::IsAliveHostile(AttackTarget.Get(), TeamIndex) || (Status != EForceStatus::Marching && FVector::DistSquared2D(AttackTarget->GetActorLocation(), Destination) > FMath::Square(PursuitRadius))))
	{
		AttackTarget = nullptr;
		ForceNetUpdate();
	}
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		AArmyUnit* Unit = Units[Index];
		if (IsValid(Unit) && Unit->IsAlive())
			UpdateUnitCombat(*Unit, Scan);
	}
}
