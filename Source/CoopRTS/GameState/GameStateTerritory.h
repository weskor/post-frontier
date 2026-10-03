#pragma once

#include "CoreMinimal.h"

class ACommandGameState;

// Region ownership, contest and anchors derived from the registry, capture points and troops.
namespace GameStateTerritory
{
int32 RegionController(const ACommandGameState& State, int32 RegionIndex);
bool IsRegionContested(const ACommandGameState& State, int32 RegionIndex, int32 ForTeam);
FVector RegionAnchor(const ACommandGameState& State, int32 RegionIndex);
}
