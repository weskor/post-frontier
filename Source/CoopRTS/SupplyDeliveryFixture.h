#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyUnit.h"
#include "Misc/AutomationTest.h"
#include "TeamEconomyFixture.h"

namespace SupplyTests
{
// Each registered test runs alone in a fresh standalone world. The map is rewritten as the chain
// main (0) - Neck (2) - Far (3), all controlled by the humans, so Far is two hops from the producer's
// region. JEV is gone; a single paid Barracks produces a Frontline force. Production is driven by calling
// TickProduction, so the only clock a delivery depends on is the world clock.
class FScenarioBase : public IAutomationLatentCommand
{
public:
	explicit FScenarioBase(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
	bool Update() override;

protected:
	static constexpr int32 Neck = FTeamEconomyFixture::Neck, Far = FTeamEconomyFixture::Far;

	virtual bool Run() = 0;
	bool Check(bool bValue, const TCHAR* Message);
	double Now() const { return ArmyTestSetup::GameSeconds(GameWorld); }
	void SetStage(int32 Next);
	double StageSeconds() const { return Now() - StageStarted; }

	// Finishes one unit of work on the producer: enables, ticks a full duration, pauses again.
	// Returns the Power the wallet lost.
	int32 Produce();
	// Stages 0 and 1: one recruit leaves the producer's exit, is moved to Far and holds there.
	// True once the force stands holding in Far with exactly one member.
	bool PutForceInFar();
	AArmyUnit* FirstMember() const;
	// Living units of the force as actors in the world; a recruit that exists only in a queue is not one.
	int32 MemberActors() const;
	int32 Joined() const { return Force->GetJoinedCount(); }
	int32 UnitCost() const { return Producer->GetProductionCost(); }
	bool InRegion(const AArmyUnit* Unit, int32 Region) const;

	FAutomationTestBase* Test;
	double Started, StageStarted = 0.;
	int32 Stage = 0;
	bool bFailed = false, bPrepared = false;
	UWorld* GameWorld = nullptr;
	ACommandGameState* State = nullptr;
	ACommandPlayerState* Wallet = nullptr;
	TUniquePtr<FTeamEconomyFixture> Fixture;
	TWeakObjectPtr<ACommandBuilding> Producer;
	TWeakObjectPtr<AArmyGroup> Force;
	TWeakObjectPtr<AArmyUnit> Attacker;

private:
	bool Prepare();
};
}

#define SUPPLY_WORLD_TEST(ClassName, TestName, ...)                                \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, "CoopRTS.Forces.Supply." TestName, \
		EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)  \
	bool ClassName::RunTest(const FString&)                                        \
	{                                                                              \
		ADD_LATENT_AUTOMATION_COMMAND(__VA_ARGS__);                                \
		return true;                                                               \
	}
#endif
