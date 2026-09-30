#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandGameMode.h"
#include "Content/BuildingDefinition.h"
#include "Headquarters.h"
#include "NavigationSystem.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructionLifecycleTest, "CoopRTS.Construction.Lifecycle",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructionProductionTest, "CoopRTS.Construction.Production",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Named, not anonymous: a using-directive inside an anonymous namespace leaks into
// every later file of a unity translation unit.
namespace ConstructionScenarioTests
{
using namespace ArmyTestSetup;
bool FindPlacement(ACommandGameState* State, int32 BuildingIndex, const FVector& Center, FVector& Result)
{
	for (int32 Ring = 0; Ring < 5; ++Ring)
		for (int32 Direction = 0; Direction < 16; ++Direction)
		{
			const float Angle = Direction * PI / 8.f;
			FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 110.f);
			Point.Z = 5.f;
			FString Reason;
			if (State->ValidateBuildingPlacement(BuildingIndex, 0, Point, Reason)) { Result = Point; return true; }
		}
	return false;
}
class FConstructionScenario : public IAutomationLatentCommand
{
public:
	FConstructionScenario(FAutomationTestBase* InTest, bool bInProduction) : Test(InTest), bProduction(bInProduction) {}
	bool Update() override
	{
		UWorld* World = ArmyTestSetup::World();
		if (!World || World->GetTimeSeconds() < 3.f) return false;
		ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		ACommandPlayerState* Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!PC || !State || !Wallet || Wallet->CommanderIndex < 0 || !MapReady(State) || !State->Content) return false;
		if (Stage == 0)
		{
			for (TActorIterator<AEnemyCommander> It(World); It; ++It) It->Destroy();
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				if (It->TeamIndex == 5) It->Destroy();
			State->bVerificationIncomePaused = true;
			Wallet->Resources = 4000; // Budget fixture; configuration and every recruit use real paid authority paths.
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				if (It->TeamIndex == 0) return Fail(TEXT("Normal new match must not spawn fixed friendly armies"));
			const UBuildingDefinition* Barracks = State->Content->Building(BarracksIndex);
			if (!Barracks) return Fail(TEXT("Match content lacks the barracks definition"));
			FVector Location;
			if (!FindPlacement(State, BarracksIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
				return Fail(TEXT("No valid barracks footprint in HQ construction territory"));
			const int32 Before = Wallet->Resources;
			FString PreviewReason;
			if (!Check(PC->CanPlaceBuildingAt(BarracksIndex, Location, PreviewReason),
				TEXT("Construction preview permits a valid affordable footprint regardless of explanatory text"))) return true;
			Wallet->Resources = Barracks->BuildCost - 1;
			if (!Check(!PC->CanPlaceBuildingAt(BarracksIndex, Location, PreviewReason),
				TEXT("Construction preview rejects a valid footprint when one resource short"))) return true;
			Wallet->Resources = Before;
			PC->ServerPlaceBuilding(BarracksIndex, Location);
			for (ACommandBuilding* Candidate : State->Buildings)
				if (IsValid(Candidate) && Candidate->OwningPlayerState == Wallet && Candidate->Kind == EBuildingKind::Barracks) Building = Candidate;
			if (!Check(Building.IsValid() && Wallet->Resources == Before - Barracks->BuildCost,
				TEXT("Owned placement creates one paid barracks"))) return true;
			if (!Check(!Building->IsComplete(), TEXT("Placement begins construction instead of instantly completing"))) return true;
			PC->ServerConfigureProduction(Building.Get(), EUnitRole::Frontline, true);
			if (!Check(!Building->bProductionEnabled, TEXT("Unfinished production rejects activation"))) return true;
			const int32 After = Wallet->Resources;
			PC->ServerPlaceBuilding(BarracksIndex, Location);
			PC->ServerPlaceBuilding(BarracksIndex, OutsideArena(State));
			PC->ServerPlaceBuilding(255, Location + FVector(400.f, 0.f, 0.f)); // No such definition index.
			if (!Check(Wallet->Resources == After, TEXT("Overlap, outside territory and invalid definition cannot debit wallet"))) return true;
			Building->Tick(60.f);
			if (!Check(Building->IsComplete() && Building->Health > 0, TEXT("Game-time construction completes a living building"))) return true;
			if (!bProduction) return Lifecycle(World, PC, State, Wallet);
			Stage = 1;
			return false; // Let the real dynamic navmesh incorporate the new blocker before deployment.
		}
		if (Stage < 8 && !Building.IsValid()) return Fail(TEXT("Production building disappeared"));
		if (Stage == 1)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav || Nav->IsNavigationBuildInProgress()) return false;
			FString Reason;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				FVector Location;
				if (!FindPlacement(State, BarracksIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
					return Fail(TEXT("No footprint for independent force producer"));
				ACommandBuilding* Added = State->TryPlaceBuilding(BarracksIndex, Location, Wallet, 0, Reason);
				if (!Added) return Fail(TEXT("Independent producer placement rejected"));
				Added->Tick(60.f);
				Producers.Add(Added);
			}
			Producers.Insert(Building.Get(), 0);
			UnrelatedWallet = World->SpawnActor<ACommandPlayerState>();
			if (!UnrelatedWallet.IsValid()) return Fail(TEXT("Independent commander wallet fixture could not spawn"));
			UnrelatedWallet->CommanderIndex = 1;
			UnrelatedWallet->Resources = 777;
			State->AddPlayerState(UnrelatedWallet.Get());
			Attacker = SpawnGroup(World, nullptr, -1, HostileStaging(State));
			if (!Attacker.IsValid()) return Fail(TEXT("Hostile damage fixture failed"));
			Attacker->IssueHold();
			Attacker->SetActorTickEnabled(false);
			for (AArmyUnit* Unit : Attacker->Units) Unit->SetActorTickEnabled(false);
			Stage = 2;
			return false;
		}
		if (Stage == 2)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav || Nav->IsNavigationBuildInProgress()) return false;
			const EUnitRole Roles[] = {EUnitRole::Ranged, EUnitRole::Frontline, EUnitRole::Siege};
			for (int32 Index = 0; Index < Producers.Num(); ++Index)
			{
				ACommandBuilding* Producer = Producers[Index].Get();
				const int32 Before = Wallet->Resources;
				PC->ServerConfigureProduction(Producer, static_cast<EUnitRole>(255), true);
				if (!Check(!Producer->bForceConfigured && !Producer->bProductionEnabled
					&& !IsValid(Producer->ForceGroup) && Wallet->Resources == Before,
					TEXT("Invalid first Start rejects atomically without allocating a force or charging"))) return true;
				if (Roles[Index] == EUnitRole::Siege)
				{
					Wallet->Resources = 179;
					PC->ServerConfigureProduction(Producer, EUnitRole::Siege, true);
					if (!Check(!Producer->bForceConfigured && Wallet->Resources == 179,
						TEXT("Unaffordable siege configuration does not lock type or debit"))) return true;
					Wallet->Resources = Before;
				}
				PC->ServerConfigureProduction(Producer, Roles[Index], false);
				if (!Check(!Producer->bForceConfigured, TEXT("Choosing a type before Start does not lock it"))) return true;
				PC->ServerConfigureProduction(Producer, Roles[Index], true);
				if (!Check(Producer->bForceConfigured && IsValid(Producer->ForceGroup) && Alive(Producer) == 0
					&& Wallet->Resources == Before - (Roles[Index] == EUnitRole::Siege ? 180 : 0),
					TEXT("First Start creates a permanent typed force and charges siege configuration exactly 180"))) return true;
				const float ExpectedDuration = Roles[Index] == EUnitRole::Frontline ? 10.f / 3.f
					: Roles[Index] == EUnitRole::Ranged ? 13.f / 3.f : 20.f / 3.f;
				const int32 BeforeWork = Wallet->Resources;
				Producer->TickProduction(ExpectedDuration - .25f);
				if (!Check(Alive(Producer) == 0 && Wallet->Resources == BeforeWork,
					TEXT("Work short of one unit duration cannot spawn or debit, for any force type"))) return true;
				PC->ServerConfigureProduction(Producer, Roles[Index], false);
				const int32 ConfiguredBalance = Wallet->Resources;
				const float Progress = Producer->ProductionProgressSeconds;
				AArmyGroup* Identity = Producer->ForceGroup;
				PC->ServerConfigureProduction(Producer, static_cast<EUnitRole>(255), true);
				PC->ServerConfigureProduction(Producer, Roles[(Index + 1) % 3], true);
				if (!Check(Producer->ProductionRole == Roles[Index] && !Producer->bProductionEnabled
					&& Producer->ForceGroup == Identity && Producer->ProductionProgressSeconds == Progress
					&& Wallet->Resources == ConfiguredBalance,
					TEXT("Invalid and changed roles after Start reject atomically even while paused"))) return true;
				PC->ServerConfigureProduction(Producer, Roles[Index], true);
				if (!Check(Wallet->Resources == ConfiguredBalance, TEXT("Resume never repeats configuration charge"))) return true;
				PC->ServerConfigureProduction(Producer, Roles[Index], false);
				Forces.Add(Identity);
			}
			if (!Check(Forces[0] != Forces[1] && Forces[1] != Forces[2] && Forces[0] != Forces[2],
				TEXT("Every producer owns a distinct stable force"))) return true;
			Wallet->Resources = 0;
			Building->SetProduction(UnitIndex(State, EUnitRole::Ranged), true);
			const float StarvedProgress = Building->ProductionProgressSeconds;
			Building->TickProduction(60.f);
			if (!Check(Alive(Building.Get()) == 0 && Building->ProductionProgressSeconds == StarvedProgress
				&& Wallet->Resources == 0, TEXT("Starvation cannot emit recruits or advance paid work"))) return true;
			Wallet->Resources = 2000;
			Building->TickProduction(Building->GetProductionDuration());
			Building->SetProduction(UnitIndex(State, EUnitRole::Ranged), false);
			Squad = Building->ForceGroup;
			if (!Check(Alive(Building.Get()) == 1 && Wallet->Resources == 1970,
				TEXT("One completed ranged slot produces one unit, not a batch, for exactly 30"))) return true;
			Recruit = Squad->Units[0];
			if (!Check(Recruit->UnitRole == EUnitRole::Ranged && Recruit->CommanderIndex == Wallet->CommanderIndex
				&& Recruit->Group == Squad.Get() && FVector::Dist2D(Recruit->GetActorLocation(), Building->GetActorLocation())
					> State->Content->Building(BarracksIndex)->FootprintRadius,
				TEXT("Paid recruit physically starts outside its owning producer"))) return true;
			Building->SetProduction(UnitIndex(State, EUnitRole::Ranged), true);
			Building->TickProduction(Building->GetProductionDuration() * .25f);
			Building->SetProduction(UnitIndex(State, EUnitRole::Ranged), false);
			const float PausedProgress = Building->ProductionProgressSeconds;
			const int32 PausedBalance = Wallet->Resources;
			Building->TickProduction(60.f);
			if (!Check(PausedProgress > 0.f && Building->ProductionProgressSeconds == PausedProgress
				&& Wallet->Resources == PausedBalance && Alive(Building.Get()) == 1,
				TEXT("Pause preserves partially completed unit work and wallet"))) return true;
			FillBalance = Wallet->Resources;
			for (int32 Index = 0; Index < Producers.Num(); ++Index)
			{
				ACommandBuilding* Producer = Producers[Index].Get();
				// Fronts are HQ-relative: the first force east-south of the HQ, the others
				// further north, out of the recruit's later travel corridor; this scenario
				// proves ownership/refill, not crowd-grid escape.
				const FVector Front = Index == 0 ? FromFriendlyHQ(State, 1700.f, -1100.f, 5.f)
					: FromFriendlyHQ(State, 1000.f, 1900.f + Index * 400.f, 5.f);
				if (!Producer->SetFront(EFrontOrder::Defend, Front))
					return Fail(TEXT("Independent force front rejected"));
				Producer->SetProduction(Producer->ProductionUnitIndex, true);
			}
			Stage = 3;
			return false;
		}
		if (Stage == 3)
		{
			const int32 Capacities[] = {4, 6, 2};
			const int32 Costs[] = {30, 20, 50};
			int32 ExpectedDebit = 0;
			bool bFull = true;
			for (int32 Index = 0; Index < Producers.Num(); ++Index)
			{
				int32 Joined, Travelling;
				Producers[Index]->GetForceCounts(Joined, Travelling);
				if (!Check(Joined + Travelling <= Capacities[Index], TEXT("Alive travellers consume their producer capacity"))) return true;
				if (!Check(ValidMembers(Producers[Index].Get(), Capacities[Index]), TEXT("Force roles, ownership and unique slots remain valid"))) return true;
				ExpectedDebit += (Joined + Travelling - (Index == 0 ? 1 : 0)) * Costs[Index];
				bFull &= Joined == Capacities[Index] && Travelling == 0;
			}
			if (!Check(Wallet->Resources == FillBalance - ExpectedDebit, TEXT("Each produced unit charges only its own role price"))) return true;
			if (!bFull) return false;
			for (const auto& Producer : Producers)
			{
				const float Progress = Producer->ProductionProgressSeconds;
				Producer->TickProduction(60.f);
				if (!Check(Producer->ProductionProgressSeconds == Progress && Producer->GetProductionState() == EProductionState::ForceComplete,
					TEXT("Enabled full forces stop work and report automatic capacity waiting"))) return true;
				Producer->SetProduction(Producer->ProductionUnitIndex, false);
				if (!Check(Producer->GetProductionState() == EProductionState::Paused,
					TEXT("Explicit pause takes presentation priority even on a full force"))) return true;
			}
			if (!Check(Wallet->Resources == FillBalance - 3 * 30 - 6 * 20 - 2 * 50,
				TEXT("Twelve living units fill independent 4/6/2 forces without a shared cap"))) return true;
			OtherFront = Forces[1]->FrontLocation;
			OtherOrder = Forces[1]->FrontOrder;
			PC->ServerAssignFront(Building.Get(), EFrontOrder::Secure, FromFriendlyHQ(State, 1700.f, -1100.f, 5.f));
			if (!Check(Forces[1]->FrontLocation == OtherFront && Forces[1]->FrontOrder == OtherOrder,
				TEXT("An owning commander's front change affects only the selected producer"))) return true;
			AArmyUnit* Victim = Squad->Units[0];
			Victim->ReceiveAttack(Victim->Health, Attacker->Units[0]);
			if (!Check(!Victim->IsAlive() && Alive(Building.Get()) == 3, TEXT("Real lethal damage opens exactly one vacancy"))) return true;
			ReplacementBalance = Wallet->Resources;
			Building->SetProduction(UnitIndex(State, EUnitRole::Ranged), true);
			Stage = 4;
			return false;
		}
		if (Stage == 4)
		{
			for (AArmyUnit* Unit : Squad->Units)
				if (IsValid(Unit) && Unit->IsAlive() && Unit->bReinforcing) Recruit = Unit;
			if (!Recruit.IsValid() || !Recruit->bReinforcing) return false;
			Building->SetProduction(UnitIndex(State, EUnitRole::Ranged), false);
			if (!Check(Wallet->Resources == ReplacementBalance - 30 && UnrelatedWallet->Resources == 777
				&& Alive(Building.Get()) == 4 && Alive(Producers[1].Get()) == 6,
				TEXT("Replacement debits only its own commander, without taking another producer's capacity"))) return true;
			RecruitStart = Recruit->GetActorLocation();
			JoinedStart = Squad->GetCenter();
			if (!Check(FVector::Dist2D(RecruitStart, Building->GetActorLocation()) < 1200.f
				&& FVector::Dist2D(RecruitStart, JoinedStart) > 500.f && JoinedCenterMatches(Squad.Get()),
				TEXT("Replacement leaves producer rather than spawning at force; center excludes travellers"))) return true;
			PC->ServerAssignFront(Building.Get(), EFrontOrder::Defend, FromFriendlyHQ(State, 1700.f, 3100.f, 5.f));
			Stage = 5;
			return false;
		}
		if (Stage == 5)
		{
			if (!Check(Recruit.IsValid() && Recruit->IsAlive() && JoinedCenterMatches(Squad.Get()),
				TEXT("Moving force center remains independent of travelling recruit"))) return true;
			bRecruitMoved |= FVector::Dist2D(RecruitStart, Recruit->GetActorLocation()) > 200.f;
			bJoinedMoved |= FVector::Dist2D(JoinedStart, Squad->GetCenter()) > 200.f;
			if (Recruit->bReinforcing || !bRecruitMoved || !bJoinedMoved) return false;
			if (!Check(FVector::Dist2D(Recruit->GetActorLocation(), Squad->GetCenter()) < 500.f
				&& Forces[1]->FrontLocation == OtherFront && Forces[1]->FrontOrder == OtherOrder,
				TEXT("Recruit follows moving force to physical arrival without altering the other front"))) return true;
			while (!Squad->Units.IsEmpty())
			{
				AArmyUnit* Unit = Squad->Units.Last();
				Unit->ReceiveAttack(Unit->Health, Attacker->Units[0]);
				if (!Check(!Unit->IsAlive() && !Squad->Units.Contains(Unit), TEXT("Each lethal hit removes its force member"))) return true;
			}
			if (!Check(IsValid(Building->ForceGroup) && Building->ForceGroup == Squad.Get() && Alive(Building.Get()) == 0,
				TEXT("Complete wipe retains the same empty force identity"))) return true;
			RememberedFront = Building->FrontLocation;
			ReplacementBalance = Wallet->Resources;
			Building->SetProduction(UnitIndex(State, EUnitRole::Ranged), true);
			Stage = 6;
			return false;
		}
		if (Stage == 6)
		{
			if (Alive(Building.Get()) == 0) return false;
			Building->SetProduction(UnitIndex(State, EUnitRole::Ranged), false);
			Recruit = Squad->Units[0];
			if (!Check(Recruit->bReinforcing && Building->ForceGroup == Squad.Get()
				&& Squad->FrontLocation == RememberedFront && Wallet->Resources == ReplacementBalance - 30,
				TEXT("Wiped force refills its remembered front under the original identity"))) return true;
			Recruit->ReceiveAttack(Recruit->Health, Attacker->Units[0]);
			if (!Check(!Recruit->IsAlive() && Alive(Building.Get()) == 0,
				TEXT("Killing a travelling recruit reopens its paid vacancy"))) return true;
			ReplacementBalance = Wallet->Resources;
			Building->SetProduction(UnitIndex(State, EUnitRole::Ranged), true);
			Stage = 7;
			return false;
		}
		if (Stage == 7)
		{
			if (Alive(Building.Get()) == 0) return false;
			Building->SetProduction(UnitIndex(State, EUnitRole::Ranged), false);
			if (!Check(Wallet->Resources == ReplacementBalance - 30, TEXT("Dead traveller replacement charges again"))) return true;
			Building->ReceiveAttack(Building->Health, Attacker->Units[0]);
			if (!Check(Squad.IsValid() && !IsValid(Squad->ProductionBuilding) && Squad->FrontLocation == RememberedFront,
				TEXT("Destroyed producer leaves survivors on their last front with no producer transfer"))) return true;
			SurvivorCount = Squad->Units.Num();
			ReplacementBalance = Wallet->Resources;
			Blocker = World->SpawnActor<AActor>();
			if (!Blocker.IsValid()) return Fail(TEXT("Deployment obstruction fixture could not spawn"));
			UBoxComponent* Box = NewObject<UBoxComponent>(Blocker.Get());
			Blocker->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(1400.f, 1400.f, 300.f));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Blocker->SetActorLocation(Producers[2]->GetActorLocation());
			while (!Forces[2]->Units.IsEmpty())
			{
				AArmyUnit* Unit = Forces[2]->Units.Last();
				Unit->ReceiveAttack(Unit->Health, Attacker->Units[0]);
				if (!Check(!Unit->IsAlive() && !Forces[2]->Units.Contains(Unit), TEXT("Blocked-producer casualty is real lethal damage"))) return true;
			}
			Producers[2]->SetProduction(UnitIndex(State, EUnitRole::Siege), true);
			Stage = 8;
			return false;
		}
		if (Stage == 8)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav || Nav->IsNavigationBuildInProgress()) return false;
			Producers[2]->TickProduction(60.f);
			if (!Check(Alive(Producers[2].Get()) == 0 && Wallet->Resources == ReplacementBalance,
				TEXT("Physically blocked deployment never charges or emits a recruit"))) return true;
			Blocker->Destroy();
			Producers[2]->SetProduction(UnitIndex(State, EUnitRole::Siege), false);
			Stage = 9;
			return false;
		}
		if (Stage == 9)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav || Nav->IsNavigationBuildInProgress()) return false;
			FVector Location;
			if (!FindPlacement(State, BarracksIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
				return Fail(TEXT("No legal footprint for a replacement producer"));
			FString Reason;
			NewProducer = State->TryPlaceBuilding(BarracksIndex, Location, Wallet, 0, Reason);
			if (!NewProducer.IsValid()) return Fail(TEXT("Replacement producer placement rejected"));
			NewProducer->Tick(60.f);
			Stage = 10;
			return false;
		}
		if (Stage == 10)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav || Nav->IsNavigationBuildInProgress()) return false;
			if (!Check(NewProducer->SetProduction(UnitIndex(State, EUnitRole::Frontline), true)
				&& IsValid(NewProducer->ForceGroup) && NewProducer->ForceGroup != Squad.Get()
				&& Alive(NewProducer.Get()) == 0 && Squad->Units.Num() == SurvivorCount
				&& !IsValid(Squad->ProductionBuilding),
				TEXT("A new producer creates its own empty force instead of adopting orphan survivors"))) return true;
			NewProducer->SetProduction(UnitIndex(State, EUnitRole::Frontline), false);
			ReplacementBalance = Wallet->Resources;
			State->MatchResult = EMatchResult::Victory; // Terminal guard fixture, not outcome proof.
			const float Progress = Producers[2]->ProductionProgressSeconds;
			PC->ServerConfigureProduction(Producers[2].Get(), EUnitRole::Siege, true);
			PC->ServerAssignFront(Producers[1].Get(), EFrontOrder::FallBack, FromFriendlyHQ(State, 1700.f, 600.f, 5.f));
			Producers[2]->TickProduction(60.f);
			if (!Check(!Producers[2]->bProductionEnabled && Producers[2]->ProductionProgressSeconds == Progress
				&& Wallet->Resources == ReplacementBalance && Forces[1]->FrontLocation == OtherFront
				&& Squad->Units.Num() == SurvivorCount && !IsValid(Squad->ProductionBuilding),
				TEXT("Terminal freezes production/front commands; orphan survivors receive no free refill or transfer"))) return true;
			Test->AddInfo(TEXT("Fixed-force proof: one paid unit, permanent role, siege charge, 4/6/2 independent capacities, travel/arrival, casualty and traveller replacement, wipe identity, pause/starve/block/terminal and producer destruction."));
			return true;
		}
		return false;
	}
