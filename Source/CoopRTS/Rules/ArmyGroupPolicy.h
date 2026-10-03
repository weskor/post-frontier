#pragma once

#include "CoreMinimal.h"

namespace ArmyGroupPolicy
{
// Slot layout of one force. A produced force packs two columns around its capacity;
// an authored encounter force spreads wider and faces the other way when opposing.
struct FFormation
{
	bool bProduced = false;
	int32 Capacity = 0;
	bool bOpposing = false;
};

// Offset of a composition slot from the force's anchor. Even slots lie on -Y, odd on +Y.
FVector FormationOffset(const FFormation& Formation, int32 Slot);

// Lowest slot below Capacity whose bit is clear in Occupied, or INDEX_NONE when all are taken.
int32 FirstVacantSlot(uint32 Occupied, int32 Capacity);

// Team 0 forces belong to one of the five human commanders; any other force belongs to the
// enemy commander on team 5. The owner's team must also match the force's.
bool OwnerPermitted(int32 GroupTeam, int32 OwnerTeam, int32 CommanderIndex, bool bEnemyCommander);

constexpr float MaxAcquireDistance = 1450.f;

// Whether a living hostile may be engaged by a unit. Without an Attack order that is its weapon
// range. An Attack order engages targets near its anchor within MaxAcquireDistance, and, while
// marching, anything already in weapon range once the unit has left the anchor's pursuit radius.
// All distances are squared and planar.
struct FEngagement
{
	bool bAttackOrder = false;
	bool bMarching = false;
	double UnitToEnemy = 0.;
	double EnemyToAnchor = 0.;
	double UnitToAnchor = 0.;
	float WeaponRange = 0.f;
	float PursuitRadius = 0.f;
};
bool EngagementPermitted(const FEngagement& Engagement);
}
