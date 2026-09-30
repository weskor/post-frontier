#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "Headquarters.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyConstructionTest, "CoopRTS.Enemy.ConstructionEconomy",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

class FEnemyConstructionScenario : public IAutomationLatentCommand
{
public:
	explicit FEnemyConstructionScenario(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		UWorld* World = ArmyTestSetup::World();
		if (!World || World->GetTimeSeconds() < 3.f) return false;
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
		if (!State || !PC || !State->EnemyHeadquarters || !State->FriendlyHeadquarters) return false;
		AEnemyCommander* Planner = nullptr;
		for (TActorIterator<AEnemyCommander> It(World); It && !Planner; ++It) Planner = *It;
		if (!Planner) return Fail(TEXT("New match must create the economic enemy commander"));
		if (Stage == 3)
		{
			if (!Recovery.IsValid() || !Production.IsValid() || Production->ForceGroup != Recovery.Get()
				|| Recovery->GetProductionBuilding() != Production.Get())
				return Fail(TEXT("Recovery must retain its living producer and stable force backlink"));
			float Health = 0.f;
			int32 Joined = 0;
			for (const AArmyUnit* Unit : Recovery->GetUnits())
				if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing())
				{
					Health += float(Unit->GetHealth()) / Unit->MaxHealth();
					++Joined;
				}
			Planner->EvaluatePlan();
			if (!OtherProduction.IsValid() || OtherProduction->FrontOrder == EFrontOrder::FallBack
				|| OtherProduction->ForceGroup->GetProductionBuilding() != OtherProduction.Get())
				return Fail(TEXT("Recovering force cannot put another producer into fallback or take its units"));
			if (!Joined || Health / Joined < .8f)
			{
				if (Production->FrontOrder != EFrontOrder::FallBack || Recovery->FrontOrder != EFrontOrder::FallBack)
					return Fail(TEXT("Owning producer must retain fallback until its joined force heals"));
				return false;
			}
			if (Production->FrontOrder == EFrontOrder::FallBack || Recovery->FrontOrder == EFrontOrder::FallBack)
				return Fail(TEXT("Naturally healed force must resume its producer's strategic front"));
			Test->AddInfo(TEXT("Enemy proof: per-unit paid economy, real capture/outpost, reactive defense, producer-scoped recovery with unchanged backlink and natural healing."));
			return true;
		}
		if (Stage == 0)
		{
			for (TActorIterator<ACommandBuilding> It(World); It; ++It) if (It->TeamIndex == 5) It->Destroy();
			for (TActorIterator<AArmyGroup> It(World); It; ++It) if (It->GetTeamIndex() == 5) It->Destroy();
			State->bVerificationIncomePaused = true;
			State->EnemyResources = 600;
			HumanBalance = PC->GetPlayerState<ACommandPlayerState>()->Resources;
			Planner->EvaluatePlan();
			for (ACommandBuilding* Building : State->Buildings)
				if (IsValid(Building) && Building->TeamIndex == 5 && Building->IsProducer()) Production = Building;
			const int32 BarracksCost = State->Content->FindBuilding(TEXT("barracks"))->BuildCost;
			if (!Production.IsValid() || State->EnemyResources != 600 - BarracksCost
				|| Production->OwningPlayerState || Production->IsComplete())
				return Fail(TEXT("Enemy must pay its own wallet for an unfinished barracks through shared construction"));
			Stage = 1;
			Test->AddInfo(TEXT("Enemy paid construction observed; waiting for normal construction and single-unit production ticks."));
			return false;
		}
		if (Stage == 1)
		{
			AArmyGroup* Produced = Production.IsValid() ? Production->ForceGroup.Get() : nullptr;
			if (!IsValid(Produced) || Produced->GetUnits().IsEmpty()) return false;
			int32 Joined, Travelling;
			Production->GetForceCounts(Joined, Travelling);
			const int32 Count = Joined + Travelling;
			if (Count != 1 || !Production->bForceConfigured || Produced->GetProductionBuilding() != Production.Get()
				|| !Produced->bAutomaticFront || Produced->FrontOrder != EFrontOrder::Secure
				|| State->EnemyResources != 600 - State->Content->FindBuilding(TEXT("barracks"))->BuildCost - 20
				|| PC->GetPlayerState<ACommandPlayerState>()->Resources != HumanBalance)
				return Fail(TEXT("First enemy production must create one paid infantry unit, not a batch, in its producer force"));
			Recovery = Produced;
			Stage = 2;
			Test->AddInfo(TEXT("Enemy naturally produced one paid recruit; waiting for capture and completed outpost."));
			return false;
		}
		if (!Production.IsValid() || !Recovery.IsValid()) return Fail(TEXT("Enemy lost its producer-owned expansion force"));
		int32 Joined, Travelling;
		Production->GetForceCounts(Joined, Travelling);
		const int32 Count = Joined + Travelling;
		int32 ConstructionSpend = 0;
		for (const ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->TeamIndex == 5)
				ConstructionSpend += Building->GetDefinition() ? Building->GetDefinition()->BuildCost : 0;
		if (Count > 6 || Production->ForceGroup != Recovery.Get() || Recovery->GetProductionBuilding() != Production.Get()
			|| State->EnemyResources != 600 - ConstructionSpend - Count * 20
			|| PC->GetPlayerState<ACommandPlayerState>()->Resources != HumanBalance)
			return Fail(TEXT("Enemy infantry including travellers uses six independent slots and pays 20 per unit from enemy wallet"));
		bool bEstablished = false;
		for (ACapturePoint* Site : State->CaptureSites) if (IsValid(Site) && Site->IsEstablishedForTeam(5)) bEstablished = true;
		if (!bEstablished) return false;
		if (State->GetEnemyIncomePerSecond() <= ACommandGameState::BaselineIncomePerSecond)
			return Fail(TEXT("Completed enemy outpost must increase its territorial income"));
		AArmyGroup* Threat = ArmyTestSetup::SpawnGroup(World, PC, 20,
			State->EnemyHeadquarters->GetActorLocation() + FVector(-800.f, -500.f, 0.f));
		if (!Threat) return Fail(TEXT("Real hostile-pressure fixture could not spawn"));
		Planner->EvaluatePlan();
		bool bDefending = false;
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			if (It->GetTeamIndex() == 5 && It->bAutomaticFront && It->FrontOrder == EFrontOrder::Defend) bDefending = true;
		if (!bDefending) return Fail(TEXT("Real nearby attackers must interrupt expansion with a defensive front"));
		if (!Production.IsValid() || Production->ForceGroup != Recovery.Get()
			|| Recovery->GetProductionBuilding() != Production.Get())
			return Fail(TEXT("Recovery fixture must use the real producer-owned force"));
		for (AArmyUnit* Unit : Recovery->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing())
				Unit->ReceiveAttack(Unit->GetHealth() - FMath::Max(1, Unit->MaxHealth() / 4), Threat->GetUnits()[0]);
		Threat->Destroy();
		State->EnemyResources = 2000; // Paid second producer and repairs setup, not asserted income.
		OtherProduction = PlaceEnemy(State, TEXT("barracks"), State->EnemyHeadquarters->GetActorLocation());
		ACommandBuilding* Workshop = PlaceEnemy(State, TEXT("workshop"), State->EnemyHeadquarters->GetActorLocation());
		if (!OtherProduction.IsValid() || !Workshop) return Fail(TEXT("Recovery isolation fixtures have no legal footprints"));
		OtherProduction->Tick(60.f);
		Workshop->Tick(60.f);
		if (!OtherProduction->SetProduction(State->Content->UnitIndexForRole(EUnitRole::Ranged), true)
			|| !Workshop->TryResearch(EArmyDoctrine::FieldRepairs))
			return Fail(TEXT("Paid independent force and recovery research setup rejected"));
		Planner->EvaluatePlan();
		if (Production->FrontOrder != EFrontOrder::FallBack || Recovery->FrontOrder != EFrontOrder::FallBack
			|| Recovery->GetProductionBuilding() != Production.Get() || OtherProduction->FrontOrder == EFrontOrder::FallBack)
			return Fail(TEXT("Real damage must retreat only the injured force's owning producer without detaching it"));
		Planner->SetActorTickEnabled(false); // Explicit evaluations below observe natural return and repair ticks.
		Stage = 3;
		return false;
	}
private:
	bool Fail(const TCHAR* Message) { Test->AddError(Message); return true; }
	ACommandBuilding* PlaceEnemy(ACommandGameState* State, FName Id, const FVector& Center)
	{
		for (int32 Ring = 0; Ring < 6; ++Ring)
			for (int32 Direction = 0; Direction < 16; ++Direction)
			{
				const float Angle = Direction * PI / 8.f;
				FVector Location = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 110.f);
				Location.Z = 5.f;
				FString Reason;
				if (ACommandBuilding* Building = State->TryPlaceBuilding(State->Content->BuildingIndexOf(Id), Location, nullptr, 5, Reason)) return Building;
			}
		return nullptr;
	}
	FAutomationTestBase* Test;
	TWeakObjectPtr<ACommandBuilding> Production;
	TWeakObjectPtr<AArmyGroup> Recovery;
	TWeakObjectPtr<ACommandBuilding> OtherProduction;
	int32 Stage = 0;
	int32 HumanBalance = 0;
};
bool FEnemyConstructionTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEnemyConstructionScenario(this));
	return true;
}
#endif
