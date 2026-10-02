#include "Rules/GoalPath.h"

int32 ForceGoals::NextWaypoint(const uint64* Neighbours, int32 RegionCount, int32 Start, int32 Target)
{
	if (!Neighbours || RegionCount <= 0 || RegionCount > MaxRegions
		|| Start < 0 || Start >= RegionCount || Target < 0 || Target >= RegionCount) return INDEX_NONE;
	if (Start == Target) return Start;
	int32 Queue[MaxRegions];
	int32 FirstStep[MaxRegions];
	uint64 Visited = uint64(1) << Start;
	int32 Read = 0, Write = 0;
	Queue[Write++] = Start;
	FirstStep[Start] = Start;
	while (Read < Write)
	{
		const int32 Current = Queue[Read++];
		for (int32 Next = 0; Next < RegionCount; ++Next)
		{
			const uint64 Bit = uint64(1) << Next;
			if (!(Neighbours[Current] & Bit) || (Visited & Bit)) continue;
			Visited |= Bit;
			FirstStep[Next] = Current == Start ? Next : FirstStep[Current];
			if (Next == Target) return FirstStep[Next];
			Queue[Write++] = Next;
		}
	}
	return INDEX_NONE;
}
