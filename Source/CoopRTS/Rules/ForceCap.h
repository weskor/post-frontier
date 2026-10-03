#pragma once

#include "CoreMinimal.h"

namespace ForceCap
{
constexpr int32 Limit(int32 HumanCommanders) { return HumanCommanders == 1 ? 5 : 4; }
// Construction and configuration do not affect occupancy; orphan armies are not buildings.
constexpr bool Counts(bool bAlive, bool bProducer, bool bOwned)
{
	return bAlive && bProducer && bOwned;
}
constexpr bool HasRoom(int32 Count, int32 Cap)
{
	return Cap == 0 || Count < Cap;
}
}
