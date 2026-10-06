#include "Rules/MarchSpeedPolicy.h"

namespace MarchSpeedPolicy
{
namespace
{
// Progress of a member along the heading beyond its slot.
double Progress(const FMember& Member, const FVector2D& Heading)
{
	return FVector2D::DotProduct(Member.Position - Member.SlotOffset, Heading);
}
}

float Lag(TConstArrayView<FMember> Members, int32 Index, const FVector2D& Heading)
{
	if (!Members.IsValidIndex(Index) || Heading.IsNearlyZero())
		return 0.f;
	double Mean = 0.;
	for (const FMember& Member : Members)
		Mean += Progress(Member, Heading);
	Mean /= Members.Num();
	return static_cast<float>(Mean - Progress(Members[Index], Heading));
}

float Factor(float Lag, float ExtraBand)
{
	const float Beyond = FMath::Abs(Lag) - BandHalfWidth - ExtraBand;
	if (Beyond <= 0.f)
		return 1.f;
	const float Reach = FMath::Min(Beyond / RampLength, 1.f);
	return Lag > 0.f ? FMath::Lerp(1.f, MaxCatchUp, Reach) : FMath::Lerp(1.f, MinAhead, Reach);
}

void Factors(TConstArrayView<FMember> Members, const FVector2D& Heading, TArray<float, TInlineAllocator<8>>& Out, float ExtraBand)
{
	Out.Reset();
	for (int32 Index = 0; Index < Members.Num(); ++Index)
		Out.Add(Members.Num() < 2 ? 1.f : Factor(Lag(Members, Index, Heading), ExtraBand));
}
}
