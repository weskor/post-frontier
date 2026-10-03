#include "GameState/GameStateTerritory.h"

#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandGameState.h"
#include "EngineUtils.h"
#include "GameState/GameStateRegistry.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "Rules/PlacementPolicy.h"

namespace GameStateTerritory
{
int32 RegionController(const ACommandGameState& State, int32 RegionIndex)
{
	const AMapRegion* Region = GameStateRegistry::FindRegion(State, RegionIndex);
	if (!Region)
		return -1;
	const AHeadquarters* Home = GameStateRegistry::HomeHeadquarters(State, Region->HomeTeam);
	return PlacementPolicy::RegionController(Region->RegionRole == ERegionRole::Main, Region->HomeTeam,
		IsValid(Home) && Home->IsAlive(), IsValid(Region->Anchor) ? Region->Anchor->ControllingTeam : -1);
}

bool IsRegionContested(const ACommandGameState& State, int32 RegionIndex, int32 ForTeam)
{
	const AMapRegion* Region = GameStateRegistry::FindRegion(State, RegionIndex);
	if (!Region || (ForTeam != 0 && ForTeam != 5) || !State.GetWorld())
		return false;
	for (TActorIterator<AArmyUnit> It(State.GetWorld()); It; ++It)
		if (It->IsAlive() && It->GetTeamIndex() != ForTeam && Region->Contains(It->GetActorLocation()))
			return true;
	return false;
}

FVector RegionAnchor(const ACommandGameState& State, int32 RegionIndex)
{
	const AMapRegion* Region = GameStateRegistry::FindRegion(State, RegionIndex);
	if (!Region)
		return FVector::ZeroVector;
	if (Region->RegionRole != ERegionRole::Main)
		return IsValid(Region->Anchor) ? Region->Anchor->GetActorLocation() : FVector::ZeroVector;
	const AHeadquarters* Home = GameStateRegistry::HomeHeadquarters(State, Region->HomeTeam);
	return IsValid(Home) ? Home->GetActorLocation() : FVector::ZeroVector;
}
}
