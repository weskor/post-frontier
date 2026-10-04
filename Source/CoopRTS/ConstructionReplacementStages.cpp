#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ConstructionScenario.h"

namespace ConstructionScenarioTests
{
bool FConstructionScenario::StageFour(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	// The finished replacement is paid at once and travels the supply chain as a queue entry.
	if (Squad->GetPendingRecruitCount() == 0)
		return false;
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
	const int32 Capacity = Building->GetProductionDefinition()->Capacity;
	if (!Check(Wallet->Resources == ReplacementBalance - Building->GetProductionDefinition()->UnitCost && UnrelatedWallet->Resources == 777
				&& Alive(Building.Get()) == Capacity
				&& Alive(Producers[1].Get()) == Producers[1]->GetProductionDefinition()->Capacity,
			TEXT("Replacement debits only its own commander, without taking another producer's capacity")))
		return true;
	if (!Check(Squad->GetJoinedCount() == Capacity - 1 && Squad->GetUnits().Num() == Capacity - 1 && JoinedCenterMatches(Squad.Get()),
			TEXT("The replacement is a queue entry, not a walker: no member exists for it and the force center is unchanged")))
		return true;
	// Retarget to clear home ground, not another full force's exact slots.
	// This isolates delivery to a moving formation from cross-force capsule blockage.
	const int32 Region = ForceOrderGraph::TeamMain(*State, Squad->GetTeamIndex());
	if (Region == INDEX_NONE || !FCommandService::IssueForceOrder(Wallet, Squad.Get(), EForceVerb::MoveHold, Region))
		return Fail(TEXT("Replacement order needs a different reachable region"));
	Stage = 5;
	return false;
}

bool FConstructionScenario::StageFive(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	const int32 Capacity = Building->GetProductionDefinition()->Capacity;
	if (Squad->GetPendingRecruitCount() > 0)
		return !Check(Squad->GetUnits().Num() == Capacity - 1 && JoinedCenterMatches(Squad.Get()),
			TEXT("A moving force's center stays on its joined members while the replacement is in transit"));
	Recruit = nullptr;
	for (AArmyUnit* Unit : Squad->GetUnits())
		if (IsValid(Unit) && Unit->IsAlive() && Unit->GetCompositionSlot() == VictimSlot)
			Recruit = Unit;
	if (!Check(Recruit.IsValid() && Squad->GetJoinedCount() == Capacity
				&& FVector::Dist2D(Recruit->GetActorLocation(), Squad->GetCenter()) < 500.f
				&& Forces[1]->TargetRegionIndex == OtherRegion && Forces[1]->Verb == OtherVerb,
			TEXT("The replacement appears in its vacated slot of the moving force, without altering the other force")))
		return true;
	while (!Squad->GetUnits().IsEmpty())
	{
		AArmyUnit* Unit = Squad->GetUnits().Last();
		Unit->ReceiveAttack(Unit->GetHealth(), Attacker->GetUnits()[0]);
		if (!Check(!Unit->IsAlive() && !Squad->GetUnits().Contains(Unit), TEXT("Each lethal hit removes its force member")))
			return true;
	}
	if (!Check(IsValid(Building->ForceGroup) && Building->ForceGroup == Squad.Get() && Alive(Building.Get()) == 0,
			TEXT("Complete wipe retains the same empty force identity")))
		return true;
	RememberedRegion = Squad->TargetRegionIndex;
	ReplacementBalance = Wallet->Resources;
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, true);
	Stage = 6;
	return false;
}

bool FConstructionScenario::StageSix(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	if (Alive(Building.Get()) == 0)
		return false;
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
	Recruit = Squad->GetUnits().IsEmpty() ? nullptr : Squad->GetUnits()[0].Get();
	if (!Check(Recruit.IsValid() && Building->ForceGroup == Squad.Get() && Squad->GetJoinedCount() == 1
				&& Squad->GetPendingRecruitCount() == 0 && Squad->TargetRegionIndex == RememberedRegion
				&& FVector::Dist2D(Recruit->GetActorLocation(), Building->GetActorLocation()) < 1200.f
				&& Wallet->Resources == ReplacementBalance - Building->GetProductionDefinition()->UnitCost,
			TEXT("Wiped force refills at the producer exit under the original identity and remembered region")))
		return true;
	Recruit->ReceiveAttack(Recruit->GetHealth(), Attacker->GetUnits()[0]);
	if (!Check(!Recruit->IsAlive() && Alive(Building.Get()) == 0,
			TEXT("Killing the recruit reopens its paid vacancy")))
		return true;
	ReplacementBalance = Wallet->Resources;
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, true);
	Stage = 7;
	return false;
}

