#include "ArmyGroup.h"

#include "AIController.h"
#include "ArmyGroupInternal.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MapRegion.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Rules/PursuitPolicy.h"
#include "Rules/TargetingPolicy.h"

using namespace ArmyGroupInternal;

namespace
{
bool IsPermittedHoldEnemy(const AArmyUnit& Target, const ACommandGameState& State,
	const AMapRegion& Region, int32 Team, float MaximumWeaponRange)
{
	if (!Target.IsAlive() || Target.GetTeamIndex() == Team)
		return false;
	const FVector2D Position(Target.GetActorLocation());
	if (HoldPolicy::Contains(Region.Polygon, Position))
		return true;
	const double Distance = FVector2D::Distance(Position, HoldPolicy::ClosestBoundary(Region.Polygon, Position));
	return HoldPolicy::WithinLeash(false, true, Distance, MaximumWeaponRange)
		&& State.IsDamagingRegion(Target, Region.RegionIndex, Team);
}

bool IsPreparedHoldMovePermitted(const AArmyUnit& Unit, const AMapRegion& Region, const FPreparedMove& Move)
{
	if (!HoldPolicy::Contains(Region.Polygon, FVector2D(Move.Goal)))
		return false;
	// An ordinary route originating outside may complete. Requests originating
	// inside must keep every segment inside, including across concave borders.
	if (!HoldPolicy::Contains(Region.Polygon, FVector2D(Unit.GetActorLocation())))
		return true;
	FVector Previous = Unit.GetActorLocation();
	for (const FNavPathPoint& Point : Move.Path->GetPathPoints())
	{
		if (!HoldPolicy::SegmentInside(Region.Polygon, FVector2D(Previous), FVector2D(Point.Location)))
			return false;
		Previous = Point.Location;
	}
	return true;
}

AArmyUnit* SelectHoldCombatTarget(AArmyUnit& Unit, TConstArrayView<AArmyUnit*> Enemies)
{
	const FVector Position = Unit.GetActorLocation();
	const float RangeSquared = FMath::Square(Unit.WeaponRange());
	auto InRange = [&](const AArmyUnit* Enemy) {
		return FVector::DistSquared2D(Position, Enemy->GetActorLocation()) <= RangeSquared;
	};
	AArmyUnit* Target = Cast<AArmyUnit>(Unit.Target.Get());
	if (!Enemies.Contains(Target) || !InRange(Target))
	{
		Target = nullptr;
		FTargetSelection Selection;
		for (int32 Index = 0; Index < Enemies.Num(); ++Index)
		{
			AArmyUnit* Enemy = Enemies[Index];
			if (!InRange(Enemy))
				continue;
			Selection.Consider(Unit.GetDamageType(), Index, Enemy->GetArmorClass(),
				FVector::DistSquared2D(Position, Enemy->GetActorLocation()));
			if (Selection.Index == Index)
				Target = Enemy;
		}
	}
	if (Unit.Target != Target)
	{
		Unit.Target = Target;
		Unit.ForceNetUpdate();
	}
	return Target;
}

const AMapRegion* FindRegion(const ACommandGameState& State, int32 RegionIndex)
{
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->RegionIndex == RegionIndex)
			return Region;
	return nullptr;
}
}

bool AArmyGroup::IsHoldingRegion() const
{
	return Verb == EForceVerb::MoveHold && Status == EForceStatus::Holding && HoldRegionIndex != INDEX_NONE;
}

void AArmyGroup::ResetHoldState()
{
	HoldRegionIndex = HoldPostIndex = HoldPostSlot = INDEX_NONE;
	bHoldResponding = false;
	HoldThreat = nullptr;
	HoldThreatenedAsset = nullptr;
	HoldThreatKind = EHoldThreatKind::Intrusion;
	HoldClock = {};
}

bool AArmyGroup::ClipHoldingDestination(FVector& Goal) const
{
	if (!IsHoldingRegion())
		return true;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const AMapRegion* Region = State ? FindRegion(*State, HoldRegionIndex) : nullptr;
	if (!Region)
		return false;
	const FVector2D Inside = HoldPolicy::ClampInside(Region->Polygon, FVector2D(Goal));
	Goal.X = Inside.X;
	Goal.Y = Inside.Y;
	return true;
}

bool AArmyGroup::IsHoldTargetPermitted(const AArmyUnit& Target, const AMapRegion& Region, float MaximumWeaponRange) const
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	return State && IsHoldingRegion() && Region.RegionIndex == HoldRegionIndex
		&& IsPermittedHoldEnemy(Target, *State, Region, TeamIndex, MaximumWeaponRange);
}

