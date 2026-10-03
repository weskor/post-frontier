#include "ForceSelectionPolicy.h"

ForceSelectionPolicy::EAccess ForceSelectionPolicy::ResolveAccess(
	int32 LocalCommanderIndex, int32 ForceCommanderIndex, bool bSameTeam, bool bAlive)
{
	if (!bAlive || !bSameTeam || LocalCommanderIndex < 0 || ForceCommanderIndex < 0)
		return EAccess::None;
	return LocalCommanderIndex == ForceCommanderIndex ? EAccess::Command : EAccess::Inspect;
}

bool ForceSelectionPolicy::IsNumberAvailable(int32 Number, bool bSolo)
{
	return Number >= 1 && Number <= (bSolo ? 5 : 4);
}

bool ForceSelectionPolicy::IsInScreenBox(const FVector2D& Point, const FVector2D& Start, const FVector2D& End)
{
	return Point.X >= FMath::Min(Start.X, End.X) && Point.X <= FMath::Max(Start.X, End.X)
		&& Point.Y >= FMath::Min(Start.Y, End.Y) && Point.Y <= FMath::Max(Start.Y, End.Y);
}
