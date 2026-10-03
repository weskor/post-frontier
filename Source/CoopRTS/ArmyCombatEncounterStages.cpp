#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyCombatScenario.h"

namespace ArmyCombatScenarioPrivate
{
bool FArmyCombatScenario::MoveHoldStage(double Now)
{
	for (const AArmyUnit* Unit : Army->GetUnits())
	{
		const AAIController* AI = Cast<AAIController>(Unit->GetController());
		const UPathFollowingComponent* Path = AI ? AI->GetPathFollowingComponent() : nullptr;
		if (!Check(!Unit->bPursuing && Path && Path->GetPath().IsValid()
					&& FVector::Dist2D(Path->GetPath()->GetEndLocation(), Army->Destination) < 450.f,
				TEXT("Travelling MoveHold members keep their formation route instead of chasing an off-route hostile")))
			return true;
	}
	if (Now - StageStarted < 1.)
		return false;
	if (!Check(FVector::Dist2D(MoveStart, Army->GetCenter()) > 100.f
				&& TotalAttacks() > MoveAttacks,
			TEXT("MoveHold physically travels and fires on an off-route hostile without pursuing it")))
		return true;
	const ACommandGameState* State = Army->GetWorld()->GetGameState<ACommandGameState>();
	if (!Check(FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::Attack,
				   ArmyTestSetup::TravelRegion(Army.Get(), State->EnemyHeadquarters->GetActorLocation()))
				   .IsAccepted(),
			TEXT("An owned region Attack replaces the travelling MoveHold")))
		return true;
	const FVector Approach = (Army->Destination - Army->GetCenter()).GetSafeNormal2D();
	for (AArmyUnit* Unit : Enemy->GetUnits())
		Unit->SetActorLocation(Army->GetUnits()[0]->GetActorLocation() + Approach * 100.f,
			false, nullptr, ETeleportType::TeleportPhysics);
	EncounterAttacks = TotalAttacks();
	SetStage(1, Now);
	return false;
}

bool FArmyCombatScenario::AttackStage(double Now)
{
	if (Now - StageStarted < 1.)
		return false;
	if (!Check(TotalAttacks() > EncounterAttacks, TEXT("Region Attack acquires a nearby hostile and fires a real weapon")))
		return true;
	Victim = Enemy->GetUnits().Last();
	const int32 Count = Enemy->GetUnits().Num();
	Victim->ReceiveAttack(Victim->GetHealth(), Army->GetUnits()[0]);
	if (!Check(!Victim->IsAlive() && Victim->GetHealth() == 0 && Enemy->GetUnits().Num() == Count - 1,
			TEXT("Server lethal damage removes a dead member from its force")))
		return true;
	static_cast<AActor*>(Army.Get())->Tick(.25f);
	for (const AArmyUnit* Unit : Army->GetUnits())
		if (!Check(Unit->Target != Victim.Get(), TEXT("Acquisition never retains a dead hostile")))
			return true;
	const ACommandGameState* State = Army->GetWorld()->GetGameState<ACommandGameState>();
	const FVector RetreatStart = State->GetRegionAnchor(ArmyTestSetup::RegionAt(State, State->EnemyHeadquarters->GetActorLocation()));
	for (AArmyUnit* Unit : Army->GetUnits())
		Unit->SetActorLocation(RetreatStart + FVector(1200.f, Unit->GetCompositionSlot() * 100.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
	if (!Check(FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::Retreat).IsAccepted(),
			TEXT("Retreat replaces the region attack")))
		return true;
	for (const AArmyUnit* Unit : Army->GetUnits())
		if (!Check(!Unit->Target, TEXT("Retreat immediately clears each combat target")))
			return true;
	if (!Check(Army->Verb == EForceVerb::Retreat && Army->Status == EForceStatus::Retreating,
			TEXT("Distant orphan begins active Retreat motion")))
		return true;
	RetreatCenter = Army->GetCenter();
	for (AArmyUnit* Unit : Army->GetUnits())
		Unit->NextAttackTime = 0.f;
	for (AArmyUnit* Unit : Enemy->GetUnits())
	{
		Unit->NextAttackTime = TNumericLimits<float>::Max();
		Unit->SetActorLocation(Army->GetUnits()[0]->GetActorLocation() + FVector(100.f, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
	}
	RetreatAttacks = TotalAttacks();
	SetStage(2, Now);
	return false;
}

bool FArmyCombatScenario::RetreatStage(double Now)
{
	if (Army->Verb == EForceVerb::Retreat && Army->Status == EForceStatus::Retreating)
	{
		if (!Check(TotalAttacks() == RetreatAttacks,
				TEXT("Active Retreat suppresses weapon fire while a living hostile remains in range")))
			return true;
		bObservedRetreatMotion |= FVector::Dist2D(RetreatCenter, Army->GetCenter()) > 200.f;
		// Follow the retreat with living hostiles, without extending suppression
		// into the completed MoveHold's normal defensive combat.
		for (AArmyUnit* Unit : Enemy->GetUnits())
			Unit->SetActorLocation(Army->GetUnits()[0]->GetActorLocation() + FVector(100.f, 0.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
		if (Now - StageStarted < 3.5 || !bObservedRetreatMotion)
			return false;
	}
	else
	{
		if (Army->Verb == EForceVerb::MoveHold && Army->Status == EForceStatus::Marching)
			return false; // Completion can still assemble the safe-region formation.
		if (!Check(bObservedRetreatMotion && Army->IsHoldingRegion()
					&& Army->HoldRegionIndex == Army->TargetRegionIndex
					&& Army->HoldPostIndex != INDEX_NONE
					&& ArmyTestSetup::CurrentRegion(Army.Get()) == Army->TargetRegionIndex,
				TEXT("Physically moving orphan Retreat completes into its safe-region hold, including live alarm response")))
			return true;
	}
	if (!RejectUnregisteredTargets())
		return true;
	Test->AddInfo(TEXT("Live combat passed: role-specific weapon range/damage, automatic counter acquisition, region Attack, server death and Retreat fire suppression."));
	return true;
}
}

#endif