void AArmyGroup::UpdateHoldMovement(AArmyUnit& Unit, const AMapRegion& Region,
	UNavigationSystemV1* Navigation, const FVector& RequestedGoal, float Now)
{
	AAIController* AI = GetReadyController(&Unit);
	if (!AI)
		return;
	const FVector2D Inside = HoldPolicy::ClampInside(Region.Polygon, FVector2D(RequestedGoal));
	const FVector Goal(Inside.X, Inside.Y, RequestedGoal.Z);
	const bool bActiveMove = Unit.bPursuing && AI->GetMoveStatus() != EPathFollowingStatus::Idle;
	if (FVector::DistSquared2D(Unit.GetActorLocation(), Goal) <= FMath::Square(35.f))
	{
		if (AI->GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			AI->StopMovement();
			Unit.GetCharacterMovement()->StopMovementImmediately();
		}
		return;
	}
	if (bActiveMove && FVector::DistSquared2D(Goal, Unit.PursuitGoal) <= FMath::Square(130.f))
		return;
	float& RetryAt = PursuitRetryAt(Unit.CompositionSlot);
	if (Now < RetryAt)
		return;
	// Every attempt consumes the retry interval, including projection, path and
	// border rejections. Only an accepted request updates the issued endpoint.
	RetryAt = Now + PursuitPolicy::IdleRetrySeconds;
	auto RejectMove = [&]() {
		AI->StopMovement();
		Unit.GetCharacterMovement()->StopMovementImmediately();
		Unit.bPursuing = false;
	};
	FPreparedMove Move;
	Move.Controller = AI;
	if (!Navigation || !PrepareMove(*Navigation, Unit.GetNavAgentPropertiesRef(), AI, *AI->GetPathFollowingComponent(), Unit.GetNavAgentLocation(), Goal, Move, 75.f)
		|| !IsPreparedHoldMovePermitted(Unit, Region, Move))
	{
		RejectMove();
		return;
	}
	if (!StartPreparedMove(Move))
	{
		RejectMove();
		return;
	}
	Unit.bPursuing = true;
	Unit.PursuitGoal = Move.Goal;
}

void AArmyGroup::UpdateHoldResponse(AArmyUnit& Unit, const AMapRegion& Region,
	UNavigationSystemV1* Navigation, float Now)
{
	AAIController* AI = Cast<AAIController>(Unit.GetController());
	if (!HoldThreat)
	{
		// Keep the last response position through the commitment/quiet grace.
		if (AI)
			AI->StopMovement();
		Unit.GetCharacterMovement()->StopMovementImmediately();
		Unit.bPursuing = Unit.Target != nullptr;
		return;
	}
	if (!AI)
		return;
	const int32 Slot = Unit.CompositionSlot;
	const FVector Position = Unit.GetActorLocation();
	const FVector ThreatPosition = HoldThreat->GetActorLocation();
	const float Range = Unit.WeaponRange();
	// The region supplies the leash; leave the policy's circular leash inactive
	// and clip its capsule-aware standoff to the polygon in UpdateHoldMovement.
	const FPursuitDecision Decision = PursuitPolicy::Evaluate(Position, ThreatPosition, Range,
		Unit.GetSimpleCollisionRadius() + 35.f, Position,
		FVector::Dist2D(Position, ThreatPosition) + Range, HoldPostLocation.Z,
		Unit.bPursuing && AI->GetMoveStatus() != EPathFollowingStatus::Idle,
		false, Unit.PursuitGoal, Now >= PursuitRetryAt(Slot));
	if (Decision.bInRange)
	{
		Unit.bPursuing = true; // Engaged, as in ordinary pursuit.
		PursuitRetryAt(Slot) = 0.f;
		if (AI->GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			AI->StopMovement();
			Unit.GetCharacterMovement()->StopMovementImmediately();
		}
	}
	else if (Decision.bIssueMove)
		UpdateHoldMovement(Unit, Region, Navigation, Decision.Goal, Now);
}

void AArmyGroup::UpdateHoldCombat()
{
	if (HoldPostIndex == INDEX_NONE)
		return;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const AMapRegion* Region = State ? FindRegion(*State, HoldRegionIndex) : nullptr;
	if (!Region)
		return;
	float MaximumWeaponRange = 0.f;
	for (const AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive())
			MaximumWeaponRange = FMath::Max(MaximumWeaponRange, Unit->WeaponRange());
	TArray<AArmyUnit*, TInlineAllocator<32>> Enemies;
	for (TActorIterator<AArmyUnit> It(GetWorld()); It; ++It)
		if (IsPermittedHoldEnemy(**It, *State, *Region, TeamIndex, MaximumWeaponRange))
			Enemies.Add(*It);
	if (HoldThreat && !Enemies.Contains(HoldThreat.Get()))
		HoldThreat = nullptr;
	if (!bHoldResponding)
		Destination = HoldPostLocation;
	else if (HoldThreat)
		Destination = HoldThreat->GetActorLocation();
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const float Now = GetWorld()->GetTimeSeconds();
	for (AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive())
			continue;
		AArmyUnit* Target = SelectHoldCombatTarget(*Unit, Enemies);
		if (!bHoldResponding)
			UpdateHoldMovement(*Unit, *Region, Navigation, HoldPostLocation + FormationOffset(Unit->CompositionSlot), Now);
		else
			UpdateHoldResponse(*Unit, *Region, Navigation, Now);
		if (Target)
			Unit->FireAt(Target);
	}
}
