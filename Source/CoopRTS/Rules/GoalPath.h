#pragma once

#include "CoreMinimal.h"

namespace ForceGoals
{
	constexpr int32 MaxRegions = 64;
	// Adjacency bit N addresses stable region index N. Ascending-index BFS breaks ties.
	// Returns Start when already there, INDEX_NONE for invalid or unreachable endpoints.
	int32 NextWaypoint(const uint64* Neighbours, int32 RegionCount, int32 Start, int32 Target);
}
