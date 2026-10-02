#include "GoalGraph.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "ArmyGroup.h"
#include "MapRegion.h"

namespace CommandGoalGraph
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
	for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index)
		Graph[Index] = 0;
	int32 Count = 0;
	for (const AMapRegion* Item : State.Regions)
	{
		if (!IsValid(Item) || Item->RegionIndex < 0 || Item->RegionIndex >= ForceGoals::MaxRegions)
			continue;
		Count = FMath::Max(Count, Item->RegionIndex + 1);
		for (int32 Next : Item->Neighbours)
			if (Next >= 0 && Next < ForceGoals::MaxRegions && Region(State, Next))
				Graph[Item->RegionIndex] |= uint64(1) << Next;
	}
	return Count;
}

int32 SourceRegion(const ACommandBuilding& Building, const ACommandGameState& State)
{
	int32 Joined = 0, Travelling = 0;
	Building.GetForceCounts(Joined, Travelling);
	const FVector Position = Joined > 0 && IsValid(Building.ForceGroup)
		? Building.ForceGroup->GetCenter()
		: Building.GetActorLocation();
	const AMapRegion* Source = State.FindRegionAt(Position);
	return Source ? Source->RegionIndex : INDEX_NONE;
}

int32 EnemyMain(const ACommandGameState& State, int32 Team)
{
	int32 Result = INDEX_NONE;
	for (const AMapRegion* Item : State.Regions)
		if (IsValid(Item) && Item->RegionRole == ERegionRole::Main && Item->HomeTeam == (Team == 0 ? 5 : 0)
			&& (Result == INDEX_NONE || Item->RegionIndex < Result))
			Result = Item->RegionIndex;
	return Result;
}
}
