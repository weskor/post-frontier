#pragma once

#include "CoreMinimal.h"

namespace ForceOrders
{
constexpr int32 MaxRegions = 64;
// Adjacency bit N addresses stable region index N. Ascending-index BFS breaks ties.
// Returns Start when already there, INDEX_NONE for invalid or unreachable endpoints.
int32 NextWaypoint(const uint64* Neighbours, int32 RegionCount, int32 Start, int32 Target);
// The controlled component reachable from Home. Invalid graphs or homes return zero.
uint64 ConnectedMask(const uint64* Neighbours, int32 RegionCount, int32 Home, uint64 ControlledMask);
// Safe means HQ-connected control with no hostiles, reachable from Source through any region.
// Prefer LastHeld, then fewest hops with ascending-index ties; no safe destination returns Home.
// Invalid graphs, sources or homes return INDEX_NONE. An invalid LastHeld is simply ignored.
int32 SafeRegion(const uint64* Neighbours, int32 RegionCount, int32 Source, int32 Home,
	int32 LastHeld, uint64 ControlledMask, uint64 HostileMask);
// Strictly below the percentage threshold. Invalid counts or thresholds above 100 never withdraw.
bool ShouldWithdraw(int32 Alive, int32 Capacity, uint8 Threshold);
// Ceil(80% capacity), or zero for a nonpositive capacity.
int32 ResumeCount(int32 Capacity);
bool ShouldResume(int32 Joined, int32 Capacity);
// A queued order needs a vacant slot among three total orders, active order included.
// Replacement ignores the current nonnegative count.
bool CanQueue(int32 OrderCount, bool bQueue);
// Zero-speed empty orphans do not constrain the selection.
// Empty/all-zero views and invalid speeds (negative or nonfinite) return zero.
float SlowestSpeed(TConstArrayView<float> Speeds);
// Retreat adds 25%; invalid base speeds (negative or nonfinite) return zero.
float TravelSpeed(float Base, bool bRetreat);
}
