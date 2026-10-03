#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ConstructionScenario.h"

namespace ConstructionScenarioTests
{
bool FConstructionScenario::StageOne(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav || Nav->IsNavigationBuildInProgress())
		return false;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FVector Location;
		if (!FindPlacement(State, BarracksIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
			return Fail(TEXT("No footprint for independent force producer"));
		ACommandBuilding* Added = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Location).Building;
		if (!Added)
			return Fail(TEXT("Independent producer placement rejected"));
		Added->Tick(60.f);
		Producers.Add(Added);
	}
	Producers.Insert(Building.Get(), 0);
	UnrelatedWallet = World->SpawnActor<ACommandPlayerState>();
	if (!UnrelatedWallet.IsValid())
		return Fail(TEXT("Independent commander wallet fixture could not spawn"));
	UnrelatedWallet->CommanderIndex = 1;
	UnrelatedWallet->Resources = 777;
	State->AddPlayerState(UnrelatedWallet.Get());
	Attacker = SpawnGroup(World, nullptr, -1, FromEnemyHQ(State, 0.f, 0.f, 100.f));
	if (!Attacker.IsValid())
		return Fail(TEXT("Hostile damage fixture failed"));
	if (!FCommandService::IssueForceOrder(State->EnemyCommander, Attacker.Get(), EForceVerb::MoveHold, ForceOrderGraph::TeamMain(*State, 5)))
		return Fail(TEXT("Hostile damage fixture must remain inside its own HQ region"));
	Attacker->SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Attacker->GetUnits())
		Unit->SetActorTickEnabled(false);
	Stage = 2;
	return false;
}

bool FConstructionScenario::ConfigureProducers(ACommandPlayerState* Wallet)
{
	const EUnitRole Roles[] = { EUnitRole::Ranged, EUnitRole::Frontline, EUnitRole::Siege };
	for (int32 Index = 0; Index < Producers.Num(); ++Index)
	{
		ACommandBuilding* Producer = Producers[Index].Get();
		const int32 Before = Wallet->Resources;
		FCommandService::ConfigureProduction(Wallet, Producer, static_cast<EUnitRole>(255), true);
		if (!Check(!Producer->bForceConfigured && !Producer->bProductionEnabled
					&& !IsValid(Producer->ForceGroup) && Wallet->Resources == Before,
				TEXT("Invalid first Start rejects atomically without allocating a force or charging")))
			return true;
		if (Roles[Index] == EUnitRole::Siege)
		{
			Wallet->Resources = 179;
			FCommandService::ConfigureProduction(Wallet, Producer, EUnitRole::Siege, true);
			if (!Check(!Producer->bForceConfigured && Wallet->Resources == 179,
					TEXT("Unaffordable siege configuration does not lock type or debit")))
				return true;
			Wallet->Resources = Before;
		}
		FCommandService::ConfigureProduction(Wallet, Producer, Roles[Index], false);
		if (!Check(!Producer->bForceConfigured, TEXT("Choosing a type before Start does not lock it")))
			return true;
		FCommandService::ConfigureProduction(Wallet, Producer, Roles[Index], true);
		if (!Check(Producer->bForceConfigured && IsValid(Producer->ForceGroup) && Alive(Producer) == 0
					&& Wallet->Resources == Before - (Roles[Index] == EUnitRole::Siege ? 180 : 0),
				TEXT("First Start creates a permanent typed force and charges siege configuration exactly 180")))
			return true;
		const float Duration = Producer->GetProductionDefinition()->UnitDuration;
		const int32 BeforeWork = Wallet->Resources;
		Producer->TickProduction(Duration - .25f);
		if (!Check(Alive(Producer) == 0 && Wallet->Resources == BeforeWork,
				TEXT("Work short of one unit duration cannot spawn or debit, for any force type")))
			return true;
		FCommandService::ConfigureProduction(Wallet, Producer, Roles[Index], false);
		const int32 ConfiguredBalance = Wallet->Resources;
		const float Progress = Producer->ProductionProgressSeconds;
		AArmyGroup* Identity = Producer->ForceGroup;
		FCommandService::ConfigureProduction(Wallet, Producer, static_cast<EUnitRole>(255), true);
		FCommandService::ConfigureProduction(Wallet, Producer, Roles[(Index + 1) % 3], true);
		if (!Check(Producer->ProductionRole == Roles[Index] && !Producer->bProductionEnabled
					&& Producer->ForceGroup == Identity && Producer->ProductionProgressSeconds == Progress
					&& Wallet->Resources == ConfiguredBalance,
				TEXT("Invalid and changed roles after Start reject atomically even while paused")))
			return true;
		FCommandService::ConfigureProduction(Wallet, Producer, Roles[Index], true);
		if (!Check(Wallet->Resources == ConfiguredBalance, TEXT("Resume never repeats configuration charge")))
			return true;
		FCommandService::ConfigureProduction(Wallet, Producer, Roles[Index], false);
		Forces.Add(Identity);
	}
	return false;
}

