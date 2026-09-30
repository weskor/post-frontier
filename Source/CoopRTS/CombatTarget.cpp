#include "CombatTarget.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Headquarters.h"
#include "Engine/World.h"

bool CombatTarget::IsAliveHostile(const AActor* Target, int32 AttackerTeam)
{
	if (!IsValid(Target)) return false;
	if (const AArmyUnit* Unit = Cast<AArmyUnit>(Target))
		return Unit->IsAlive() && Unit->TeamIndex != AttackerTeam && IsValid(Unit->Group)
			&& Unit->Group->TeamIndex == Unit->TeamIndex && Unit->Group->Units.Contains(Unit);
	if (const AHeadquarters* HQ = Cast<AHeadquarters>(Target))
	{
		const ACommandGameState* State = HQ->GetWorld()->GetGameState<ACommandGameState>();
		return HQ->IsAlive() && HQ->TeamIndex != AttackerTeam && State
			&& (State->FriendlyHeadquarters == HQ || State->EnemyHeadquarters == HQ);
	}
	if (const ACommandBuilding* Building = Cast<ACommandBuilding>(Target))
	{
		const ACommandGameState* State = Building->GetWorld()->GetGameState<ACommandGameState>();
		return Building->IsAlive() && Building->TeamIndex != AttackerTeam && State
			&& State->Buildings.Contains(Building);
	}
	return false;
}

void CombatTarget::ReceiveAttack(AActor* Target, int32 Damage, AArmyUnit* Attacker)
{
	if (!IsValid(Attacker) || !CombatTarget::IsAliveHostile(Target, Attacker->TeamIndex)) return;
	if (AArmyUnit* Unit = Cast<AArmyUnit>(Target)) Unit->ReceiveAttack(Damage, Attacker);
	else if (AHeadquarters* HQ = Cast<AHeadquarters>(Target)) HQ->ReceiveAttack(Damage, Attacker);
	else if (ACommandBuilding* Building = Cast<ACommandBuilding>(Target)) Building->ReceiveAttack(Damage, Attacker);
}
