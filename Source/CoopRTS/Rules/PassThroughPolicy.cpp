#include "Rules/PassThroughPolicy.h"

#include "Rules/ForceOrderPolicy.h"

namespace PassThroughPolicy
{
bool CanPass(const FRegions& Regions, int32 Region)
{
	if (Region < 0 || Region >= 64)
		return false;
	const uint64 Bit = uint64(1) << Region;
	return !(Regions.Hostiles & Bit) && (!(Regions.Anchored & Bit) || (Regions.Controlled & Bit));
}

int32 Waypoint(const uint64* Graph, int32 Count, int32 Source, int32 Target, int32 Applied, const FRegions& Regions)
{
	if (Source == Target)
		return ForceOrders::NextWaypoint(Graph, Count, Source, Target);
	int32 Fresh = INDEX_NONE;
	// Every region before the one examined is passable.
	bool bPassableSoFar = true;
	int32 Current = Source;
	for (int32 Step = 1; Step <= Count; ++Step)
	{
		const int32 Next = ForceOrders::NextWaypoint(Graph, Count, Current, Target);
		if (Next == INDEX_NONE || Next == Current)
			break;
		if (Applied != INDEX_NONE && Applied != Source && Next == Applied && bPassableSoFar)
			return Applied;
		// The leg into the target region stays its own leg (the box at arrival, ArmyGroupPolicy), so a leg that would
		// end there ends in the region before it.
		if (Fresh == INDEX_NONE && (Next == Target || !CanPass(Regions, Next) || Step == MaxSegments))
			Fresh = Next == Target && Current != Source ? Current : Next;
		if (Next == Target)
			break;
		bPassableSoFar = bPassableSoFar && CanPass(Regions, Next);
		if (Fresh != INDEX_NONE && !bPassableSoFar)
			break;
		Current = Next;
	}
	return Fresh != INDEX_NONE ? Fresh : ForceOrders::NextWaypoint(Graph, Count, Source, Target);
}
}
