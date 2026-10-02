#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "Content/BuildingDefinition.h"
#include "NavigationSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructionForceIdentityTest, "CoopRTS.Construction.ForceIdentity",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace ForceIdentityScenarioTests
{
using namespace ArmyTestSetup;
bool FindPlacement(ACommandGameState* State, int32 BuildingIndex, int32 Team, const FVector& Center, FVector& Result)
{
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
			Point.Z = 5.f;
			Point = State->ResolveBuildingLocation(BuildingIndex, Point, Team);
			if (State->FindRegionAt(Point) != State->FindRegionAt(Center))
				continue;
			FString Reason;
			if (State->ValidateBuildingPlacement(BuildingIndex, Team, Point, Reason))
			{
				Result = Point;
				return true;
			}
		}
	return false;
}
class FForceIdentityScenario : public IAutomationLatentCommand
{
public:
	explicit FForceIdentityScenario(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		UWorld* World = ArmyTestSetup::World();
		if (!World || World->GetTimeSeconds() < 3.f)
			return false;
		ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		ACommandPlayerState* Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!PC || !State || !Wallet || Wallet->CommanderIndex < 0 || !MapReady(State)
			|| !State->Content || !IsValid(State->EnemyCommander))
			return false;
		if (Stage == 0)
		{
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->Destroy();
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				if (It->TeamIndex == 5)
					It->Destroy();
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
			{
				if (It->GetTeamIndex() == 0)
					return Fail(TEXT("Normal new match must not spawn fixed friendly armies"));
				It->Destroy();
			}
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				if (It->TeamIndex == 0)
					return Fail(TEXT("Normal new match must not spawn friendly buildings"));
			State->bVerificationIncomePaused = true;
			Wallet->Resources = 4000; // Isolated budgets; placement and each recruit still use paid authority paths.
			State->EnemyCommander->Resources = 4000;
			ForeignWallet = World->SpawnActor<ACommandPlayerState>();
			if (!ForeignWallet.IsValid())
				return Fail(TEXT("Foreign commander wallet fixture could not spawn"));
			ForeignWallet->CommanderIndex = Wallet->CommanderIndex == 0 ? 1 : 0;
			ForeignWallet->Resources = 4000;
			State->AddPlayerState(ForeignWallet.Get());
			First = Place(State, BarracksIndex, Wallet, 0);
			Second = Place(State, BarracksIndex, Wallet, 0);
			Foreign = Place(State, BarracksIndex, ForeignWallet.Get(), 0);
			Enemy = Place(State, BarracksIndex, State->EnemyCommander.Get(), 5);
			Workshop = Place(State, WorkshopIndex, Wallet, 0);
			if (!First.IsValid() || !Second.IsValid() || !Foreign.IsValid() || !Enemy.IsValid() || !Workshop.IsValid())
				return true;
			if (!Check(First->ForceNumber == 1 && Second->ForceNumber == 2,
					TEXT("Two living producers of one commander receive force numbers 1 and 2")))
				return true;
			if (!Check(Foreign->ForceNumber == 1 && Enemy->ForceNumber == 1,
					TEXT("Another friendly commander and the enemy each start their own force numbering at 1")))
				return true;
			if (!Check(Workshop->ForceNumber == 0, TEXT("A non-producing workshop has no force number")))
				return true;
			Stage = 1;
			return false; // Allow the dynamic navmesh to incorporate all paid building footprints.
		}
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav || Nav->IsNavigationBuildInProgress())
			return false;
		if (!Foreign.IsValid() || !Enemy.IsValid() || (Stage == 1 && !Second.IsValid()))
			return Fail(TEXT("Isolated producer disappeared"));
		if (Stage == 1)
		{
			if (!First.IsValid())
				return Fail(TEXT("First force producer disappeared before recruitment"));
			OwnedUnit = Recruit(First.Get(), State);
			SecondUnit = Recruit(Second.Get(), State);
			ForeignUnit = Recruit(Foreign.Get(), State);
			EnemyUnit = Recruit(Enemy.Get(), State);
			if (!OwnedUnit.IsValid() || !SecondUnit.IsValid() || !ForeignUnit.IsValid() || !EnemyUnit.IsValid())
				return true;
			SurvivorForce = First->ForceGroup;
			if (!Check(SurvivorForce->ForceNumber == 1 && Second->ForceGroup->ForceNumber == 2
						&& Foreign->ForceGroup->ForceNumber == 1 && Enemy->ForceGroup->ForceNumber == 1,
					TEXT("Each recruited force snapshots its own producer's number")))
				return true;
			PC->SelectActor(OwnedUnit.Get());
			if (!Check(PC->GetSelectedBuilding() == First.Get(),
					TEXT("Selecting an owned paid recruit selects its living producer")))
				return true;
			PC->SelectActor(SecondUnit.Get());
			if (!Check(PC->GetSelectedBuilding() == Second.Get(),
					TEXT("Selecting another owned force's recruit selects that producer, not the prior force")))
				return true;
			PC->SelectActor(ForeignUnit.Get());
			if (!Check(PC->GetSelectedBuilding() == nullptr,
					TEXT("A foreign friendly recruit clears selection despite sharing force number 1")))
				return true;
			PC->SelectActor(OwnedUnit.Get());
			PC->SelectActor(EnemyUnit.Get());
			if (!Check(PC->GetSelectedBuilding() == nullptr,
					TEXT("An enemy recruit clears selection despite sharing force number 1")))
				return true;
			SecondUnit->ReceiveAttack(SecondUnit->MaxHealth(), EnemyUnit.Get());
			if (!Check(!SecondUnit->IsAlive() && Second->IsAlive(),
					TEXT("Real lethal damage kills the owned recruit without destroying its producer")))
				return true;
			PC->SelectActor(OwnedUnit.Get());
			PC->SelectActor(SecondUnit.Get());
			if (!Check(PC->GetSelectedBuilding() == nullptr, TEXT("A dead owned recruit cannot select its living producer")))
				return true;
			RememberedFront = SurvivorForce->FrontLocation;
			RememberedOrder = SurvivorForce->FrontOrder;
			First->ReceiveAttack(First->MaxHealth(), EnemyUnit.Get());
			if (!Check(!First.IsValid() && SurvivorForce.IsValid() && OwnedUnit->IsAlive()
						&& !IsValid(SurvivorForce->GetProductionBuilding()) && SurvivorForce->ForceNumber == 1
						&& SurvivorForce->FrontLocation == RememberedFront && SurvivorForce->FrontOrder == RememberedOrder,
					TEXT("Destroying producer 1 leaves a living orphan force with number 1 and its last front")))
				return true;
			PC->SelectActor(Second.Get());
			PC->SelectActor(OwnedUnit.Get());
			if (!Check(PC->GetSelectedBuilding() == nullptr, TEXT("Selecting an orphan survivor clears selection")))
				return true;
			Second->ReceiveAttack(Second->MaxHealth(), EnemyUnit.Get());
			if (!Check(!Second.IsValid(), TEXT("Destroying empty producer 2 releases its number")))
				return true;
			Stage = 2;
			return false; // Let navigation remove the destroyed producer's footprint before paid replacement.
		}
		if (Stage == 2)
		{
			Replacement = Place(State, BarracksIndex, Wallet, 0);
			if (!Replacement.IsValid())
				return true;
			if (!Check(Replacement->ForceNumber == 2
						&& Foreign->ForceNumber == 1 && SurvivorForce.IsValid() && SurvivorForce->ForceNumber == 1,
					TEXT("New producer gets 2 while living orphan force 1 reserves its number")))
				return true;
			Stage = 3;
			return false;
		}
		if (Stage == 3)
		{
			if (!Replacement.IsValid() || !SurvivorForce.IsValid() || !OwnedUnit.IsValid())
				return Fail(TEXT("Replacement producer or orphan survivor disappeared"));
			const int32 Frontline = UnitIndex(State, EUnitRole::Frontline);
			if (!Check(Replacement->SetProduction(Frontline, true) && IsValid(Replacement->ForceGroup),
					TEXT("The paid replacement producer can configure its own force")))
				return true;
			Replacement->SetProduction(Frontline, false);
			if (!Check(Replacement->ForceGroup != SurvivorForce.Get() && Replacement->ForceGroup->ForceNumber == 2
						&& Replacement->ForceGroup->GetUnits().IsEmpty() && OwnedUnit->GetGroup() == SurvivorForce.Get()
						&& SurvivorForce->ForceNumber == 1 && !IsValid(SurvivorForce->GetProductionBuilding())
						&& SurvivorForce->FrontLocation == RememberedFront && SurvivorForce->FrontOrder == RememberedOrder,
					TEXT("New force 2 does not adopt orphan survivors or change their number and front")))
				return true;
			PC->SelectActor(Replacement.Get());
			PC->SelectActor(OwnedUnit.Get());
			if (!Check(PC->GetSelectedBuilding() == nullptr,
					TEXT("An orphan survivor cannot select an unrelated replacement")))
				return true;
			OwnedUnit->ReceiveAttack(OwnedUnit->MaxHealth(), EnemyUnit.Get());
			if (!Check(!OwnedUnit->IsAlive(), TEXT("Real lethal damage kills the last orphan unit")))
				return true;
			Stage = 4;
			return false;
		}
		ACommandBuilding* Reused = Place(State, BarracksIndex, Wallet, 0);
		if (!Reused)
			return true;
		if (!Check(Reused->ForceNumber == 1 && Replacement.IsValid() && Replacement->ForceNumber == 2,
				TEXT("Number 1 becomes reusable only after its producer and every orphan unit are dead")))
			return true;
		Test->AddInfo(TEXT("Force identity proof: per-commander 1/2 numbering, enemy and foreign 1, non-producer 0, paid recruitment, owned/enemy/foreign/dead/orphan selection, living-orphan number reservation, dead-orphan reuse and survivor number/front retention."));
		return true;
	}
