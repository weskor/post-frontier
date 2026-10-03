#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "DepositSite.h"
#include "MapRegion.h"
#include "CommandGameMode.h"
#include "Content/BuildingDefinition.h"
#include "Headquarters.h"
#include "NavigationSystem.h"
#include "Rules/PlacementPolicy.h"
#include "Commands/OrderGraph.h"
#include "Components/BoxComponent.h"
#include "HAL/PlatformTime.h"

namespace ConstructionScenarioTests
{
using namespace ArmyTestSetup;
bool FindPlacement(ACommandGameState* State, int32 BuildingIndex, const FVector& Center, FVector& Result);
int32 FindForceRegion(ACommandGameState* State, AArmyGroup* Force, const FVector& Preferred,
	const TArray<int32, TInlineAllocator<4>>& Reserved);
class FConstructionScenario : public IAutomationLatentCommand
{
public:
	FConstructionScenario(FAutomationTestBase* InTest, bool bInProduction) : Test(InTest), bProduction(bInProduction) {}
	bool Update() override;
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
	static int32 Alive(const ACommandBuilding* Producer)
	{
		int32 Joined, Travelling;
		Producer->GetForceCounts(Joined, Travelling);
		return Joined + Travelling;
	}
	static AArmyUnit* TravellingRecruit(const AArmyGroup* Force)
	{
		for (TActorIterator<AArmyUnit> It(Force->GetWorld()); It; ++It)
			if (It->IsAlive() && It->IsReinforcing() && It->GetGroup() == Force)
				return *It;
		return nullptr;
	}
	static bool JoinedCenterMatches(const AArmyGroup* Force)
	{
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing())
			{
				Sum += Unit->GetActorLocation();
				++Count;
			}
		return Count == 0 || FVector::Dist2D(Sum / Count, Force->GetCenter()) < 1.f;
	}
	static bool ValidMembers(const ACommandBuilding* Producer, int32 Capacity)
	{
		uint32 Slots = 0;
		int32 Count = 0;
		for (TActorIterator<AArmyUnit> It(Producer->GetWorld()); It; ++It)
		{
			const AArmyUnit* Unit = *It;
			if (!Unit->IsAlive() || Unit->GetGroup() != Producer->ForceGroup)
				continue;
			++Count;
			if (Unit->GetUnitRole() != Producer->ProductionRole
				|| Unit->GetCommanderIndex() != Producer->OwningPlayerState->CommanderIndex
				|| Unit->GetCompositionSlot() < 0 || Unit->GetCompositionSlot() >= Capacity
				|| (Slots & (1u << Unit->GetCompositionSlot())))
				return false;
			Slots |= 1u << Unit->GetCompositionSlot();
		}
		return Count == Alive(Producer);
	}
	bool CheckDeadline();
	bool StageZero(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool ValidatePlacement(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, const UBuildingDefinition* Barracks, FVector Location, const FVector& Snapped);
	bool BuildPlacement(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, const UBuildingDefinition* Barracks, const FVector& Location, const FVector& Snapped, const FNavLocation& Ground, int32 Before, FString& RequestedReason, FString& SnappedReason);
	bool StageOne(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool ConfigureProducers(ACommandPlayerState* Wallet);
	bool StageTwo(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool StageEleven(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool StageThree(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool CompleteForces(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool StageFour(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool StageFive(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool StageSix(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool StageSeven(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool StageEight(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool StageNine(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool StageTen(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool Lifecycle(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	bool LifecycleWorkshop(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, ACommandPlayerState* OtherWallet);
	bool LifecycleCapture(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, ACommandPlayerState* OtherWallet, FVector Location);
	bool LifecycleExtractor(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, ACommandPlayerState* OtherWallet, FVector Location, ADepositSite* Deposit, ACapturePoint* Site, AArmyGroup* Occupiers);
	bool LifecycleDepletion(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, ACommandPlayerState* OtherWallet, ADepositSite* Deposit, ACapturePoint* Site, AArmyGroup* Occupiers, ACommandBuilding* Extractor);
	bool LifecycleContest(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, ADepositSite* Deposit, ACapturePoint* Site, AArmyGroup* Enemy);
	bool RunProduction(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet);
	FAutomationTestBase* Test;
	bool bProduction;
	int32 Stage = 0;
	int32 TimedStage = 0;
	double Started = FPlatformTime::Seconds();
	double StageStarted = Started;
	float LastProgressReport = 0.f;
	TWeakObjectPtr<ACommandBuilding> Building;
	TWeakObjectPtr<AArmyGroup> Squad;
	TArray<TWeakObjectPtr<ACommandBuilding>> Producers;
	TWeakObjectPtr<ACommandBuilding> NewProducer;
	TArray<TWeakObjectPtr<AArmyGroup>> Forces;
	TWeakObjectPtr<AArmyGroup> Attacker;
	TWeakObjectPtr<AArmyUnit> Recruit;
	TWeakObjectPtr<AActor> Blocker;
	TWeakObjectPtr<ACommandPlayerState> UnrelatedWallet;
	FVector RecruitStart, JoinedStart;
	int32 OtherRegion = INDEX_NONE, RememberedRegion = INDEX_NONE;
	EForceVerb OtherVerb = EForceVerb::MoveHold;
	int32 FirstRecruitBalance = 0, FillBalance = 0, ReplacementBalance = 0, SurvivorCount = 0;
	bool bRecruitMoved = false, bJoinedMoved = false;
};
}
#endif
