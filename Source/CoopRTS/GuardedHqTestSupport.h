#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "EngineUtils.h"
#include "FailoverNode.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "Rules/HqHoldPolicy.h"

namespace GuardedHqTest
{
// Breaks whatever guards the HQ (a map may place Failover Nodes) and takes it to 0 HP with Attacker's own hits.
inline void TakeOffline(AHeadquarters& Target, AArmyUnit& Attacker)
{
	for (const TWeakObjectPtr<AFailoverNode>& Node : Target.GetNodes())
		if (Node.IsValid())
			Node->ReceiveAttack(100000, &Attacker);
	Target.ReceiveAttack(Target.MaxHealth() * 100, &Attacker);
}

// Outcome fixtures end a battle through the real rules: an HQ at 0 HP is only offline, so the hold has to
// complete. This removes the side's own units from its main, puts Attacker (when none stands there yet) in it,
// and lets the full hold pass in one step. True when the HQ is lost afterwards.
inline bool CompleteHold(AHeadquarters& Target, AArmyUnit* Attacker = nullptr)
{
	UWorld* World = Target.GetWorld();
	ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
	const AMapRegion* Main = State ? State->FindRegionAt(Target.GetActorLocation()) : nullptr;
	if (!Main || !Target.IsOffline())
		return false;
	TArray<AArmyUnit*> Defenders;
	for (TActorIterator<AArmyUnit> It(World); It; ++It)
		if (It->GetTeamIndex() == Target.TeamIndex && Main->Contains(It->GetActorLocation()))
			Defenders.Add(*It);
	for (AArmyUnit* Unit : Defenders)
	{
		if (AArmyGroup* Group = Unit->GetGroup())
			Group->OnMemberDied(Unit);
		if (AController* Controller = Unit->GetController())
			Controller->Destroy();
		Unit->Destroy();
	}
	if (Target.CountPresence(*State).Attackers == 0 && IsValid(Attacker))
		Attacker->SetActorLocation(Target.GetActorLocation() + FVector(450.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
	Target.Tick(HqHoldPolicy::HoldSeconds);
	return !Target.IsAlive();
}
}
#endif
