#pragma once

#include "CoreMinimal.h"

class AActor;
class AArmyUnit;

// Only living roster units and headquarters are combat targets. Centralize this
// boundary so an arbitrary replicated actor cannot receive or attract attacks.
namespace CombatTarget
{
	bool IsAliveHostile(const AActor* Target, int32 AttackerTeam);
	void ReceiveAttack(AActor* Target, int32 Damage, AArmyUnit* Attacker);
}
