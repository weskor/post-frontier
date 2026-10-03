#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "AIController.h"
#include "MapRegion.h"
#include "CapturePoint.h"
#include "ObjectiveAnnouncer.h"
#include "NavigationData.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "HAL/PlatformTime.h"

namespace HoldAlarmFixture
{
enum class ECase : uint8
{
	BuildingEdge,
	Proportional,
	Border,
	Timers,
	SharedCommanders,
	Jev
};

class FScenario : public IAutomationLatentCommand
{
public:
	FScenario(FAutomationTestBase* InTest, ECase InCase)
		: Test(InTest), Case(InCase), Started(FPlatformTime::Seconds()) {}

protected:
	enum class EStage : uint8
	{
		Setup,
		Posts,
		Respond,
		Feint,
		Expanded,
		Combat,
		Return,
		EarlyQuiet,
		StickyAcquire,
		Sticky,
		StickyDeath,
		StickyReacquire,
		LateQuiet,
		AttackAcquire,
		AttackEngaged
	};
	enum class EStep : uint8
	{
		Continue,
		Waiting,
		Done
	};

	bool Check(bool Condition, const TCHAR* Message);
	void SetStage(EStage Next, double Now);
	static double Power(const AArmyGroup& Group);
	bool Project(const FVector& Point, FVector& Ground) const;
	bool Reachable(const FVector& From, const FVector& To) const;
	bool ClearFormation(const FVector& Center) const;
	bool FindGeometry();
	AArmyGroup* Spawn(ACommandPlayerState* Wallet, int32 Index, const FVector& Home);
	static void Teleport(AArmyUnit* Unit, const FVector& Ground);
	bool Setup();
	bool AtPosts() const;
	bool CheckPosts();
	void BeginAlarm(double Now);
	TArray<int32> Responders() const;
	static TArray<int32> SortedByDistance(const FVector& Target, const TArray<FVector>& Centers);
	bool CheckNearest(int32 Count);
	bool CheckTargets();
	bool CheckIdleHolders(const TArray<int32>& Active);
	void EnableWeapons();
	uint32 TotalAttacks() const;
	bool AllEnteredThreatsDead() const;
	double DistanceToBorder(const FVector& Location) const;
	int32 ResponseEventCount() const;
	bool CheckResponseFeed(bool bCheckInitialForces = false);
	bool Finish();
	EStep PrepareUpdate(double& Now);
	bool CheckTimeout();
	bool ObserveHolders();
	bool RunPosts(double Now);
	bool RunRespond(double Now);
	bool RunCombat(double Now);
	virtual bool RunReturn(double Now);
	EStep PrepareSetup(UWorld*& World, ACommandPlayerState*& Human);
	bool SpawnHolders(UWorld* World, ACommandPlayerState* Human, ACommandPlayerState* HolderWallet, FVector& Anchor);
	bool SpawnThreats(UWorld* World, ACommandPlayerState* ThreatWallet);
	bool SpawnBuilding(UWorld* World, ACommandPlayerState* HolderWallet, const FVector& Anchor);
	bool TryGeometry(AMapRegion* Candidate, const FVector& Anchor, double Range, const FVector2D& A, const FVector2D& B, double Fraction);

	virtual bool CheckPostsForCase() { return true; }
	virtual bool ChooseIntrusion() { return true; }
	virtual bool ObserveCaseHolder(const TWeakObjectPtr<AArmyGroup>& Holder) { return true; }
	virtual bool ObserveCaseBorders() { return true; }
	virtual bool CheckCombatForCase() { return true; }
	virtual void BeginReturn() {}
	virtual bool AdvanceResponse(double Now)
	{
		EnableWeapons();
		SetStage(EStage::Combat, Now);
		return bFailed;
	}

	FAutomationTestBase* Test;
	ECase Case;
	EStage Stage = EStage::Setup;
	double Started;
	double StageStarted = 0., ResponseStarted = 0., QuietStarted = -1.;
	bool bFailed = false, bShootBuilding = false, bObservedBuildingDamage = false, bObservedThreatDamage = false;
	bool bCommitObserved = false, bQuietObserved = false, bObservedBorderShot = false, bObservedCounterDamage = false;
	int32 UnitIndex = INDEX_NONE, FrontlineIndex = INDEX_NONE;
	int32 BuildingInitialHealth = 0, ThreatInitialHealth = 0, OutsideHealth = 0, CounterInitialHealth = 0;
	uint32 CounterInitialAttacks = 0;
	int32 EngagedOrderSerial = 0;
	int32 ExpectedResponseEvents = 0;
	TWeakObjectPtr<ACommandGameState> State;
	TWeakObjectPtr<AMapRegion> Region;
	TWeakObjectPtr<ACommandPlayerState> SecondCommander;
	TWeakObjectPtr<AArmyGroup> Enemy, StickyHolder, AttackProbe;
	TWeakObjectPtr<ACommandBuilding> Building;
	TWeakObjectPtr<AArmyUnit> FirstTarget;
	TArray<TWeakObjectPtr<AArmyGroup>> Holders;
	TArray<TWeakObjectPtr<AArmyUnit>> Threats;
	TArray<FVector> Posts, InitialCenters, ExpandedCenters;
	TArray<int32> ExpectedNearest, FirstResponders;
	TMap<AArmyGroup*, uint32> BorderAttacks;
	TSet<AArmyGroup*> MovedHolders;
	TSet<AArmyGroup*> ReachedAnchors;
	FVector Intrusion = FVector::ZeroVector, BuildingLocation = FVector::ZeroVector;
	FVector Outside = FVector::ZeroVector, FarOutside = FVector::ZeroVector, Side = FVector::RightVector;
	FVector OutsideEntryStart = FVector::ZeroVector, CounterLocation = FVector::ZeroVector;
};
}

#endif