bool FConstructionScenario::StageSeven(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	if (Alive(Building.Get()) == 0)
		return false;
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
	if (!Check(Wallet->Resources == ReplacementBalance - Building->GetProductionDefinition()->UnitCost, TEXT("Replacing a dead recruit charges again")))
		return true;
	if (Squad->GetJoinedCount() == 0)
		return false;
	if (!Check(Squad->GetJoinedCount() == 1 && Squad->GetPendingRecruitCount() == 0,
			TEXT("Paid replacement joins at the exit before producer destruction")))
		return true;
	Building->ReceiveAttack(Building->Health, Attacker->GetUnits()[0]);
	if (!Check(Squad.IsValid() && !IsValid(Squad->GetProductionBuilding()) && Squad->TargetRegionIndex == RememberedRegion,
			TEXT("Destroyed producer leaves survivors on their last region order with no producer transfer")))
		return true;
	SurvivorCount = Squad->GetUnits().Num();
	ReplacementBalance = Wallet->Resources;
	Blocker = World->SpawnActor<AActor>();
	if (!Blocker.IsValid())
		return Fail(TEXT("Deployment obstruction fixture could not spawn"));
	UBoxComponent* Box = NewObject<UBoxComponent>(Blocker.Get());
	Blocker->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(1400.f, 1400.f, 300.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Blocker->SetActorLocation(Producers[2]->GetActorLocation());
	while (!Forces[2]->GetUnits().IsEmpty())
	{
		AArmyUnit* Unit = Forces[2]->GetUnits().Last();
		Unit->ReceiveAttack(Unit->GetHealth(), Attacker->GetUnits()[0]);
		if (!Check(!Unit->IsAlive() && !Forces[2]->GetUnits().Contains(Unit), TEXT("Blocked-producer casualty is real lethal damage")))
			return true;
	}
	FCommandService::ConfigureProduction(Wallet, Producers[2].Get(), EUnitRole::Siege, true);
	Stage = 8;
	return false;
}

bool FConstructionScenario::StageEight(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav || Nav->IsNavigationBuildInProgress())
		return false;
	Producers[2]->TickProduction(60.f);
	if (!Check(Alive(Producers[2].Get()) == 0 && Wallet->Resources == ReplacementBalance,
			TEXT("Physically blocked deployment never charges or emits a recruit")))
		return true;
	Blocker->Destroy();
	FCommandService::ConfigureProduction(Wallet, Producers[2].Get(), EUnitRole::Siege, false);
	Stage = 9;
	return false;
}

bool FConstructionScenario::StageNine(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav || Nav->IsNavigationBuildInProgress())
		return false;
	FVector Location;
	if (!FindPlacement(State, BarracksIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
		return Fail(TEXT("No legal footprint for a replacement producer"));
	NewProducer = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Location).Building;
	if (!NewProducer.IsValid())
		return Fail(TEXT("Replacement producer placement rejected"));
	NewProducer->Tick(60.f);
	Stage = 10;
	return false;
}

bool FConstructionScenario::StageTen(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav || Nav->IsNavigationBuildInProgress())
		return false;
	if (!Check(FCommandService::ConfigureProduction(Wallet, NewProducer.Get(), EUnitRole::Frontline, true).IsAccepted()
				&& IsValid(NewProducer->ForceGroup) && NewProducer->ForceGroup != Squad.Get()
				&& Alive(NewProducer.Get()) == 0 && Squad->GetUnits().Num() == SurvivorCount
				&& !IsValid(Squad->GetProductionBuilding()),
			TEXT("A new producer creates its own empty force instead of adopting orphan survivors")))
		return true;
	FCommandService::ConfigureProduction(Wallet, NewProducer.Get(), EUnitRole::Frontline, false);
	ReplacementBalance = Wallet->Resources;
	State->SetMatchResult(EMatchResult::Victory); // Terminal guard fixture, not outcome proof.
	const float Progress = Producers[2]->ProductionProgressSeconds;
	FCommandService::ConfigureProduction(Wallet, Producers[2].Get(), EUnitRole::Siege, true);
	FCommandService::IssueForceOrder(Wallet, Forces[1].Get(), EForceVerb::Retreat);
	Producers[2]->TickProduction(60.f);
	if (!Check(!Producers[2]->bProductionEnabled && Producers[2]->ProductionProgressSeconds == Progress
				&& Wallet->Resources == ReplacementBalance && Forces[1]->TargetRegionIndex == OtherRegion
				&& Squad->GetUnits().Num() == SurvivorCount && !IsValid(Squad->GetProductionBuilding()),
			TEXT("Terminal freezes production/force commands; orphan survivors receive no free refill or transfer")))
		return true;
	Test->AddInfo(TEXT("Fixed-force proof: one paid unit, permanent role, siege charge, 4/6/2 independent capacities, travel/arrival, casualty and traveller replacement, wipe identity, pause/starve/block/terminal and producer destruction."));
	return true;
}
}
#endif
