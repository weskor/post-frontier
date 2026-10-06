#pragma once

#include "CoreMinimal.h"

// Per-unit progress toward a goal, sampled on the force tick. A unit that stops making progress widens its
// "close enough" radius, gets one re-path, and is finally settled for that goal, so one wedged unit never blocks
// its force's arrival or its next order. Pure: no world, no actors.
namespace MovementProgressPolicy
{
// A window ends after this long; the unit must have moved MinProgress within it.
constexpr float SampleSeconds = 1.5f;
constexpr float MinProgress = 25.f;
// Consecutive idle seconds at which the unit is re-pathed once, and settled.
constexpr float RepathIdleSeconds = 3.f;
constexpr float SettleIdleSeconds = 6.f;
// The close-enough radius around the goal grows by this much per idle second, up to MaxRadius.
constexpr float RadiusGrowth = 75.f;
constexpr float MaxRadius = 300.f;
// A unit this close to its goal has arrived and is not tracked.
constexpr float AtGoalRadius = 60.f;
// A goal that moved further than this is a new goal.
constexpr float GoalChangeTolerance = 100.f;

struct FUnitProgress
{
	bool bTracking = false;
	bool bRepathed = false;
	bool bSettled = false;
	FVector Goal = FVector::ZeroVector;
	// Where and when the open window began.
	FVector WindowStart = FVector::ZeroVector;
	float WindowStartTime = 0.f;
	// Consecutive idle seconds; a window with progress clears it.
	float IdleSeconds = 0.f;
};

struct FSample
{
	float Now = 0.f;
	FVector Position = FVector::ZeroVector;
	FVector Goal = FVector::ZeroVector;
	// The unit is expected to be going somewhere: it has an order goal and is not engaged.
	bool bHasGoal = false;
};

enum class EAction : uint8
{
	None,
	// Issue the unit one fresh path to its goal.
	Repath,
	// The unit just became settled: stop it.
	Settle
};

// Advances one unit's state by a sample. Without a goal, or at the goal, the state is cleared; a new goal starts a new window.
EAction Update(FUnitProgress& Progress, const FSample& Sample);

// Radius around the goal inside which the unit counts as arrived, for its idle time.
float CloseEnoughRadius(float IdleSeconds);

// A settled unit, or an idle one inside its grown radius, no longer holds back arrival or waypoint advance.
bool IsExempt(const FUnitProgress& Progress, const FVector& Position);
}
