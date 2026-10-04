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

void Observe(const ACommandGameState& State, int32 Team, MapPresentation::FCutObserver& Observer)
{
	uint64 Neighbours[ForceOrders::MaxRegions] = {};
	ForceOrderGraph::ReadGraph(State, Neighbours);
	MapPresentation::FObservation In;
	In.Connected = State.GetConnectedMask(Team);
	In.Neighbours = Neighbours;
	In.ChangedAt = State.GetConnectionChangedAt(Team);
	In.Now = State.GetServerWorldTimeSeconds();
	const int32 Opponent = Team == 0 ? 5 : 0;
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region) || Region->RegionIndex < 0 || Region->RegionIndex >= ForceOrders::MaxRegions)
			continue;
		const uint64 Bit = uint64(1) << Region->RegionIndex;
		const int32 Controller = State.GetRegionController(Region->RegionIndex);
		In.Held |= Controller == Team ? Bit : 0;
		In.Opponent |= Controller == Opponent ? Bit : 0;
	}
	In.CutOff = In.Held & ~In.Connected;
	MapPresentation::Observe(Observer, In);
}

void ForEachCable(const ACommandGameState& State, int32 Team, const MapPresentation::FCutObserver& Observer,
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
			MapPresentation::ECable Cable = MapPresentation::ClassifyCable(
				LinkOf(State, Team, From->RegionIndex), LinkOf(State, Team, Neighbour));
			if (Cable == MapPresentation::ECable::None
				&& (Observer.IsSnapped(From->RegionIndex, Neighbour) || Observer.IsSnapped(Neighbour, From->RegionIndex)))
				Cable = MapPresentation::ECable::Snapped;
			if (Cable != MapPresentation::ECable::None)
				Visit(*From, *To, Cable);
		}
	}
}
}
