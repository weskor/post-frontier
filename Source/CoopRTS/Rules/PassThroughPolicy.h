#pragma once

#include "CoreMinimal.h"

// A marching force passes through intermediate regions as one multi-region leg instead of stopping and
// re-ordering at each anchor, wherever the capture rules allow it. Bit N of every mask is region index N. Pure.
namespace PassThroughPolicy
{
// How many regions one fresh order may span (the last one is where the force stops or is ordered on from).
constexpr int32 MaxSegments = 3;

struct FRegions
{
	uint64 Controlled = 0;
	uint64 Hostiles = 0;
	// Regions with a capture anchor.
	uint64 Anchored = 0;
};

// A force may walk through a region it does not have to secure: one without a capture anchor, or one its team
// already controls, and with no hostile unit in it. Contested or uncontrolled anchored ground is secured.
bool CanPass(const FRegions& Regions, int32 Region);

// The waypoint region for a force standing in Source whose order targets Target, along the shortest route
// (ForceOrders::NextWaypoint). The leg runs past every passable region, up to MaxSegments regions, and ends at
// the first region that must be secured or at the target. A waypoint Applied earlier that still lies ahead on the
// route, with only passable regions before it, is kept so the force does not re-order at every border.
// INDEX_NONE when the target cannot be reached.
int32 Waypoint(const uint64* Graph, int32 Count, int32 Source, int32 Target, int32 Applied, const FRegions& Regions);
}