private:
	bool Check(bool Value, const TCHAR* Message)
	{
		if (!Value)
			Test->AddError(Message);
		return Value;
	}
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		return true;
	}
	ACommandBuilding* Place(ACommandGameState* State, int32 BuildingIndex, ACommandPlayerState* Wallet, int32 Team)
	{
		const UBuildingDefinition* Definition = State->Content->Building(BuildingIndex);
		if (!Definition)
		{
			Fail(TEXT("Match content lacks the requested building definition"));
			return nullptr;
		}
		FVector Location;
		const FVector Center = (Team == 5 ? State->EnemyHeadquarters : State->FriendlyHeadquarters)->GetActorLocation();
		if (!FindPlacement(State, BuildingIndex, Team, Center, Location))
		{
			Fail(TEXT("No valid isolated force-identity building footprint"));
			return nullptr;
		}
		const int32 Before = Wallet->Resources;
		FString Reason;
		ACommandBuilding* Building = State->TryPlaceBuilding(BuildingIndex, Location, Wallet, Team, Reason);
		if (!Check(IsValid(Building) && Wallet->Resources == Before - ACommandBuilding::GetBuildCost(*Definition),
				TEXT("Force-identity producer or workshop placement pays its actual building cost")))
			return nullptr;
		Building->Tick(60.f);
		if (!Check(Building->IsAlive() && Building->IsComplete(), TEXT("Paid building completes before recruitment")))
			return nullptr;
		return Building;
	}
	AArmyUnit* Recruit(ACommandBuilding* Producer, ACommandGameState* State)
	{
		const int32 Frontline = UnitIndex(State, EUnitRole::Frontline);
		const int32 Before = Producer->OwningPlayerState->Resources;
		if (!Check(Producer->SetProduction(Frontline, true) && IsValid(Producer->ForceGroup),
				TEXT("Completed producer configures a real frontline force")))
			return nullptr;
		Producer->TickProduction(Producer->GetProductionDuration());
		Producer->SetProduction(Frontline, false);
		if (!Check(Producer->ForceGroup->GetUnits().Num() == 1
					&& Producer->OwningPlayerState->Resources == Before - Producer->GetProductionCost(),
				TEXT("One real frontline recruit is deployed and charged exactly once")))
			return nullptr;
		AArmyUnit* Unit = Producer->ForceGroup->GetUnits()[0];
		if (!Check(IsValid(Unit) && Unit->IsAlive() && Unit->GetGroup() == Producer->ForceGroup
					&& Unit->GetGroup()->GetProductionBuilding() == Producer,
				TEXT("Paid living recruit belongs to its producer's force")))
			return nullptr;
		Producer->ForceGroup->SetActorTickEnabled(false); // Selection isolation, not travel/combat proof.
		Unit->SetActorTickEnabled(false);
		return Unit;
	}
	FAutomationTestBase* Test;
	int32 Stage = 0;
	TWeakObjectPtr<ACommandPlayerState> ForeignWallet;
	TWeakObjectPtr<ACommandBuilding> First, Second, Foreign, Enemy, Workshop, Replacement;
	TWeakObjectPtr<AArmyUnit> OwnedUnit, SecondUnit, ForeignUnit, EnemyUnit;
	TWeakObjectPtr<AArmyGroup> SurvivorForce;
	FVector RememberedFront;
	EFrontOrder RememberedOrder = EFrontOrder::Defend;
};
}
bool FConstructionForceIdentityTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ForceIdentityScenarioTests::FForceIdentityScenario(this));
	return true;
}
#endif
