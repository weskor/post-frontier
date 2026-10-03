#include "Rules/RouteIntent.h"

namespace
{
// One traversal, with no full-path reconstruction for the executor's next hop.
template <bool bFullPath>
int32 FindRoute(const uint64* Graph, int32 Count, int32 Source, int32 Target, RouteIntent::FPath* Path)
{
	if (!Graph || Count <= 0 || Count > ForceOrders::MaxRegions || Source < 0 || Source >= Count || Target < 0 || Target >= Count)
		return INDEX_NONE;
	if (Source == Target)
	{
		if constexpr (bFullPath)
		{
			Path->Regions[0] = Source;
			Path->Count = 1;
		}
		return Source;
	}
	int32 Queue[ForceOrders::MaxRegions], ParentOrFirst[ForceOrders::MaxRegions];
	uint64 Seen = uint64(1) << Source;
	int32 Read = 0, Write = 1;
	Queue[0] = Source;
	ParentOrFirst[Source] = Source;
	while (Read < Write)
	{
		const int32 Current = Queue[Read++];
		for (int32 Next = 0; Next < Count; ++Next)
		{
			const uint64 Bit = uint64(1) << Next;
			if (!(Graph[Current] & Bit) || (Seen & Bit))
				continue;
			Seen |= Bit;
			if constexpr (bFullPath)
				ParentOrFirst[Next] = Current;
			else
				ParentOrFirst[Next] = Current == Source ? Next : ParentOrFirst[Current];
			if (Next == Target)
			{
				if constexpr (!bFullPath)
					return ParentOrFirst[Next];
				else
				{
					for (int32 At = Target;; At = ParentOrFirst[At])
					{
						Path->Regions[Path->Count++] = At;
						if (At == Source)
							break;
					}
					for (int32 Index = 0; Index < Path->Count / 2; ++Index)
						Swap(Path->Regions[Index], Path->Regions[Path->Count - Index - 1]);
					return Path->Regions[1];
				}
			}
			Queue[Write++] = Next;
		}
	}
	return INDEX_NONE;
}
}

RouteIntent::FPath RouteIntent::Path(const uint64* Graph, int32 Count, int32 Source, int32 Target)
{
	FPath Result;
	FindRoute<true>(Graph, Count, Source, Target, &Result);
	return Result;
}

int32 ForceOrders::NextWaypoint(const uint64* Graph, int32 Count, int32 Source, int32 Target)
{
	return FindRoute<false>(Graph, Count, Source, Target, nullptr);
}

RouteIntent::FPolyline RouteIntent::Polyline(const FVector& Start, TConstArrayView<int32> Regions,
	TConstArrayView<FVector> Anchors, bool bIncludeSource)
{
	FPolyline Result;
	if (Start.ContainsNaN() || Regions.IsEmpty() || Regions.Num() > ForceOrders::MaxRegions)
		return Result;
	for (int32 Region : Regions)
		if (!Anchors.IsValidIndex(Region) || Anchors[Region].ContainsNaN())
			return Result;
	Result.Points[Result.Count++] = Start;
	for (int32 Index = bIncludeSource || Regions.Num() == 1 ? 0 : 1; Index < Regions.Num(); ++Index)
	{
		const FVector& Point = Anchors[Regions[Index]];
		if (!Point.Equals(Result.Points[Result.Count - 1], .01))
			Result.Points[Result.Count++] = Point;
	}
	return Result;
}
