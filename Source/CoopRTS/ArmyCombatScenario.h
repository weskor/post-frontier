#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CombatTarget.h"
#include "CommandPlayerController.h"
#include "EnemyCommander.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Headquarters.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"

namespace ArmyCombatScenarioPrivate
{
// Run alone in a fresh standalone world. Explicit mixed-role fixtures exercise
// authoritative combat and internal orders, not starting forces or paid production.
class FArmyCombatScenario : public IAutomationLatentCommand
{
public:
	explicit FArmyCombatScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override;

private:
	bool Check(bool Condition, const TCHAR* Message)
	{
		if (!Condition)
			Test->AddError(Message);
		return Condition;
	}

	void SetStage(int32 Next, double Now)
	{
		Stage = Next;
		StageStarted = Now;
	}

	uint32 TotalAttacks() const
	{
		uint32 Count = 0;
		for (const AArmyUnit* Unit : Army->GetUnits())
			Count += Unit->AttackCount;
		return Count;
	}

	void IsolateWorld();
	bool MoveHoldStage(double Now);
	bool AttackStage(double Now);
	bool RetreatStage(double Now);
	bool Begin();
	bool PrepareFixtures(UWorld* World, const ACommandGameState* State);
	bool BeginMoveHold(UWorld* World, const ACommandGameState* State);
	bool CheckWeapons(const ACommandGameState* State);
	bool ProbeWeapon(AArmyUnit* Shooter, AArmyUnit* Front, AArmyUnit* Ranged, AArmyUnit* Siege);
	void PrepareSplash(AArmyUnit* Front, FVector (&SplashPositions)[3], int32 (&SplashHealth)[3]);
	bool CheckSplash(AArmyUnit* Shooter, AArmyUnit* Front, const FVector (&SplashPositions)[3],
		const int32 (&SplashHealth)[3], int32 FriendlyHealth, const FVector& FriendlyPosition);
	bool CheckHeadquartersSplash(const ACommandGameState* State, AArmyUnit* Siege);
	bool CheckCounterAcquisition(const ACommandGameState* State);
	bool CheckCounterPreference(const FVector& Anchor);
	bool CheckStructurePriority(AHeadquarters* HQ, bool& bOk);
	bool CheckPersistentLock(const ACommandGameState* State, const FVector& Anchor, bool& bOk);
	void CheckChaseBounds(const AArmyUnit* Heavy, bool& bOk);
	bool RejectUnregisteredTargets();
	bool Rejected(AActor* Target, uint32 Serial, const FVector& Destination);
	bool CheckDetachedTarget(uint32 Serial, const FVector& Destination);
	bool CheckUnregisteredHeadquarters(ACommandGameState* State, uint32 Serial, const FVector& Destination);
	bool CheckUnregisteredBuilding(ACommandGameState* State, uint32 Serial, const FVector& Destination);

	FAutomationTestBase* Test;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Army;
	TWeakObjectPtr<AArmyGroup> Enemy;
	TWeakObjectPtr<AArmyUnit> Victim;
	uint32 EncounterAttacks = 0;
	uint32 RetreatAttacks = 0;
	FVector RetreatCenter = FVector::ZeroVector;
	FVector MoveStart = FVector::ZeroVector;
	uint32 MoveAttacks = 0;
	bool bObservedRetreatMotion = false;
	int32 Stage = 0;
	bool bIsolated = false;
	double Started;
	double StageStarted = 0.;
};
}

#endif
