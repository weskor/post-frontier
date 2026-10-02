#pragma once

#include "CoreMinimal.h"
#include "Rules/CombatPolicy.h"

class AActor;
class AArmyUnit;

// Only living roster units, registered buildings and headquarters are combat
// targets. Unrelated replicated actors must not attract or receive attacks.
namespace CombatTarget
{
bool IsAliveHostile(const AActor* Target, int32 AttackerTeam);
EArmorClass ArmorClass(const AActor* Target);
void ReceiveAttack(AActor* Target, int32 Damage, AArmyUnit* Attacker);
}
