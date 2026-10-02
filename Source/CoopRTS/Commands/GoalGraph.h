#pragma once
#include "CoreMinimal.h"

class ACommandBuilding;
class ACommandGameState;
class AMapRegion;

namespace CommandGoalGraph
{
const AMapRegion* Region(const ACommandGameState& State, int32 Index);
int32 ReadGraph(const ACommandGameState& State, uint64* Graph);
int32 SourceRegion(const ACommandBuilding& Building, const ACommandGameState& State);
int32 EnemyMain(const ACommandGameState& State, int32 Team);
}