bool FConstructionScenario::StageTwo(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav || Nav->IsNavigationBuildInProgress())
		return false;
	if (ConfigureProducers(Wallet))
		return true;
	if (!Check(Forces[0] != Forces[1] && Forces[1] != Forces[2] && Forces[0] != Forces[2],
			TEXT("Every producer owns a distinct stable force")))
		return true;
	Wallet->Resources = 0;
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, true);
	const float StarvedProgress = Building->ProductionProgressSeconds;
	Building->TickProduction(60.f);
	if (!Check(Alive(Building.Get()) == 0 && Building->ProductionProgressSeconds == StarvedProgress
				&& Wallet->Resources == 0,
			TEXT("Starvation cannot emit recruits or advance paid work")))
		return true;
	Wallet->Resources = 2000;
	Building->TickProduction(Building->GetProductionDuration());
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
	Squad = Building->ForceGroup;
	if (!Check(Alive(Building.Get()) == 1 && Squad.IsValid() && Squad->GetJoinedCount() == 0
				&& Wallet->Resources == 2000 - Building->GetProductionDefinition()->UnitCost,
			TEXT("One completed slot produces one travelling recruit and charges its definition's unit cost")))
		return true;
	Recruit = TravellingRecruit(Squad.Get());
	if (!Check(Recruit.IsValid() && Recruit->GetUnitRole() == EUnitRole::Ranged && Recruit->GetCommanderIndex() == Wallet->CommanderIndex
				&& Recruit->GetGroup() == Squad.Get() && FVector::Dist2D(Recruit->GetActorLocation(), Building->GetActorLocation()) > State->Content->Building(BarracksIndex)->FootprintRadius,
			TEXT("Paid recruit physically starts outside its owning producer")))
		return true;
	FirstRecruitBalance = Wallet->Resources;
	Stage = 11;
	return false;
}

bool FConstructionScenario::StageEleven(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	if (!Check(Recruit.IsValid() && Recruit->IsAlive(), TEXT("First paid recruit survives its real route to the force")))
		return true;
	if (Recruit->IsReinforcing())
		return false;
	if (!Check(Squad->GetUnits().Num() == 1 && Squad->GetUnits().Contains(Recruit.Get())
				&& Squad->GetJoinedCount() == 1 && Alive(Building.Get()) == 1 && Wallet->Resources == FirstRecruitBalance,
			TEXT("First recruit joins physically without another spawn or debit")))
		return true;
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, true);
	Building->TickProduction(Building->GetProductionDuration() * .25f);
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
	const float PausedProgress = Building->ProductionProgressSeconds;
	const int32 PausedBalance = Wallet->Resources;
	Building->TickProduction(60.f);
	if (!Check(PausedProgress > 0.f && Building->ProductionProgressSeconds == PausedProgress
				&& Wallet->Resources == PausedBalance && Alive(Building.Get()) == 1,
			TEXT("Pause preserves partially completed unit work and wallet")))
		return true;
	FillBalance = Wallet->Resources;
	TArray<int32, TInlineAllocator<4>> ReservedRegions;
	for (int32 Index = 0; Index < Producers.Num(); ++Index)
	{
		ACommandBuilding* Producer = Producers[Index].Get();
		const FVector Preferred = FromFriendlyHQ(State, 1700.f, Index * 1200.f, 5.f);
		const int32 Region = FindForceRegion(State, Producer->ForceGroup, Preferred, ReservedRegions);
		if (Region == INDEX_NONE || !FCommandService::IssueForceOrder(Wallet, Producer->ForceGroup, EForceVerb::MoveHold, Region))
			return Fail(TEXT("Independent producer needs its own reachable polygon anchor"));
		ReservedRegions.Add(Region);
		FCommandService::ConfigureProduction(Wallet, Producer, State->Content->Unit(Producer->ProductionUnitIndex)->Role, true);
	}
	Stage = 3;
	return false;
}

