#pragma once

#include "CoreMinimal.h"

namespace ForceSelectionPolicy
{
enum class EAccess : uint8
{
	None,
	Command,
	Inspect
};

// Adapters resolve actor validity/liveness and team membership before calling.
EAccess ResolveAccess(int32 LocalCommanderIndex, int32 ForceCommanderIndex, bool bSameTeam, bool bAlive);
bool IsNumberAvailable(int32 Number, bool bSolo);
// Badge centres on the edges belong to the box, in either drag direction.
bool IsInScreenBox(const FVector2D& Point, const FVector2D& Start, const FVector2D& End);
}
