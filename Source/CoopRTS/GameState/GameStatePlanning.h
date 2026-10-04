#pragma once

#include "CoreMinimal.h"

class UWorld;

// The planning phase's world-facing helpers; the phase itself is ACommandGameState::BeginPlanning (GameStatePlanning.cpp).
namespace GameStatePlanning
{
// True once buildings can be placed: no navigation system or data at all counts as ready, a build in progress does not.
// Kits are placed on the navmesh, so planning cannot finish before it exists.
bool NavigationReady(UWorld* World);

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
// The wallet every automation fixture starts from (the pre-planning opening); the real opening is smaller.
constexpr int32 FixtureStartingResources = 600;
#endif
}
