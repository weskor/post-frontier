#include "GameState/GameStateTerritory.h"

#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandGameState.h"
#include "Commands/AbilityCommandComponent.h"
#include "EngineUtils.h"
#include "GameState/GameStateRegistry.h"
#include "Commands/OrderGraph.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "Rules/EconomyPolicy.h"
#include "Rules/ForceOrderPolicy.h"
#include "Rules/MapPresentationPolicy.h"
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

uint64 TeamConnectedMask(const ACommandGameState& State, int32 Team)
{
	uint64 Graph[ForceOrders::MaxRegions];
	const int32 Count = ForceOrderGraph::ReadGraph(State, Graph);
	TArray<int32, TInlineAllocator<ForceOrders::MaxRegions>> Controllers;
	for (int32 Index = 0; Index < Count; ++Index)
		Controllers.Add(GameStateRegistry::FindRegion(State, Index) ? RegionController(State, Index) : -1);
	return EconomyPolicy::ConnectedRegions(Graph, Controllers, ForceOrderGraph::TeamMain(State, Team), Team);
}

namespace
{
// Tells the team's commanders about each region the change left held but unreachable.
void AnnounceCuts(const ACommandGameState& State, int32 Team, uint64 Previous, uint64 Connected)
{
	uint64 Held = 0;
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->RegionIndex >= 0 && Region->RegionIndex < ForceOrders::MaxRegions
			&& RegionController(State, Region->RegionIndex) == Team)
			Held |= uint64(1) << Region->RegionIndex;
	const uint64 Cut = MapPresentation::NewlyCutOff(Previous, Connected, Held);
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->RegionIndex >= 0 && Region->RegionIndex < ForceOrders::MaxRegions
			&& (Cut & (uint64(1) << Region->RegionIndex)))
			UAbilityCommandComponent::PostSupplyCut(*Region, Team);
}
}

bool RefreshConnections(ACommandGameState& State)
{
	bool bChanged = false;
	const float Now = State.GetServerWorldTimeSeconds();
	const auto Publish = [&](FTeamConnection& Connection, int32 Team) {
		const uint64 Mask = TeamConnectedMask(State, Team);
		if (Mask == Connection.Mask)
			return;
		// Only the humans have a team feed; JEV's cuts show on the map alone.
		if (Team == 0)
			AnnounceCuts(State, Team, Connection.Mask, Mask);
		Connection.Mask = Mask;
		Connection.ChangedAt = Now;
		bChanged = true;
	};
	Publish(State.HumanConnection, 0);
	Publish(State.EnemyConnection, 5);
	if (bChanged)
		State.ForceNetUpdate();
	return bChanged;
}
}
