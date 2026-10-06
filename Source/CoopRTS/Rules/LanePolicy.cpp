#include "Rules/LanePolicy.h"

namespace LanePolicy
{
int32 Allocate(TConstArrayView<int32> Used)
{
	// Repeat the grid for every full set of LaneCount forces already placed.
	TArray<int32, TInlineAllocator<32>> Counts;
	Counts.Init(0, LaneCount);
	for (const int32 Lane : Used)
		if (Lane >= 0)
			++Counts[Lane % LaneCount];
	int32 Best = 0;
	for (int32 Lane = 1; Lane < LaneCount; ++Lane)
		if (Counts[Lane] < Counts[Best])
			Best = Lane;
	return Best;
}

FVector2D Offset(int32 Lane, const FVector2D& Heading)
{
	const int32 Slot = FMath::Max(0, Lane) % LaneCount;
	const int32 Rank = Slot / LanesPerRank;
	const int32 Index = Slot % LanesPerRank;
	// 0, +1, -1, +2, -2
	const int32 Lateral = Index == 0 ? 0 : (Index % 2 ? (Index + 1) / 2 : -(Index / 2));
	const FVector2D Forward = Heading.IsNearlyZero() ? FVector2D(1., 0.) : Heading.GetSafeNormal();
	const FVector2D Right(Forward.Y, -Forward.X);
	return Right * (Lateral * LaneSpacing) - Forward * (Rank * RankDepth);
}
}
