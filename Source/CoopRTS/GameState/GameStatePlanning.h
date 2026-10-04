#pragma once

#include "CoreMinimal.h"
#include "Rules/PlanningPolicy.h"

class ACommandGameState;
class UMatchContent;
class UWorld;

// The planning phase's world-facing helpers; the phase itself is ACommandGameState::BeginPlanning (GameStatePlanning.cpp).
namespace GameStatePlanning
{
// True once buildings can be placed: no navigation system or data at all counts as ready, a build in progress does not.
// Kits are placed on the navmesh, so planning cannot finish before it exists.
bool NavigationReady(UWorld* World);

// The catalogue index of the kit's Barracks (the first building that produces forces) or Drill Rig (the first that needs a
// deposit); INDEX_NONE without one.
int32 KitBuildingIndex(const UMatchContent& Content, bool bRig);
// Every deposit as a rig site for Team: free, with ore left, and whether its region is the team's own and uncontested.
void CollectRigSites(const ACommandGameState& State, int32 Team, TArray<PlanningPolicy::FRigSite>& Out);

// The default spots are ONE rule: the end of planning places by it and the HUD's ghosts show it. Each search offers
// candidates in order and the first Accept takes. The server's Accept places the piece, the ghost's validates it.
// Barracks: rings around the home headquarters (PlanningPolicy::DefaultBarracksSpot), skipping spots that would crowd a free
// deposit. Rig: the nearest free own deposit, then the next when a placement fails.
bool FindDefaultBarracks(const ACommandGameState& State, int32 Team, TFunctionRef<bool(const FVector&)> Accept, FVector& OutSpot);
bool FindDefaultRig(const ACommandGameState& State, int32 Team, TFunctionRef<bool(const FVector&)> Accept, FVector& OutSite);

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
// The wallet every automation fixture starts from (the pre-planning opening); the real opening is smaller.
constexpr int32 FixtureStartingResources = 600;
#endif
}
