#pragma once

#include "CoreMinimal.h"

// Forces of one wave that march to the same waypoint anchor take different lanes instead of piling onto one
// slot rectangle. A lane is an index into a fixed grid around the anchor: rank 0 is the anchor's own line,
// later ranks lie behind it (against the heading), and within a rank the lanes alternate centre, right, left,
// further right, further left. Lane 0 is the anchor itself, so a lone force marches exactly as before. Pure.
namespace LanePolicy
{
constexpr int32 LanesPerRank = 5;
constexpr int32 Ranks = 3;
constexpr int32 LaneCount = LanesPerRank * Ranks;
// A formation is about 280 cm wide; a lane leaves a little air around it.
constexpr float LaneSpacing = 300.f;
constexpr float RankDepth = 260.f;
// A force standing on a lane (not lane 0) while its region needs securing and nobody of its team is in capture
// range of the anchor takes the anchor after this long, so capture never depends on the lane-0 force surviving.
constexpr float ReleaseSeconds = 6.f;
// Forces take lanes against forces that took theirs this recently: one wave, ordered in one turn. A force that has
// been on its way longer is not crowding the anchor with the new arrivals.
constexpr float WaveSeconds = 5.f;

// The lowest lane not in Used (INDEX_NONE entries ignored); lane 0 when none is taken. Beyond LaneCount forces
// the lanes repeat, lowest first, so the result is still deterministic.
int32 Allocate(TConstArrayView<int32> Used);

// Planar offset of a lane from the anchor for a heading (unit, pointing toward the anchor, in Unreal's axes). Lateral
// is to the right of the heading; ranks lie behind it. A zero heading puts every lane on the x axis.
FVector2D Offset(int32 Lane, const FVector2D& Heading);
}
