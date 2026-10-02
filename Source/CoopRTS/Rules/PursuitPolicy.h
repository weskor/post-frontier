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
constexpr float IdleRetrySeconds = .5f;

// LastIssuedGoal is an accepted move endpoint, never the unit's firing position.
// bPursuing means the last pursuit request is still active, not a completed path.
inline FPursuitDecision Evaluate(const FVector& Unit, const FVector& Target, float Range, float ArrivalAllowance,
	const FVector& Anchor, float Radius, float GoalZ, bool bPursuing, bool bTargetChanged,
	const FVector& LastIssuedGoal, bool bRetryReady)
{
	FPursuitDecision Decision;
	Decision.bInRange = FVector::DistSquared2D(Unit, Target) <= FMath::Square(Range);
	if (Decision.bInRange)
		return Decision;
	// Reserve both the moving capsule radius and the adapter's arrival tolerance.
	const float StandOff = FMath::Max(0.f, FMath::Min(Range * .82f, Range - ArrivalAllowance));
	Decision.Goal = Target + (Unit - Target).GetSafeNormal2D() * StandOff;
	const FVector Offset = Decision.Goal - Anchor;
	if (Offset.SizeSquared2D() > FMath::Square(Radius))
		Decision.Goal = Anchor + Offset.GetSafeNormal2D() * Radius;
	Decision.Goal.Z = GoalZ;
	const bool bGoalChanged = FVector::DistSquared2D(Decision.Goal, LastIssuedGoal) > FMath::Square(130.f);
	Decision.bIssueMove = bTargetChanged || (bPursuing ? bGoalChanged : bRetryReady);
	return Decision;
}
}
