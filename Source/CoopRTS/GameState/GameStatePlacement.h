#pragma once

#include "CoreMinimal.h"

class ACommandGameState;

// Where a building may stand: grid snap, free-deposit selection, territory and full validation.
namespace GameStatePlacement
{
FVector ResolveLocation(const ACommandGameState& State, int32 BuildingIndex, const FVector& RequestedLocation, int32 Team);
bool IsInBuildTerritory(const ACommandGameState& State, int32 BuildingIndex, int32 Team, const FVector& RequestedLocation);
bool Validate(const ACommandGameState& State, int32 BuildingIndex, int32 Team, const FVector& RequestedLocation, FString& OutReason);
}
