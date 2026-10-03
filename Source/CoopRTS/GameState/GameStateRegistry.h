#pragma once

#include "CoreMinimal.h"

class ACommandGameState;
class AHeadquarters;
class AMapRegion;

// Lookups over the replicated catalogues the game state owns (Regions and the two headquarters).
namespace GameStateRegistry
{
const AMapRegion* FindRegion(const ACommandGameState& State, int32 RegionIndex);
const AMapRegion* FindRegionAt(const ACommandGameState& State, const FVector& Location);
// Friendly headquarters for team 0, enemy headquarters for team 5, otherwise null.
const AHeadquarters* HomeHeadquarters(const ACommandGameState& State, int32 Team);
}
