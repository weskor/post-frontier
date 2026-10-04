#include "RegionTraitPolicy.h"

float RegionTraitPolicy::RangeMultiplier(ERegionTrait Trait)
{
	return Trait == ERegionTrait::HighGround ? HighGroundRangeMultiplier : 1.f;
}

float RegionTraitPolicy::IncomingMultiplier(ERegionTrait Trait)
{
	return Trait == ERegionTrait::Cover ? CoverDamageMultiplier : 1.f;
}

float RegionTraitPolicy::SpeedMultiplier(ERegionTrait Trait)
{
	return Trait == ERegionTrait::Open ? OpenSpeedMultiplier : 1.f;
}

int32 RegionTraitPolicy::AdvanceHazard(ERegionTrait Trait, float& InsideSeconds, float DeltaSeconds)
{
	if (Trait != ERegionTrait::Hazard)
	{
		InsideSeconds = 0.f;
		return 0;
	}
	InsideSeconds += FMath::Max(0.f, DeltaSeconds);
	const int32 Ticks = FMath::FloorToInt(InsideSeconds / HazardTickSeconds);
	InsideSeconds -= Ticks * HazardTickSeconds;
	return Ticks;
}

float RegionTraitPolicy::ForceSpeedMultiplier(TConstArrayView<ERegionTrait> MemberTraits)
{
	if (MemberTraits.IsEmpty())
		return 1.f;
	for (const ERegionTrait Trait : MemberTraits)
		if (Trait != ERegionTrait::Open)
			return 1.f;
	return OpenSpeedMultiplier;
}