bool FConstructionScenario::StageThree(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	int32 ExpectedDebit = 0;
	bool bFull = true;
	const bool bReportProgress = World->GetTimeSeconds() - LastProgressReport >= 5.f;
	if (bReportProgress)
		LastProgressReport = World->GetTimeSeconds();
	for (int32 Index = 0; Index < Producers.Num(); ++Index)
	{
		const UArmyUnitDefinition* Definition = Producers[Index]->GetProductionDefinition();
		int32 Joined, Travelling;
		Producers[Index]->GetForceCounts(Joined, Travelling);
		if (!Check(Joined + Travelling <= Definition->Capacity, TEXT("Alive travellers consume their producer capacity")))
			return true;
		if (!Check(ValidMembers(Producers[Index].Get(), Definition->Capacity), TEXT("Force roles, ownership and unique slots remain valid")))
			return true;
		ExpectedDebit += (Joined + Travelling - (Index == 0 ? 1 : 0)) * Definition->UnitCost;
		bFull &= Joined == Definition->Capacity && Travelling == 0;
		if (bReportProgress)
			UE_LOG(LogTemp, Display, TEXT("Production fixture filling force=%d joined=%d travelling=%d state=%d front=%s"),
				Index, Joined, Travelling, static_cast<int32>(Producers[Index]->GetProductionState()),
				*Producers[Index]->ForceGroup->Destination.ToString());
	}
	if (!Check(Wallet->Resources == FillBalance - ExpectedDebit, TEXT("Each produced unit charges only its own role price")))
		return true;
	if (!bFull)
		return false;
	return CompleteForces(World, PC, State, Wallet);
}

bool FConstructionScenario::CompleteForces(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	for (const auto& Producer : Producers)
	{
		const float Progress = Producer->ProductionProgressSeconds;
		Producer->TickProduction(60.f);
		if (!Check(Producer->ProductionProgressSeconds == Progress && Producer->GetProductionState() == EProductionState::ForceComplete,
				TEXT("Enabled full forces stop work and report automatic capacity waiting")))
			return true;
		FCommandService::ConfigureProduction(Wallet, Producer.Get(), State->Content->Unit(Producer->ProductionUnitIndex)->Role, false);
		if (!Check(Producer->GetProductionState() == EProductionState::Paused,
				TEXT("Explicit pause takes presentation priority even on a full force")))
			return true;
	}
	OtherRegion = Forces[1]->TargetRegionIndex;
	OtherVerb = Forces[1]->Verb;
	if (!FCommandService::IssueForceOrder(Wallet, Squad.Get(), EForceVerb::Attack, Squad->TargetRegionIndex))
		return Fail(TEXT("Owned force can replace its held region with an Attack"));
	if (!Check(Forces[1]->TargetRegionIndex == OtherRegion && Forces[1]->Verb == OtherVerb,
			TEXT("An owning commander's force order affects only the selected force")))
		return true;
	AArmyUnit* Victim = Squad->GetUnits()[0];
	Victim->ReceiveAttack(Victim->GetHealth(), Attacker->GetUnits()[0]);
	if (!Check(!Victim->IsAlive() && Alive(Building.Get()) == Building->GetProductionDefinition()->Capacity - 1,
			TEXT("Real lethal damage opens exactly one vacancy")))
		return true;
	ReplacementBalance = Wallet->Resources;
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, true);
	Stage = 4;
	return false;
}
}
#endif
