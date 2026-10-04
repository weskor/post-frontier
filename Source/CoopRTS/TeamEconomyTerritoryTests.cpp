#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "TeamEconomyFixture.h"

// Connected territory: rigs and reward regions pay only through regions the team controls from its main.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamNeckCutTest, "CoopRTS.Economy.Team.NeckCut",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamAlternatePathTest, "CoopRTS.Economy.Team.AlternatePath",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamRewardDataTest, "CoopRTS.Economy.Team.RewardData",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamJevCutTest, "CoopRTS.Economy.Team.JevCut",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamConnectionTimeTest, "CoopRTS.Economy.Team.ConnectionChangeTime",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTeamNeckCutTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		ADepositSite* Deposit = F.DepositIn(F.Far);
		F.SpawnRig(F.Far, F.Wallets[0], 0);
		F.Pay();
		T.TestEqual(TEXT("A connected rig pays its half"), F.Wallets[0]->Resources, 8);
		T.TestEqual(TEXT("and drains"), Deposit->Remaining, 1200 - 8);
		T.TestTrue(TEXT("The far region is connected through the neck"), F.State->IsRegionConnected(0, F.Far));
		// Enemy troops in the neck contest it, but the team still controls it.
		F.SpawnHostileIn(F.Neck);
		T.TestTrue(TEXT("The neck is contested"), F.State->IsRegionContested(F.Neck, 0));
		F.Pay();
		T.TestEqual(TEXT("A contested neck still carries income"), F.Wallets[1]->Resources, 16);
		T.TestEqual(TEXT("and the rig keeps draining"), Deposit->Remaining, 1200 - 16);
		// The enemy takes the neck: the far rig is cut off from the main.
		F.SetController(F.Neck, 5);
		F.Pay();
		T.TestFalse(TEXT("The far region is cut"), F.State->IsRegionConnected(0, F.Far));
		T.TestFalse(TEXT("and the neck is not ours"), F.State->IsRegionConnected(0, F.Neck));
		T.TestEqual(TEXT("A cut rig pays nothing; baselines still arrive"), F.Wallets[0]->Resources, 20);
		T.TestEqual(TEXT("A cut rig does not deplete"), Deposit->Remaining, 1200 - 16);
		T.TestTrue(TEXT("The estimate drops to the baseline"), FMath::IsNearlyEqual(F.State->GetPowerRate(F.Wallets[0]), 2., 1.e-9));
		// Retaking the neck restores income and depletion.
		F.SetController(F.Neck, 0);
		F.Pay();
		T.TestTrue(TEXT("Recapture reconnects"), F.State->IsRegionConnected(0, F.Far));
		T.TestEqual(TEXT("and income resumes"), F.Wallets[0]->Resources, 28);
		T.TestEqual(TEXT("and depletion resumes"), Deposit->Remaining, 1200 - 24);
		// Losing the far region itself cuts its own rig too.
		F.SetController(F.Far, -1);
		F.Pay();
		T.TestEqual(TEXT("A rig in a region the team lost pays nothing"), F.Wallets[0]->Resources, 32);
		T.TestEqual(TEXT("and does not deplete"), Deposit->Remaining, 1200 - 24);
	}));
	return true;
}

bool FTeamAlternatePathTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		F.SetTopology(true);
		ADepositSite* Deposit = F.DepositIn(F.Far);
		F.SpawnRig(F.Far, F.Wallets[0], 0);
		F.SetController(F.Neck, 5);
		F.Pay();
		T.TestTrue(TEXT("The alternate link keeps the far region connected"), F.State->IsRegionConnected(0, F.Far));
		T.TestEqual(TEXT("so the rig still pays"), F.Wallets[0]->Resources, 8);
		F.SetController(F.Alternate, 5);
		F.Pay();
		T.TestFalse(TEXT("With both routes cut the far region is disconnected"), F.State->IsRegionConnected(0, F.Far));
		T.TestEqual(TEXT("and only baselines arrive"), F.Wallets[0]->Resources, 12);
		T.TestEqual(TEXT("and no reserve is spent"), Deposit->Remaining, 1200 - 8);
		F.SetController(F.Alternate, 0);
		F.Pay();
		T.TestTrue(TEXT("Retaking the alternate link reconnects by another path"), F.State->IsRegionConnected(0, F.Far));
		T.TestEqual(TEXT("and income resumes"), F.Wallets[0]->Resources, 20);
	}));
	return true;
}

bool FTeamRewardDataTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		T.TestEqual(TEXT("Commanders start with no Data"), F.Wallets[0]->Data, 0);
		F.Pay();
		T.TestEqual(TEXT("Without a reward region there is no Data"), F.Wallets[0]->Data, 0);
		F.SetRole(F.Far, ERegionRole::Reward);
		F.Pay();
		T.TestEqual(TEXT("A connected reward region pays each commander 1 Data/s"), F.Wallets[0]->Data, 2);
		T.TestEqual(TEXT("both of them"), F.Wallets[1]->Data, 2);
		T.TestTrue(TEXT("The Data estimate is 1 per second"), FMath::IsNearlyEqual(F.State->GetDataRate(F.Wallets[1]), 1., 1.e-9));
		F.SetRole(F.Neck, ERegionRole::Reward);
		F.Pay();
		T.TestEqual(TEXT("Two reward regions pay twice"), F.Wallets[0]->Data, 6);
		T.TestEqual(TEXT("JEV earns no Data"), F.State->EnemyCommander->Data, 0);
		F.SetController(F.Neck, 5);
		F.Pay();
		T.TestEqual(TEXT("Cutting the neck stops both: the held far region is disconnected"), F.Wallets[0]->Data, 6);
		T.TestTrue(TEXT("and the estimate is zero"), FMath::IsNearlyEqual(F.State->GetDataRate(F.Wallets[0]), 0., 1.e-9));
		F.SetController(F.Neck, 0);
		F.SetController(F.Far, 5);
		F.Pay();
		T.TestEqual(TEXT("A reward region the enemy holds pays nothing; the neck still does"), F.Wallets[0]->Data, 8);
	}));
	return true;
}

