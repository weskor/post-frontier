#include "GameState/GameStateRegistry.h"

#include "CommandGameState.h"
#include "Headquarters.h"
#include "MapRegion.h"

namespace GameStateRegistry
{
const AMapRegion* FindRegion(const ACommandGameState& State, int32 RegionIndex)
{
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->RegionIndex == RegionIndex)
			return Region;
	return nullptr;
}

const AMapRegion* FindRegionAt(const ACommandGameState& State, const FVector& Location)
{
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->Contains(Location))
			return Region;
	return nullptr;
}

const AHeadquarters* HomeHeadquarters(const ACommandGameState& State, int32 Team)
{
	return Team == 0 ? State.FriendlyHeadquarters.Get() : Team == 5 ? State.EnemyHeadquarters.Get()
																	: nullptr;
}
}
