#pragma once
#include "CoreMinimal.h"
class AArmyGroup;
class ACommandGameState;

namespace ForceTravelETA
{
// Travel estimate along the replicated waypoint and remaining shortest graph route.
// Region-anchor segments exclude capture/combat delays and local crowd/nav detours.
int32 Compute(const AArmyGroup& Force, const ACommandGameState& State);
struct FEntry
{
	TWeakObjectPtr<const AArmyGroup> Force;
	uint32 OrderSerial = 0;
	int32 Waypoint = INDEX_NONE;
	double Updated = -1.;
	int32 Seconds = INDEX_NONE;
};
}
