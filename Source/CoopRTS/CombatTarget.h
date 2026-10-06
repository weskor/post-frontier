#pragma once

#include "CoreMinimal.h"
#include "Rules/CombatPolicy.h"
#include "Rules/CombatRangePolicy.h"

class AActor;
class AArmyUnit;

// Only living roster units, registered buildings and headquarters are combat
// targets. Unrelated replicated actors must not attract or receive attacks.
namespace CombatTarget
{
bool IsAliveHostile(const AActor* Target, int32 AttackerTeam);
EArmorClass ArmorClass(const AActor* Target);
void ReceiveAttack(AActor* Target, int32 Damage, AArmyUnit* Attacker);
// What an attacker of this capsule radius measures its range to: a unit's centre, or a structure's
// footprint / hit box turned with the actor (Barracks, Drill Rig, Workshop, HQ, Failover Node).
CombatRangePolicy::FRangeTarget RangeTarget(const AActor* Target, float AttackerRadius);
// The one distance weapon range is compared with: planar, from the attacker to a unit's centre or a structure's edge.
double EdgeDistance(const AArmyUnit& Attacker, const AActor* Target);
}