private:
	bool Check(bool Value, const TCHAR* Message) { if (!Value) Test->AddError(Message); return Value; }
	bool Fail(const TCHAR* Message) { Test->AddError(Message); return true; }
	static int32 Alive(const ACommandBuilding* Producer)
	{
		int32 Joined, Travelling;
		Producer->GetForceCounts(Joined, Travelling);
		return Joined + Travelling;
	}
	static bool JoinedCenterMatches(const AArmyGroup* Force)
	{
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const AArmyUnit* Unit : Force->Units)
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->bReinforcing) { Sum += Unit->GetActorLocation(); ++Count; }
		return Count == 0 || FVector::Dist2D(Sum / Count, Force->GetCenter()) < 1.f;
	}
	static bool ValidMembers(const ACommandBuilding* Producer, int32 Capacity)
	{
		uint32 Slots = 0;
		for (const AArmyUnit* Unit : Producer->ForceGroup->Units)
		{
			if (!IsValid(Unit) || !Unit->IsAlive()) continue;
			if (Unit->UnitRole != Producer->ProductionRole || Unit->Group != Producer->ForceGroup
				|| Unit->CommanderIndex != Producer->OwningPlayerState->CommanderIndex
				|| Unit->CompositionSlot < 0 || Unit->CompositionSlot >= Capacity
				|| (Slots & (1u << Unit->CompositionSlot))) return false;
			Slots |= 1u << Unit->CompositionSlot;
		}
		return true;
	}
	bool Lifecycle(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
	{
		ACommandPlayerController* Other = World->SpawnActor<ACommandPlayerController>();
		ACommandPlayerState* OtherWallet = World->SpawnActor<ACommandPlayerState>();
		if (!Other || !OtherWallet) return Fail(TEXT("Other owner fixture could not spawn"));
		Other->SetPlayerState(OtherWallet);
		OtherWallet->CommanderIndex = 1;
		OtherWallet->Resources = 777;
		State->AddPlayerState(OtherWallet);
		const int32 Before = Wallet->Resources;
		Other->ServerConfigureProduction(Building.Get(), EUnitRole::Ranged, true);
		Other->ServerCancelBuilding(Building.Get());
		Other->ServerAssignFront(Building.Get(), EFrontOrder::FallBack, FromFriendlyHQ(State, 1700.f, 600.f, 5.f));
		if (!Check(Building.IsValid() && !Building->bForceConfigured && !Building->bProductionEnabled
			&& !Building->HasConfiguredFront() && Wallet->Resources == Before && OtherWallet->Resources == 777,
			TEXT("Same-team foreign role/front/cancel commands change neither building nor either wallet"))) return true;
		PC->ServerConfigureProduction(Building.Get(), EUnitRole::Ranged, true);
		PC->ServerConfigureProduction(Building.Get(), EUnitRole::Ranged, false);
		AArmyGroup* ConfiguredForce = Building->ForceGroup;
		PC->ServerConfigureProduction(Building.Get(), EUnitRole::Siege, true);
		if (!Check(Building->bForceConfigured && Building->ProductionRole == EUnitRole::Ranged
			&& !Building->bProductionEnabled && Building->ForceGroup == ConfiguredForce && Wallet->Resources == Before,
			TEXT("Lifecycle retains first-Start configuration and rejects paused type changes without an upgrade path"))) return true;
		FVector Location;
		if (!FindPlacement(State, WorkshopIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
			return Fail(TEXT("No workshop footprint in HQ territory"));
		FString Reason;
		ACommandBuilding* Workshop = State->TryPlaceBuilding(WorkshopIndex, Location, Wallet, 0, Reason);
		if (!Check(Workshop != nullptr, TEXT("Workshop construction accepted"))) return true;
		Workshop->Tick(60.f);
		const int32 ResearchBalance = Wallet->Resources;
		PC->ServerResearch(Workshop, EArmyDoctrine::SiegeOptics);
		PC->ServerResearch(Workshop, EArmyDoctrine::FieldRepairs);
		if (!Check(Wallet->Doctrine == EArmyDoctrine::SiegeOptics && Wallet->Resources == ResearchBalance - ACommandBuilding::ResearchCost
			&& OtherWallet->Doctrine == EArmyDoctrine::None, TEXT("Research is paid exactly once and scoped to owning commander"))) return true;
		if (!FindPlacement(State, BarracksIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
			return Fail(TEXT("No cancellation-test footprint"));
		const int32 CancelBalance = Wallet->Resources;
		ACommandBuilding* Cancelled = State->TryPlaceBuilding(BarracksIndex, Location, Wallet, 0, Reason);
		if (!Check(Cancelled && Cancelled->CancelConstruction() && Wallet->Resources == CancelBalance,
			TEXT("Immediate cancellation refunds unbuilt construction exactly once"))) return true;
		const int32 AfterCancel = Wallet->Resources;
		PC->ServerCancelBuilding(Cancelled);
		if (!Check(Wallet->Resources == AfterCancel, TEXT("Repeated cancellation cannot mint resources"))) return true;
		if (State->CaptureSites.IsEmpty()) return Fail(TEXT("No resource sector in map"));
		ACapturePoint* Site = State->CaptureSites[0];
		AArmyGroup* Occupiers = ArmyTestSetup::SpawnGroup(World, PC, 10, Site->GetActorLocation() + FVector(0.f, 0.f, 100.f));
		if (!Check(Occupiers != nullptr, TEXT("Real capture occupants spawn"))) return true;
		Site->AdvanceCapture(20.f);
		if (!Check(Site->ControllingTeam == 0 && !Site->IsEstablishedForTeam(0) && State->ControlledResourceSites == 0,
			TEXT("Occupation secures land but bare capture provides no outpost income"))) return true;
		if (!FindPlacement(State, OutpostIndex, Site->GetActorLocation(), Location)) return Fail(TEXT("No valid secured-sector outpost placement"));
		ACommandBuilding* Outpost = State->TryPlaceBuilding(OutpostIndex, Location, Wallet, 0, Reason);
		if (!Check(Outpost != nullptr, TEXT("Secured territory accepts outpost"))) return true;
		Outpost->Tick(60.f);
		for (AArmyUnit* Unit : Occupiers->Units) Unit->SetActorLocation(FromFriendlyHQ(State, 1700.f, 600.f, 100.f));
		Site->AdvanceCapture(20.f);
		State->RefreshTerritory();
		if (!Check(Site->IsEstablishedForTeam(0) && State->ControlledResourceSites == 1 && !Site->bFriendlyPresent,
			TEXT("Completed outpost retains construction/income without occupying troops"))) return true;
		const int32 IncomeBefore = Wallet->Resources;
		State->bVerificationIncomePaused = false;
		State->Tick(2.f);
		State->bVerificationIncomePaused = true;
		if (!Check(Wallet->Resources == IncomeBefore + State->GetIncomePerSecond() * 2,
			TEXT("Established outpost pays territory income through normal GameState economy"))) return true;
		AArmyGroup* Enemy = SpawnGroup(World, nullptr, -1, HostileStaging(State));
		if (!Enemy) return Fail(TEXT("Hostile destruction fixture failed"));
		Outpost->ReceiveAttack(Outpost->Health, Enemy->Units[0]);
		State->RefreshTerritory();
		if (!Check(!Site->IsEstablishedForTeam(0) && State->ControlledResourceSites == 0 && Building.IsValid()
			&& Building->OwningPlayerState == Wallet, TEXT("Destroyed outpost removes rights/income without converting surviving buildings"))) return true;
		Test->AddInfo(TEXT("Construction proof: placement/payment/rejections, construction, ownership, research, cancellation, real capture, persistent outpost income and destruction."));
		return true;
	}
	FAutomationTestBase* Test;
	bool bProduction;
	int32 Stage = 0;
	TWeakObjectPtr<ACommandBuilding> Building;
	TWeakObjectPtr<AArmyGroup> Squad;
	TArray<TWeakObjectPtr<ACommandBuilding>> Producers;
	TWeakObjectPtr<ACommandBuilding> NewProducer;
	TArray<TWeakObjectPtr<AArmyGroup>> Forces;
	TWeakObjectPtr<AArmyGroup> Attacker;
	TWeakObjectPtr<AArmyUnit> Recruit;
	TWeakObjectPtr<AActor> Blocker;
	TWeakObjectPtr<ACommandPlayerState> UnrelatedWallet;
	FVector RecruitStart, JoinedStart, OtherFront, RememberedFront;
	EFrontOrder OtherOrder = EFrontOrder::Defend;
	int32 FillBalance = 0, ReplacementBalance = 0, SurvivorCount = 0;
	bool bRecruitMoved = false, bJoinedMoved = false;
};
}
bool FConstructionLifecycleTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(ConstructionScenarioTests::FConstructionScenario(this, false)); return true; }
bool FConstructionProductionTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(ConstructionScenarioTests::FConstructionScenario(this, true)); return true; }
#endif
