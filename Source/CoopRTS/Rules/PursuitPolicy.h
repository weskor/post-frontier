#pragma once

#include "CoreMinimal.h"
#include "Rules/CombatRangePolicy.h"

struct FPursuitDecision
{
	// The target is within weapon range: it may fire.
	bool bInRange = false;
	// The unit should hold still: in range, and against a structure either idle or already inside the stop band.
	bool bStop = false;
	bool bIssueMove = false;
	FVector Goal = FVector::ZeroVector;
};

// Approach rule shared by ordinary pursuit and the hold response. A unit pursues once the target is
// out of range and stands at the standoff. Distance runs centre to centre against a unit and to the
// edge against a structure. Against a structure (a box that stands still) the force's members spread
// along the edge and a goal is kept while its standoff holds. Against a building whose footprint cuts
// the navigation mesh (FRangeTarget::bBlocksMovement) a walking unit also fires from the edge of its
// range but keeps walking to the stop band, and stands at 0.9 x range. Against a unit or an HQ or
// Failover Node, which can be walked into, the approach is the one the duel matrix was balanced on:
// stop at the range edge and stand at 0.82 x range.
namespace PursuitPolicy
{
constexpr float IdleRetrySeconds = .5f;
// Footprint buildings: walking units stop inside this fraction of the weapon range; an idle unit in range stays put.
constexpr float StopBandFraction = .9f;
// Everything else: the fraction of the weapon range pursuit stands at.
constexpr float UnitStandoffFraction = .82f;
// Movement acceptance of an issued pursuit, reserved inside the weapon range.
constexpr float ArrivalTolerance = 35.f;
// A pursuit goal is kept until a new one is this far from it (units) or its standoff this far from a new one's (structures).
constexpr float GoalSlack = 130.f;
// Alternative approach standoffs tried, nearest the ideal first, when a goal's path is rejected.
constexpr int32 MaxApproachGoals = 4;
// Members of a force stand this far apart along a structure's edge: a capsule and a gap.
constexpr float SlotSpacing = 90.f;
// Sideways shifts tried along the edge to find a free place, nearest first, up to this many steps
// either way. Steps are fine so that the shorter arcs around a corner still reach a free place.
constexpr float SlotShiftStep = 30.f;
constexpr int32 MaxSlotShifts = 20;

// What a moving unit reserves inside its range: the arrival tolerance, and against targets that do not
// block movement also the capsule, since a footprint building's distance already counts it.
inline float ArrivalAllowance(const CombatRangePolicy::FRangeTarget& Target, float CapsuleRadius)
{
	return Target.bBlocksMovement ? ArrivalTolerance : CapsuleRadius + ArrivalTolerance;
}

// Distance (to the edge for a structure, to the centre for a unit) at which the unit prefers to
// stand: inside the stop band and short of the weapon range by the arrival allowance.
inline float Standoff(const CombatRangePolicy::FRangeTarget& Target, float Range, float Allowance)
{
	const float Fraction = Target.bBlocksMovement ? StopBandFraction : UnitStandoffFraction;
	return FMath::Max(0.f, FMath::Min(Range * Fraction, Range - Allowance));
}

// The Index-th approach goal (0 is the ideal), clipped to the order's leash. Later goals stand
// closer to the target, down to its edge, so that a target hidden behind a navigation cutout can
// still be approached. Occupied holds where teammates stand or are headed: against a structure the
// goal slides along its edge to the first place SlotSpacing clear of all of them, because members
// that aim at one point leave those behind the first walking against them for good.
inline FVector ApproachGoal(const FVector& Unit, const CombatRangePolicy::FRangeTarget& Target, float Range, float Allowance,
	const FVector& Anchor, float Radius, float GoalZ, int32 Index, TConstArrayView<FVector2D> Occupied = {})
{
	const double Ideal = Standoff(Target, Range, Allowance);
	const double Standoffs[MaxApproachGoals] = { Ideal, Ideal * .5, Ideal * .25, 0. };
	const double Chosen = Standoffs[FMath::Clamp(Index, 0, MaxApproachGoals - 1)];
	const FVector2D From(Unit);
	const FVector2D Preferred = CombatRangePolicy::PointAtEdgeDistance(From, Target, Chosen);
	const FVector2D Outward = (Preferred - CombatRangePolicy::NearestPoint(From, Target)).GetSafeNormal();
	const FVector2D Tangent(-Outward.Y, Outward.X);
	const auto Free = [&](const FVector2D& Candidate) {
		return !Occupied.ContainsByPredicate([&](const FVector2D& Other) { return FVector2D::Distance(Other, Candidate) < SlotSpacing; });
	};
	FVector2D Point = Preferred;
	// Slide outward a step at a time, on the side nearer the order's anchor first, so the force
	// as a whole stays centred on where it was sent rather than drifting along the edge.
	for (int32 Step = 1; Step <= MaxSlotShifts && CombatRangePolicy::IsStructure(Target) && !Free(Point); ++Step)
	{
		const FVector2D Sides[2] = { CombatRangePolicy::PointAtEdgeDistance(Preferred + Tangent * (Step * SlotShiftStep), Target, Chosen),
			CombatRangePolicy::PointAtEdgeDistance(Preferred - Tangent * (Step * SlotShiftStep), Target, Chosen) };
		const int32 First = FVector2D::DistSquared(Sides[1], FVector2D(Anchor)) < FVector2D::DistSquared(Sides[0], FVector2D(Anchor)) ? 1 : 0;
		if (Free(Sides[First]))
			Point = Sides[First];
		else if (Free(Sides[1 - First]))
			Point = Sides[1 - First];
	}
	FVector Goal(Point.X, Point.Y, GoalZ);
	const FVector Offset = Goal - Anchor;
	if (Offset.SizeSquared2D() > FMath::Square(Radius))
	{
		Goal = Anchor + Offset.GetSafeNormal2D() * Radius;
		Goal.Z = GoalZ;
	}
	return Goal;
}

// LastIssuedGoal is an accepted move endpoint, never the unit's firing position.
// bPursuing means the last pursuit request is still active, not a completed path.
inline FPursuitDecision Evaluate(const FVector& Unit, const CombatRangePolicy::FRangeTarget& Target, float Range, float Allowance,
	const FVector& Anchor, float Radius, float GoalZ, bool bPursuing, bool bTargetChanged,
	const FVector& LastIssuedGoal, bool bRetryReady)
{
	FPursuitDecision Decision;
	const bool bStructure = CombatRangePolicy::IsStructure(Target);
	const double Distance = CombatRangePolicy::EdgeDistance(FVector2D(Unit), Target);
	Decision.bInRange = Distance <= Range;
	if (Decision.bInRange)
	{
		Decision.bStop = !Target.bBlocksMovement || !bPursuing || Distance <= Range * StopBandFraction;
		return Decision;
	}
	Decision.Goal = ApproachGoal(Unit, Target, Range, Allowance, Anchor, Radius, GoalZ, 0);
	// A moving target: its goal is kept until a new one is more than the slack away. A structure
	// stands still: the accepted goal stays good while its own standoff stays near a new goal's, so
	// the unit turning around the structure does not call for a new path.
	const bool bGoalChanged = bStructure
		? FMath::Abs(CombatRangePolicy::EdgeDistance(FVector2D(LastIssuedGoal), Target) - CombatRangePolicy::EdgeDistance(FVector2D(Decision.Goal), Target)) > GoalSlack + .001
		: FVector::DistSquared2D(Decision.Goal, LastIssuedGoal) > FMath::Square(GoalSlack);
	Decision.bIssueMove = bTargetChanged || (bPursuing ? bGoalChanged : bRetryReady);
	return Decision;
}
}
