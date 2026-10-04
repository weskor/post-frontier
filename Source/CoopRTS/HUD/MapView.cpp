#include "MapPresentation.h"

#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Commands/OrderGraph.h"
#include "DepositSite.h"
#include "MapRegion.h"

namespace MapView
{
MapPresentation::FRegionLink LinkOf(const ACommandGameState& State, int32 Team, int32 Region)
{
	// Only the humans and JEV have a supply chain; a neutral region (controller -1) is nobody's to cut.
	const bool bChain = Team == 0 || Team == 5;
	return { bChain && State.GetRegionController(Region) == Team, State.IsRegionConnected(Team, Region) };
}

bool IsCutOff(const ACommandGameState& State, int32 Team, int32 Region)
{
	return MapPresentation::IsCutOff(LinkOf(State, Team, Region));
}

bool IsRigOffline(const ACommandGameState& State, const ADepositSite& Deposit)
{
	const ACommandBuilding* Rig = Deposit.Extractor;
	return IsValid(Rig) && Rig->IsAlive() && Rig->IsComplete() && Rig->Kind == EBuildingKind::Extractor
		&& Rig->Deposit == &Deposit && !State.IsRegionConnected(Rig->TeamIndex, Deposit.RegionIndex);
}

int32 OfflineRigs(const ACommandGameState& State, int32 Team, int32 Region)
{
	int32 Count = 0;
	for (const ADepositSite* Deposit : State.Deposits)
		Count += IsValid(Deposit) && Deposit->RegionIndex == Region && IsValid(Deposit->Extractor)
			&& Deposit->Extractor->TeamIndex == Team && IsRigOffline(State, *Deposit);
	return Count;
}

void ForEachCable(const ACommandGameState& State, int32 Team,
	TFunctionRef<void(const AMapRegion& A, const AMapRegion& B, MapPresentation::ECable Cable)> Visit)
{
	for (const AMapRegion* From : State.Regions)
	{
		if (!IsValid(From))
			continue;
		for (const int32 Neighbour : From->Neighbours)
		{
			const AMapRegion* To = ForceOrderGraph::Region(State, Neighbour);
			// A pair is one cable: visited from its lower end, or from whichever end lists the other if only one does.
			if (!To || To == From || (Neighbour < From->RegionIndex && To->Neighbours.Contains(From->RegionIndex)))
				continue;
			const MapPresentation::ECable Cable = MapPresentation::ClassifyCable(
				LinkOf(State, Team, From->RegionIndex), LinkOf(State, Team, Neighbour));
			if (Cable != MapPresentation::ECable::None)
				Visit(*From, *To, Cable);
		}
	}
}

float FlashAge(const ACommandGameState& State, int32 Team)
{
	const float Now = State.GetServerWorldTimeSeconds();
	const float ChangedAt = State.GetConnectionChangedAt(Team);
	return MapPresentation::FlashPlays(Now, ChangedAt) ? MapPresentation::ChangeAge(Now, ChangedAt) : -1.f;
}
}
