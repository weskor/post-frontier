#pragma once

#include "CoreMinimal.h"
#include "Rules/GoalPath.h"
#include "ForceGoals.generated.h"

UENUM(BlueprintType)
enum class EForceGoal : uint8
{
	Hold,
	Expand,
	Assault,
	FallBack
};

// Authority-only cache, bounded by the stable region graph. Never allocates while ticking.
struct FForceGoalDriver
{
	uint64 Graph[ForceGoals::MaxRegions] = {};
	int32 Controllers[ForceGoals::MaxRegions] = {};
	int32 Path[ForceGoals::MaxRegions] = {};
	int32 RegionCount = 0;
	int32 PathLength = 0;
	int32 PathCursor = 0;
	int32 Waypoint = INDEX_NONE;
	int32 LastHeld = INDEX_NONE;
	int32 AppliedWaypoint = INDEX_NONE;
	uint8 AppliedOrder = 255;
	bool bEnabled = true;
	bool bInitialized = false;
	bool bPathDirty = true;
	bool bRefilling = false;
};
