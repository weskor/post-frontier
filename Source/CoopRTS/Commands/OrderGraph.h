#pragma once
#include "CoreMinimal.h"

class AArmyGroup;
class ACommandGameState;
class AMapRegion;

namespace ForceOrderGraph
{
const AMapRegion* Region(const ACommandGameState& State, int32 Index);
int32 ReadGraph(const ACommandGameState& State, uint64* Graph);
int32 SourceRegion(const AArmyGroup& Force, const ACommandGameState& State);
int32 TeamMain(const ACommandGameState& State, int32 Team);
int32 EnemyMain(const ACommandGameState& State, int32 Team);
}
