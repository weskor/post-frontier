#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavigationTypes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogOrderCost, Log, All);

class ANavigationData;
struct FPathFindingQuery;

// The cost of moving a force, counted: how many synchronous path queries were solved, how many moves a navmesh
// raycast answered instead, in all and inside orders. The simulation report reads Snapshot().
// Counted: every query made through PrepareMove (re-path, pursuit, hold, join, order slots) and the assembly check
// of a force with no members. NOT counted, so the totals are a lower bound: the sync pathfinding that
// AAIController::MoveToLocation does at a pursuit's end (ArmyGroupCombat.cpp) and the engine's own re-paths.
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
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
// Zeroes the counters: for tests that measure one order, and for the simulation, which starts each match from zero.
void Reset();
#endif
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
// that line stays on walkable ground to its end. The corridor is the raycast's polygons and the path points are
// the two ends, the form the crowd-following component reads. The corridor costs stay zero (the raycast does not
// compute them; nothing in the game reads path cost). It stands for the path a query would return only while
// every walkable area costs the same, which holds today (the one area in use is the null area): a straight line
// is then the shortest path across a navmesh. A weighted area would make a detour cheaper than the line and
// this function wrong for it. False when the line is blocked, leaves the navmesh, or is too short
// or too long for one raycast corridor: the caller asks for a path query then.
bool TryStraightPath(const ANavigationData& NavData, const FPathFindingQuery& Query, FNavPathSharedPtr& OutPath);
}