bool FTeamJevCutTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		// JEV's chain: its main (1), then the neck and the far region, both JEV-held.
		F.SetNeighbours(0, {});
		F.SetNeighbours(1, { F.Neck });
		F.SetNeighbours(F.Neck, { 1, F.Far });
		F.SetNeighbours(F.Far, { F.Neck });
		F.SetController(F.Neck, 5);
		F.SetController(F.Far, 5);
		ADepositSite* Deposit = F.DepositIn(F.Far);
		if (!T.TestNotNull(TEXT("JEV's rig spawns"), F.SpawnRig(F.Far, nullptr, 5)))
			return;
		ACommandPlayerState* Jev = F.State->EnemyCommander;
		// One commander makes JEV's baseline exactly 4 per payment, whatever fraction it carries.
		int32 Before = Jev->Resources;
		F.Pay();
		T.TestTrue(TEXT("JEV's far region is connected from its main"), F.State->IsRegionConnected(5, F.Far));
		T.TestEqual(TEXT("A connected JEV rig pays JEV its baseline and extraction"), Jev->Resources - Before, 4 + 8);
		T.TestEqual(TEXT("and drains"), Deposit->Remaining, 1200 - 8);
		// The humans take the neck: JEV's rig is cut from its own main.
		F.SetController(F.Neck, 0);
		Before = Jev->Resources;
		F.Pay();
		T.TestFalse(TEXT("The cut JEV region is disconnected"), F.State->IsRegionConnected(5, F.Far));
		T.TestEqual(TEXT("A cut JEV rig pays nothing; the baseline still arrives"), Jev->Resources - Before, 4);
		T.TestEqual(TEXT("and does not deplete"), Deposit->Remaining, 1200 - 8);
		T.TestTrue(TEXT("JEV's estimate falls to its baseline"), FMath::IsNearlyEqual(F.State->GetPowerRate(Jev), 2., 1.e-9));
		F.SetController(F.Neck, 5);
		Before = Jev->Resources;
		F.Pay();
		T.TestEqual(TEXT("Reconnected, it pays and depletes again"), Jev->Resources - Before, 4 + 8);
		T.TestEqual(TEXT("depletion resumes"), Deposit->Remaining, 1200 - 16);
	}));
	return true;
}

namespace
{
// Stage machine on the world clock: the cut must publish within 0.25 s and stamp the server time.
class FConnectionTimeScenario : public IAutomationLatentCommand
{
public:
	explicit FConnectionTimeScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 60.)
		{
			Test->AddError(TEXT("Connection time scenario exceeded 60 seconds"));
			return true;
		}
		UWorld* World = ArmyTestSetup::World();
		if (!Fixture.IsValid())
		{
			Fixture = FTeamEconomyFixture::Create(World, Test);
			if (Fixture.IsValid() && !Fixture->Reset(1))
			{
				Test->AddError(TEXT("Fixture needs regions 2, 3 and 4"));
				return true;
			}
			StageStart = ArmyTestSetup::GameSeconds(World);
			return false;
		}
		FTeamEconomyFixture& F = *Fixture;
		const double Now = ArmyTestSetup::GameSeconds(World);
		const bool bFarConnected = F.State->IsRegionConnected(0, F.Far);
		switch (Stage)
		{
		case 0: // Let the natural tick publish the initial mask.
			if (Now - StageStart < .5 || !bFarConnected)
				return false;
			InitialChange = F.State->GetConnectionChangedAt(0);
			F.SetController(F.Neck, 5);
			CutAt = Now;
			Stage = 1;
			return false;
		case 1: // No manual refresh: the state's own tick must notice.
			if (bFarConnected)
			{
				if (Now - CutAt > .25 + 1.e-3)
				{
					Test->AddError(TEXT("The cut was not published within 0.25 s"));
					return true;
				}
				return false;
			}
			CutPublished = F.State->GetConnectionChangedAt(0);
			Test->TestTrue(TEXT("The change time is the cut's server time"), CutPublished >= CutAt - 1.e-3 && CutPublished <= CutAt + .25 + 1.e-3);
			Test->TestTrue(TEXT("and later than the initial publication"), CutPublished > InitialChange);
			StageStart = Now;
			Stage = 2;
			return false;
		case 2: // No change: the stamp stays.
			if (Now - StageStart < .5)
				return false;
			Test->TestEqual(TEXT("Without a change the stamp does not move"), F.State->GetConnectionChangedAt(0), CutPublished);
			F.SetController(F.Neck, 0);
			CutAt = Now;
			Stage = 3;
			return false;
		default:
			if (!bFarConnected)
			{
				if (Now - CutAt > .25 + 1.e-3)
				{
					Test->AddError(TEXT("The reconnection was not published within 0.25 s"));
					return true;
				}
				return false;
			}
			Test->TestTrue(TEXT("Reconnection stamps a later time"), F.State->GetConnectionChangedAt(0) > CutPublished);
			F.Teardown();
			return true;
		}
	}

private:
	FAutomationTestBase* Test;
	double Started, StageStart = 0., CutAt = 0.;
	float InitialChange = 0.f, CutPublished = 0.f;
	int32 Stage = 0;
	TUniquePtr<FTeamEconomyFixture> Fixture;
};
}

bool FTeamConnectionTimeTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FConnectionTimeScenario(this));
	return true;
}
#endif
