#pragma once

#include "CoreMinimal.h"

class ACommandGameState;

// Region ownership, contest and anchors derived from the registry, capture points and troops.
namespace GameStateTerritory
{
int32 RegionController(const ACommandGameState& State, int32 RegionIndex);
bool IsRegionContested(const ACommandGameState& State, int32 RegionIndex, int32 ForTeam);
FVector RegionAnchor(const ACommandGameState& State, int32 RegionIndex);
// Regions reachable from the team's main through regions the team controls, as a bit per region index.
// A contested region still counts while its controller is the team; the team's main counts while it is controlled.
// This is the one connectivity rule: economy, JEV and presentation read it, never a copy.
uint64 TeamConnectedMask(const ACommandGameState& State, int32 Team);
// Recompute both teams' masks and publish them with the server time of each change. Returns true on a change.
bool RefreshConnections(ACommandGameState& State);
}
