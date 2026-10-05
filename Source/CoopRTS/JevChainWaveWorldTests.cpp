#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevReleaseWorldFixture.h"
#include "Rules/JevPlanner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevChainWaveStandingTest, "CoopRTS.Enemy.Chain.WaveStanding",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace JevWorldKit;

// On Habitable Zone v2 a wave needs about a minute to march from JEV's main to its objective, far longer than the
// 25 s commitment window. The wave must keep its Attack past the window and arrive.
class FWaveStanding : public FScenario
{
public:
	using FScenario::FScenario;

private:
	bool Prepare() override
	{
		// The v1.1 wave raids a human region; that is the wave under test.
		SkipTo(120.8f);
		Enter(1);
		return false;
	}

	bool Step() override
	{
		switch (Stage)
		{
		case 1:
			return WaveLaunched(1) ? Launched() : false;
		case 2:
			return PastWindow();
		default:
			return Arrived();
		}
	}

	bool Launched()
	{
		Wave = EnemyForces(Kit.World);
		if (!Check(!Wave.IsEmpty(), TEXT("The wave fielded forces")))
			return true;
		for (const AArmyGroup* Force : Wave)
		{
			const FJevPublishedPlan* Plan = PlanOf(Force);
			if (!Check(Plan && Plan->Verb == EForceVerb::Attack && Plan->TargetRegionIndex != INDEX_NONE, TEXT("Each wave force publishes an Attack"))
				|| !Check(Plan->EtaSeconds > JevPlanner::CommitmentSeconds + 10.f,
					*FString::Printf(TEXT("The march (ETA %.1f s) is longer than the commitment window, or this test proves nothing"), Plan->EtaSeconds)))
				return true;
			Objectives.Add(Force->ForceNumber, Plan->TargetRegionIndex);
			Tickets.Add(Force->ForceNumber, Plan->TicketNumber);
			Eta = FMath::Max(Eta, Plan->EtaSeconds);
		}
		Enter(2);
		return false;
	}

	// Well after the window and several evaluations later every wave force still carries its original order.
	bool PastWindow()
	{
		if (InStage() < JevPlanner::CommitmentSeconds + 12.)
			return false;
		for (const AArmyGroup* Force : Wave)
		{
			if (Force->GetAliveCount() == 0)
				continue;
			const FJevPublishedPlan* Plan = PlanOf(Force);
			if (!Check(Force->Verb == EForceVerb::Attack && Force->TargetRegionIndex == Objectives.FindRef(Force->ForceNumber) && !Force->Orders.IsEmpty(),
					TEXT("After the commitment window a wave force still executes Attack on its objective, not a hold or retreat"))
				|| !Check(Plan && Plan->TicketNumber == Tickets.FindRef(Force->ForceNumber) && Plan->CommittedUntil < Kit.World->GetTimeSeconds(),
					TEXT("Its ticket is the one it launched with, and the window has expired")))
				return true;
		}
		Enter(3);
		return false;
	}

	bool Arrived()
	{
		const bool bThere = Wave.ContainsByPredicate([this](const AArmyGroup* Force) {
			return Force->GetAliveCount() > 0 && ArmyTestSetup::CurrentRegion(Force) == Objectives.FindRef(Force->ForceNumber);
		});
		if (bThere)
			return true;
		if (InStage() > 2. * Eta + 40.)
			return Fail(TEXT("The wave did not reach its objective region"));
		return false;
	}

	float Eta = 0.f;
	TArray<AArmyGroup*> Wave;
	TMap<int32, int32> Objectives;
	TMap<int32, int32> Tickets;
};
}

bool FJevChainWaveStandingTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FWaveStanding(this));
	return true;
}

#endif
