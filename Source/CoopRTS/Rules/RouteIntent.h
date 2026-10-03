#pragma once

#include "CoreMinimal.h"
#include "Rules/ForceOrderPolicy.h"

namespace RouteIntent
{
struct FPath
{
	int32 Regions[ForceOrders::MaxRegions];
	int32 Count = 0;
};
struct FPolyline
{
	FVector Points[ForceOrders::MaxRegions + 1];
	int32 Count = 0;
};
// Same ascending-index shortest-path tie break as ForceOrders::NextWaypoint.
FPath Path(const uint64* Graph, int32 Count, int32 Source, int32 Target);
// The force centre replaces the source anchor; include it only while securing that waypoint.
// Invalid region indices/nonfinite points reject the entire line, never bridge a missing region.
FPolyline Polyline(const FVector& Start, TConstArrayView<int32> Regions,
	TConstArrayView<FVector> Anchors, bool bIncludeSource = false);
}
