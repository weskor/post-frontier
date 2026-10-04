#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyTestSetup.h"
#include "CapturePoint.h"
#include "DepositSite.h"
#include "Misc/AutomationTest.h"

class AArmyUnit;

// World fixture for shared-pool, connectivity, Data and gift scenarios. Each scenario starts from a
// neutral five-region map it fully rewrites, so scenarios may share one loaded world in any order.
//
// Regions: 0 friendly main, 2 neck, 3 far (with a deposit), 4 alternate link. Region 1 is JEV's main.
struct FTeamEconomyFixture
{
	FAutomationTestBase* Test = nullptr;
	UWorld* World = nullptr;
	ACommandGameState* State = nullptr;
	ACommandPlayerController* Controller = nullptr;
	// Roster wallets in slot order; the controller's own wallet is first.
	TArray<ACommandPlayerState*> Wallets;

	static constexpr int32 Neck = 2, Far = 3, Alternate = 4;

	// Null until the map, controller and enemy wallet exist.
	static TUniquePtr<FTeamEconomyFixture> Create(UWorld* World, FAutomationTestBase* Test);
	// Clears actors and wallets, builds `Commanders` roster wallets with Power 0 and Data 0.
	bool Reset(int32 Commanders);
	void Teardown();

	// Chain 0-2-3, or with Alternate the ring 0-2-3 and 0-4-3.
	void SetTopology(bool bAlternatePath);
	void SetController(int32 RegionIndex, int32 Team);
	void SetRole(int32 RegionIndex, ERegionRole Role);
	ADepositSite* DepositIn(int32 RegionIndex) const;
	// A finished (or partly built) Drill Rig on the region's deposit, built by Builder.
	ACommandBuilding* SpawnRig(int32 RegionIndex, ACommandPlayerState* Builder, int32 Team, float Progress = 1.f);
	ACommandBuilding* SpawnBarracks(int32 Team, float Progress, int32 Slot);
	// A living friendly unit that can deal damage.
	AArmyUnit* SpawnAttacker();
	AArmyUnit* SpawnHostileIn(int32 RegionIndex);

	// One 2 s payment, with every other economy step the real tick performs.
	void Pay(int32 Payments = 1);
	// A short step that lets the state notice new and destroyed buildings without paying.
	void Step();
	ACommandPlayerState* Spawn(int32 CommanderIndex, bool bRoster);
};

// Runs Body once the map is ready and fails the test if it takes too long.
class FTeamEconomyScenario : public IAutomationLatentCommand
{
public:
	FTeamEconomyScenario(FAutomationTestBase* InTest, int32 InCommanders, TFunction<void(FTeamEconomyFixture&)> InBody)
		: Test(InTest), Commanders(InCommanders), Body(MoveTemp(InBody)), Started(FPlatformTime::Seconds()) {}
	bool Update() override;

private:
	FAutomationTestBase* Test;
	int32 Commanders;
	TFunction<void(FTeamEconomyFixture&)> Body;
	double Started;
};
#endif
