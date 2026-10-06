#include "CombatTarget.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "FailoverNode.h"
#include "Headquarters.h"
#include "Engine/World.h"

bool CombatTarget::IsAliveHostile(const AActor* Target, int32 AttackerTeam)
{
	if (!IsValid(Target))
		return false;
	if (const AArmyUnit* Unit = Cast<AArmyUnit>(Target))
		return Unit->IsAlive() && Unit->GetTeamIndex() != AttackerTeam && IsValid(Unit->GetGroup())
			&& Unit->GetGroup()->GetTeamIndex() == Unit->GetTeamIndex() && Unit->GetGroup()->GetUnits().Contains(Unit);
	if (const AHeadquarters* HQ = Cast<AHeadquarters>(Target))
	{
		const ACommandGameState* State = HQ->GetWorld()->GetGameState<ACommandGameState>();
		// An offline HQ is not a target: the hold, not damage, decides it. An immune one stays a valid order target.
		return HQ->IsOnline() && HQ->TeamIndex != AttackerTeam && State
			&& (State->FriendlyHeadquarters == HQ || State->EnemyHeadquarters == HQ);
	}
	if (const AFailoverNode* Node = Cast<AFailoverNode>(Target))
		return Node->IsAlive() && Node->TeamIndex != AttackerTeam && Node->GetWorld()->GetGameState<ACommandGameState>();
	if (const ACommandBuilding* Building = Cast<ACommandBuilding>(Target))
	{
		const ACommandGameState* State = Building->GetWorld()->GetGameState<ACommandGameState>();
		return Building->IsAlive() && Building->TeamIndex != AttackerTeam && State
			&& State->Buildings.Contains(Building);
	}
	return false;
}

EArmorClass CombatTarget::ArmorClass(const AActor* Target)
{
	if (const AArmyUnit* Unit = Cast<AArmyUnit>(Target))
		return Unit->GetArmorClass();
	if (const AHeadquarters* HQ = Cast<AHeadquarters>(Target))
		return HQ->GetArmorClass();
	if (const AFailoverNode* Node = Cast<AFailoverNode>(Target))
		return Node->GetArmorClass();
	if (const ACommandBuilding* Building = Cast<ACommandBuilding>(Target))
		return Building->GetArmorClass();
	checkNoEntry();
	return EArmorClass::Structure;
}

void CombatTarget::ReceiveAttack(AActor* Target, int32 Damage, AArmyUnit* Attacker)
{
	if (!IsValid(Attacker) || !CombatTarget::IsAliveHostile(Target, Attacker->GetTeamIndex()))
		return;
	if (AArmyUnit* Unit = Cast<AArmyUnit>(Target))
		Unit->ReceiveAttack(Damage, Attacker);
	else if (AHeadquarters* HQ = Cast<AHeadquarters>(Target))
		HQ->ReceiveAttack(Damage, Attacker);
	else if (AFailoverNode* Node = Cast<AFailoverNode>(Target))
		Node->ReceiveAttack(Damage, Attacker);
	else if (ACommandBuilding* Building = Cast<ACommandBuilding>(Target))
		Building->ReceiveAttack(Damage, Attacker);
}

CombatRangePolicy::FRangeTarget CombatTarget::RangeTarget(const AActor* Target, float AttackerRadius)
{
	const FVector Center = Target->GetActorLocation();
	const float Yaw = Target->GetActorRotation().Yaw;
	// Only a building's footprint keeps units out; the HQ and the Failover Node can be walked into.
	const auto Square = [&](float HalfSize, bool bBlocksMovement) {
		return CombatRangePolicy::Box(Center, FVector2D(HalfSize, HalfSize), Yaw, AttackerRadius, bBlocksMovement);
	};
	if (Cast<AArmyUnit>(Target))
		return CombatRangePolicy::FRangeTarget(Center);
	if (Cast<AHeadquarters>(Target))
		return Square(AHeadquarters::HitBoxHalfSize, false);
	if (Cast<AFailoverNode>(Target))
		return Square(AFailoverNode::HitBoxHalfSize, false);
	if (const ACommandBuilding* Building = Cast<ACommandBuilding>(Target))
		return Square(Building->GetFootprintHalfExtent(), true);
	checkNoEntry();
	return CombatRangePolicy::FRangeTarget(Center);
}

double CombatTarget::EdgeDistance(const AArmyUnit& Attacker, const AActor* Target)
{
	return CombatRangePolicy::EdgeDistance(FVector2D(Attacker.GetActorLocation()),
		RangeTarget(Target, Attacker.GetSimpleCollisionRadius()));
}
