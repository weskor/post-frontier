#pragma once

#include "CoreMinimal.h"
#include "Rules/ForceCap.h"

class ACommandGameState;
class ACommandPlayerState;

namespace CommandForceCap
{
struct FOccupancy
{
	int32 Count = 0;
	int32 Limit = 0;
	bool IsFull() const { return !ForceCap::HasRoom(Count, Limit); }
};
int32 HumanCommanderCount(const ACommandGameState& State);
FOccupancy Read(const ACommandGameState& State, const ACommandPlayerState& Commander);
FString BlockReason(const FOccupancy& Occupancy);
}
