#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "HAL/PlatformTime.h"

class ADepositSite;

class FEnemyConstructionScenario : public IAutomationLatentCommand
{
public:
	explicit FEnemyConstructionScenario(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override;
private:
	bool Dispatch(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC);
	bool Stage0(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, AEnemyCommander* Planner);
	bool Stage1(ACommandGameState* State, ACommandPlayerController* PC);
	bool Stage2(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, AEnemyCommander* Planner);
	bool FindExtractor(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, AEnemyCommander* Planner, int32 Joined, int32 Travelling);
	bool ObserveIncome(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, AEnemyCommander* Planner, ACommandBuilding* CapturedExtractor, int32 ExtractorRate);
	bool ExhaustIncome(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, ACommandBuilding* CapturedExtractor, ADepositSite* TargetDeposit, int32 RegionIndex, const TArray<ACommandBuilding*>& EnabledProducers, int32 FinalBonus);
	bool Stage3(UWorld* World, ACommandGameState* State, AEnemyCommander* Planner);
	bool Stage4(UWorld* World, ACommandGameState* State, AEnemyCommander* Planner);
	bool EvaluateRecovery(UWorld* World, ACommandGameState* State, AEnemyCommander* Planner, float Health, int32 Joined);
	bool ResumeRecovery(UWorld* World, ACommandGameState* State, float Health, int32 Joined);
	bool Stage5(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, AEnemyCommander* Planner);
	const FJevPublishedPlan* PublishedRecovery(const ACommandGameState* State) const;
	bool PrepareRecovery(AEnemyCommander* Planner, ACommandGameState* State, ACommandPlayerController* PC, UWorld* World);
	bool DamageRecovery(ACommandGameState* State, AArmyGroup* Threat, int32 DamageJoined);
	bool ObserveResumedTravel(const ACommandGameState* State, const UWorld* World);
	bool Fail(const TCHAR* Message);
	bool InSafeRecovery(const ACommandGameState* State) const;
	ACommandBuilding* PlaceEnemy(ACommandGameState* State, FName Id, const FVector& Center);
	FAutomationTestBase* Test;
	TWeakObjectPtr<ACommandBuilding> Production;
	TWeakObjectPtr<AArmyGroup> Recovery;
	TWeakObjectPtr<ACommandBuilding> OtherProduction;
	TWeakObjectPtr<ACommandBuilding> ForwardProduction;
	TWeakObjectPtr<AArmyGroup> OtherForce;
	TArray<TWeakObjectPtr<AArmyUnit>> DamagedUnits;
	bool bObservedRegionAdvance = false;
	bool bObservedSafeHold = false;
	bool bRecoveryObjectiveOpened = false;
	int32 SafeRecoveryRegion = INDEX_NONE;
	float InitialDamagedHealth = 0.f;
	float DefenseReadyAt = 0.f;
	float RecoveryReadyAt = 0.f;
	int32 HealedTicket = 0;
	float HealedDeadline = 0.f;
	int32 Stage = 0;
	int32 HumanBalance = 0;
	int32 EnemyBudget = 600;
	double NextProgress = 0.;
	int32 TimedStage = -1;
	double Started = FPlatformTime::Seconds();
	double StageStarted = Started;
	FVector ResumeStart = FVector::ZeroVector;
	FVector ResumeTarget = FVector::ZeroVector;
	double ResumeDeadline = -1.;
};
#endif
