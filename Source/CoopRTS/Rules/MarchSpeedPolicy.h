#pragma once

#include "CoreMinimal.h"

// Keeps a marching force together. The force's speed stays that of its slowest member; each member's own speed
// is that speed times a factor from where it stands against its slot. A member behind its slot by more than
// about one slot spacing, measured along the heading and against the force's own mean, runs up to MaxCatchUp
// times faster; one ahead of its slot slows down to MinAhead. Inside the band every member keeps the force's
// speed. Pure: no world, no actors.
namespace MarchSpeedPolicy
{
// How far behind or ahead a member may be before its speed changes: about one slot spacing of an encounter layout.
constexpr float BandHalfWidth = 220.f;
// A column stretches along its route on purpose, and easing its head while its tail hurries would bunch it up in a
// bottleneck: on a column leg only a member this much further behind the file than a box allows is delayed.
constexpr float ColumnExtraBand = 500.f;
// From the band edge the factor reaches its limit over this much further lag.
constexpr float RampLength = 220.f;
constexpr float MaxCatchUp = 1.15f;
constexpr float MinAhead = .85f;

struct FMember
{
	// Planar position, and the member's planned target (its slot, absolute or relative to any common centre: only
	// the difference from Position along the heading matters).
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D SlotOffset = FVector2D::ZeroVector;
};

// Planar distance behind (positive) or ahead of (negative) its slot along Heading, against the mean of the members.
float Lag(TConstArrayView<FMember> Members, int32 Index, const FVector2D& Heading);
// The speed factor for a lag, with ExtraBand added to the band on both sides.
float Factor(float Lag, float ExtraBand = 0.f);
// The factor of every member, in order. A zero Heading or fewer than two members: every factor 1.
void Factors(TConstArrayView<FMember> Members, const FVector2D& Heading, TArray<float, TInlineAllocator<8>>& Out, float ExtraBand = 0.f);
}
