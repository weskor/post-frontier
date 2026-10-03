#include "Rules/ArmyGroupPolicy.h"

namespace ArmyGroupPolicy
{
FVector FormationOffset(const FFormation& Formation, int32 Slot)
{
	const float Side = Slot % 2 ? 1.f : -1.f;
	if (Formation.bProduced)
		return FVector((Formation.Capacity / 2 - 1 - 2 * (Slot / 2)) * 55.f, Side * 55.f, 0.f);
	return FVector((Formation.bOpposing ? -1.f : 1.f) * (1 - Slot / 2) * 220.f, Side * 140.f, 0.f);
}

int32 FirstVacantSlot(uint32 Occupied, int32 Capacity)
{
	int32 Slot = 0;
	while (Slot < Capacity && (Occupied & (1u << Slot)))
		++Slot;
	return Slot == Capacity ? INDEX_NONE : Slot;
}

bool OwnerPermitted(int32 GroupTeam, int32 OwnerTeam, int32 CommanderIndex, bool bEnemyCommander)
{
	if (OwnerTeam != GroupTeam)
		return false;
	return GroupTeam == 0 ? CommanderIndex >= 0 && CommanderIndex < 5 : GroupTeam == 5 && bEnemyCommander;
}

bool EngagementPermitted(const FEngagement& Engagement)
{
	const float RangeSquared = FMath::Square(Engagement.WeaponRange);
	if (!Engagement.bAttackOrder)
		return Engagement.UnitToEnemy <= RangeSquared;
	const float RadiusSquared = FMath::Square(Engagement.PursuitRadius);
	const bool bNearAnchor = Engagement.EnemyToAnchor <= RadiusSquared && Engagement.UnitToAnchor <= RadiusSquared;
	const bool bEnRoute = Engagement.bMarching && Engagement.UnitToAnchor > RadiusSquared
		&& Engagement.UnitToEnemy <= RangeSquared;
	return (bNearAnchor && Engagement.UnitToEnemy <= FMath::Square(MaxAcquireDistance)) || bEnRoute;
}
}
