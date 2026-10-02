#pragma once

#include "CoreMinimal.h"

struct FPursuitDecision
{
	bool bInRange = false;
	bool bIssueMove = false;
	FVector Goal = FVector::ZeroVector;
};

namespace PursuitPolicy
{
// LastIssuedGoal is an accepted move endpoint, never the unit's firing position.
// bPursuing means the last pursuit request is still active, not a completed path.
inline FPursuitDecision Evaluate(const FVector& Unit, const FVector& Target, float Range,
	const FVector& Anchor, float Radius, float GoalZ, bool bPursuing, bool bTargetChanged,
	const FVector& LastIssuedGoal)
{
	FPursuitDecision Decision;
	Decision.bInRange = FVector::DistSquared2D(Unit, Target) <= FMath::Square(Range);
	if (Decision.bInRange)
		return Decision;
	// Detour can stop a capsule radius before the endpoint, independently of
	// RequestMove's 35 cm tolerance. Reserve both allowances for short weapons.
	const float StandOff = FMath::Max(0.f, FMath::Min(Range * .82f, Range - 70.f));
	Decision.Goal = Target + (Unit - Target).GetSafeNormal2D() * StandOff;
	const FVector Offset = Decision.Goal - Anchor;
	if (Offset.SizeSquared2D() > FMath::Square(Radius))
		Decision.Goal = Anchor + Offset.GetSafeNormal2D() * Radius;
	Decision.Goal.Z = GoalZ;
	Decision.bIssueMove = !bPursuing || bTargetChanged
		|| FVector::DistSquared2D(Decision.Goal, LastIssuedGoal) > FMath::Square(130.f);
	return Decision;
}
}
