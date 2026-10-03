#include "Rules/RouteIntent.h"

RouteIntent::FPath RouteIntent::Path(const uint64* Graph, int32 Count, int32 Source, int32 Target)
{
	FPath Result;
	if (!Graph || Count <= 0 || Count > ForceOrders::MaxRegions || Source < 0 || Source >= Count || Target < 0 || Target >= Count)
		return Result;
	int32 Queue[ForceOrders::MaxRegions], Parent[ForceOrders::MaxRegions];
	uint64 Seen = uint64(1) << Source;
	int32 Read = 0, Write = 1;
	Queue[0] = Source;
	Parent[Source] = Source;
	while (Read < Write && !(Seen & (uint64(1) << Target)))
	{
		const int32 Current = Queue[Read++];
		for (int32 Next = 0; Next < Count; ++Next)
		{
			const uint64 Bit = uint64(1) << Next;
			if (!(Graph[Current] & Bit) || (Seen & Bit))
				continue;
			Seen |= Bit;
			Parent[Next] = Current;
			Queue[Write++] = Next;
		}
	}
	if (!(Seen & (uint64(1) << Target)))
		return Result;
	for (int32 Current = Target;; Current = Parent[Current])
	{
		Result.Regions[Result.Count++] = Current;
		if (Current == Source)
			break;
	}
	for (int32 Index = 0; Index < Result.Count / 2; ++Index)
		Swap(Result.Regions[Index], Result.Regions[Result.Count - Index - 1]);
	return Result;
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
