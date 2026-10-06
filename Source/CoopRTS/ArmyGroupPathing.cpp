#include "ArmyGroupPathing.h"

#include "NavMesh/NavMeshPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationData.h"

DEFINE_LOG_CATEGORY(LogOrderCost);

namespace ArmyGroupPathing
{
namespace
{
FQueryStats Stats;
// Moves shorter than this have a one-point path in a path query; leave them to it.
constexpr double MinimumStraightLength = 5.;
}

const FQueryStats& Snapshot()
{
	return Stats;
}

void Reset()
{
	Stats = FQueryStats();
}

void NotePathQuery()
{
	++Stats.PathQueries;
}

void NoteStraightMove()
{
	++Stats.StraightMoves;
}

FOrderCount::FOrderCount() : StartQueries(Stats.PathQueries), StartStraight(Stats.StraightMoves)
{
	++Stats.Orders;
}

FOrderCount::~FOrderCount()
{
	Stats.OrderPathQueries += PathQueries();
	Stats.OrderStraightMoves += StraightMoves();
}

int64 FOrderCount::PathQueries() const
{
	return Stats.PathQueries - StartQueries;
}

int64 FOrderCount::StraightMoves() const
{
	return Stats.StraightMoves - StartStraight;
}

bool TryStraightPath(const ANavigationData& NavData, const FPathFindingQuery& Query, FNavPathSharedPtr& OutPath)
{
	const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(&NavData);
	if (!Recast || !Query.QueryFilter.IsValid()
		|| FVector::Dist(Query.StartLocation, Query.EndLocation) < MinimumStraightLength)
		return false;
	ARecastNavMesh::FRaycastResult Ray;
	FVector Hit;
	if (ARecastNavMesh::NavMeshRaycast(Recast, Query.StartLocation, Query.EndLocation, Hit, Query.QueryFilter, Query.Owner.Get(), Ray)
		|| !Ray.bIsRaycastEndInCorridor || Ray.CorridorPolysCount <= 0)
		return false;
	FNavPathSharedPtr Shared = NavData.CreatePathInstance<FNavMeshPath>(Query);
	FNavMeshPath* Path = Shared.IsValid() ? Shared->CastPath<FNavMeshPath>() : nullptr;
	if (!Path)
		return false;
	Path->ApplyFlags(Query.NavDataFlags);
	Path->PathCorridor.Append(Ray.CorridorPolys, Ray.CorridorPolysCount);
	for (int32 Index = 0; Index < Ray.CorridorPolysCount; ++Index)
		Path->PathCorridorCost.Add(Ray.CorridorCost[Index]);
	Path->OnPathCorridorUpdated();
	Path->GetPathPoints().Add(FNavPathPoint(Query.StartLocation, Ray.CorridorPolys[0]));
	Path->GetPathPoints().Add(FNavPathPoint(Query.EndLocation, Ray.GetLastNodeRef()));
	Path->MarkReady();
	OutPath = Shared;
	return true;
}
}
