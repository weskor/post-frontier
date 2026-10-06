#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavigationTypes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogOrderCost, Log, All);

class ANavigationData;
struct FPathFindingQuery;

// The cost of moving a force, counted: how many synchronous path queries were solved, how many moves a navmesh
// raycast answered instead, in all and inside orders. The simulation report reads Snapshot().
namespace ArmyGroupPathing
{
struct FQueryStats
{
	int64 PathQueries = 0; // FindPathSync calls of any caller of PrepareMove or an assembly check
	int64 StraightMoves = 0; // moves answered by a clear straight line, no path query
	int64 Orders = 0; // IssueTravel calls that planned moves (applied or only validated)
	int64 OrderPathQueries = 0;
	int64 OrderStraightMoves = 0;
};

const FQueryStats& Snapshot();
void Reset();
void NotePathQuery();
void NoteStraightMove();

// The queries and straight moves one order spends, from construction to now: construct it on the stack around the
// order's planning; its destructor adds them to the order totals.
class FOrderCount
{
public:
	FOrderCount();
	~FOrderCount();
	int64 PathQueries() const;
	int64 StraightMoves() const;

private:
	int64 StartQueries, StartStraight;
};

// A path along the straight line Query.StartLocation to Query.EndLocation, built from a navmesh raycast, when
// that line stays on walkable ground to its end. The corridor is the raycast's polygons, which is the corridor
// a path query would find for a clear line (the straight line is the shortest path across a navmesh), in the
// form the crowd-following component reads. False when the line is blocked, leaves the navmesh, or is too short
// or too long for one raycast corridor: the caller asks for a path query then.
bool TryStraightPath(const ANavigationData& NavData, const FPathFindingQuery& Query, FNavPathSharedPtr& OutPath);
}
