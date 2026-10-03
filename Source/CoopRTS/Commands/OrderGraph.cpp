#include "OrderGraph.h"
#include "Rules/ForceOrderPolicy.h"
#include "CommandGameState.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "MapRegion.h"

namespace ForceOrderGraph
{
const AMapRegion* Region(const ACommandGameState& State, int32 Index)
{
	for (const AMapRegion* Candidate : State.Regions)
		if (IsValid(Candidate) && Candidate->RegionIndex == Index)
			return Candidate;
	return nullptr;
}

int32 ReadGraph(const ACommandGameState& State, uint64* Graph)
{
	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
		Graph[Index] = 0;
	int32 Count = 0;
	for (const AMapRegion* Item : State.Regions)
	{
		if (!IsValid(Item) || Item->RegionIndex < 0 || Item->RegionIndex >= ForceOrders::MaxRegions)
			continue;
		Count = FMath::Max(Count, Item->RegionIndex + 1);
		for (int32 Next : Item->Neighbours)
			if (Next >= 0 && Next < ForceOrders::MaxRegions && Region(State, Next))
				Graph[Item->RegionIndex] |= uint64(1) << Next;
	}
	return Count;
}

int32 SourceRegion(const AArmyGroup& Force, const ACommandGameState& State)
{
	FVector Position;
	if (Force.GetJoinedCount() > 0)
		Position = Force.GetCenter();
	else
	{
		Position = FVector::ZeroVector;
		int32 Travelling = 0;
		for (const AArmyUnit* Unit : Force.GetUnits())
			if (IsValid(Unit) && Unit->IsAlive())
			{
				Position += Unit->GetActorLocation();
				++Travelling;
			}
		// An assembling force progresses through already-controlled waypoints
		// without requiring recruits to join another force's occupied slots.
		Position = Travelling > 0 ? Position / Travelling : Force.GetHomeLocation();
	}
	const AMapRegion* Source = State.FindRegionAt(Position);
	return Source ? Source->RegionIndex : INDEX_NONE;
}

int32 TeamMain(const ACommandGameState& State, int32 Team)
{
	int32 Result = INDEX_NONE;
	for (const AMapRegion* Item : State.Regions)
		if (IsValid(Item) && Item->RegionRole == ERegionRole::Main && Item->HomeTeam == Team
			&& (Result == INDEX_NONE || Item->RegionIndex < Result))
			Result = Item->RegionIndex;
	return Result;
}

int32 EnemyMain(const ACommandGameState& State, int32 Team)
{
	return TeamMain(State, Team == 0 ? 5 : 0);
}
}
