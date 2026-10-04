#pragma once

#include "CoreMinimal.h"
#include "RegionTraitPolicy.generated.h"

// What a region does to the units standing inside it; buildings are unaffected.
UENUM(BlueprintType)
enum class ERegionTrait : uint8
{
	None,
	HighGround,
	Cover,
	Open,
	Hazard
};

namespace RegionTraitPolicy
{
inline constexpr float HighGroundRangeMultiplier = 1.2f;
inline constexpr float CoverDamageMultiplier = .8f;
inline constexpr float OpenSpeedMultiplier = 1.15f;
inline constexpr int32 HazardDamagePerTick = 4;
inline constexpr float HazardTickSeconds = 1.f;

float RangeMultiplier(ERegionTrait Trait);
// One entry of the incoming-multiplier list (see DamagePolicy); 1 when the trait takes no damage off.
float IncomingMultiplier(ERegionTrait Trait);
float SpeedMultiplier(ERegionTrait Trait);
// A force keeps formation by moving at one speed: the Open bonus applies only while every member
// stands in Open ground. An empty force gets nothing.
float ForceSpeedMultiplier(TConstArrayView<ERegionTrait> MemberTraits);
// Advances the time spent inside a Hazard region and returns the whole ticks due. Leaving
// (any other trait) resets the clock, so re-entering waits a full tick.
int32 AdvanceHazard(ERegionTrait Trait, float& InsideSeconds, float DeltaSeconds);
}
