#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ConstructionScenario.h"

namespace ConstructionScenarioTests
{
bool FConstructionScenario::StageFour(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	Recruit = TravellingRecruit(Squad.Get());
	if (!Recruit.IsValid() || !Recruit->IsReinforcing())
		return false;
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
	if (!Check(Wallet->Resources == ReplacementBalance - Building->GetProductionDefinition()->UnitCost && UnrelatedWallet->Resources == 777
				&& Alive(Building.Get()) == Building->GetProductionDefinition()->Capacity
				&& Alive(Producers[1].Get()) == Producers[1]->GetProductionDefinition()->Capacity,
			TEXT("Replacement debits only its own commander, without taking another producer's capacity")))
		return true;
	RecruitStart = Recruit->GetActorLocation();
	JoinedStart = Squad->GetCenter();
	if (!Check(FVector::Dist2D(RecruitStart, Building->GetActorLocation()) < 1200.f
				&& FVector::Dist2D(RecruitStart, JoinedStart) > 500.f && JoinedCenterMatches(Squad.Get()),
			TEXT("Replacement leaves producer rather than spawning at force; center excludes travellers")))
		return true;
	// Retarget to clear home ground, not another full force's exact slots.
	// This isolates a moving rendezvous from cross-force capsule blockage.
	const int32 Region = ForceOrderGraph::TeamMain(*State, Squad->GetTeamIndex());
	if (Region == INDEX_NONE || !FCommandService::IssueForceOrder(Wallet, Squad.Get(), EForceVerb::MoveHold, Region))
		return Fail(TEXT("Replacement order needs a different reachable region"));
	Stage = 5;
	return false;
}

bool FConstructionScenario::StageFive(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	if (!Check(Recruit.IsValid() && Recruit->IsAlive() && JoinedCenterMatches(Squad.Get()),
			TEXT("Moving force center remains independent of travelling recruit")))
		return true;
	bRecruitMoved |= FVector::Dist2D(RecruitStart, Recruit->GetActorLocation()) > 200.f;
	bJoinedMoved |= FVector::Dist2D(JoinedStart, Squad->GetCenter()) > 200.f;
	if (Recruit->IsReinforcing() || !bRecruitMoved || !bJoinedMoved)
		return false;
	if (!Check(Squad->GetUnits().Contains(Recruit.Get())
				&& FVector::Dist2D(Recruit->GetActorLocation(), Squad->GetCenter()) < 500.f
				&& Forces[1]->TargetRegionIndex == OtherRegion && Forces[1]->Verb == OtherVerb,
			TEXT("Recruit follows moving force to physical arrival without altering the other force")))
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
	Recruit = TravellingRecruit(Squad.Get());
	if (!Check(Recruit.IsValid() && Recruit->IsReinforcing() && Building->ForceGroup == Squad.Get()
				&& Squad->GetJoinedCount() == 0 && Squad->TargetRegionIndex == RememberedRegion
				&& Wallet->Resources == ReplacementBalance - Building->GetProductionDefinition()->UnitCost,
			TEXT("Wiped force refills its remembered region under the original identity")))
		return true;
	Recruit->ReceiveAttack(Recruit->GetHealth(), Attacker->GetUnits()[0]);
	if (!Check(!Recruit->IsAlive() && Alive(Building.Get()) == 0,
			TEXT("Killing a travelling recruit reopens its paid vacancy")))
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
	if (!Check(Wallet->Resources == ReplacementBalance - Building->GetProductionDefinition()->UnitCost, TEXT("Dead traveller replacement charges again")))
		return true;
	if (Squad->GetJoinedCount() == 0)
		return false;
	if (!Check(Squad->GetJoinedCount() == 1 && !Squad->GetUnits()[0]->IsReinforcing(),
			TEXT("Paid dead-traveller replacement physically joins before producer destruction")))
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
