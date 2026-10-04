#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "TeamEconomyFixture.h"

// The shared team pool: income splits evenly whoever built the rig, with exact carries and roster changes.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPoolSharedRigTest, "CoopRTS.Economy.Team.SharedRig",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPoolConservationTest, "CoopRTS.Economy.Team.ThreeWayConservation",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPoolRosterTest, "CoopRTS.Economy.Team.JoinAndLeave",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPoolReservesTest, "CoopRTS.Economy.Team.Reserves",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPoolEmptyRosterTest, "CoopRTS.Economy.Team.NoRecipients",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTeamPoolSharedRigTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		// The second commander built the rig; the first still receives half.
		ACommandBuilding* Rig = F.SpawnRig(F.Far, F.Wallets[1], 0);
		ADepositSite* Deposit = F.DepositIn(F.Far);
		if (!T.TestNotNull(TEXT("Rig spawns on the far deposit"), Rig))
			return;
		const int32 Before = Deposit->Remaining;
		F.Pay();
		// Pool: two baselines of 2/s over 2 s (8) plus the rig's 4/s over 2 s (8), split two ways.
		for (const ACommandPlayerState* Wallet : F.Wallets)
			T.TestEqual(TEXT("Each commander gets half the pool, builder or not"), Wallet->Resources, 8);
		T.TestEqual(TEXT("The rig drained its reserve once"), Deposit->Remaining, Before - 8);
		T.TestTrue(TEXT("The estimate is the exact share, 2 + 4/2"), FMath::IsNearlyEqual(F.State->GetPowerRate(F.Wallets[0]), 4., 1.e-9));
		T.TestTrue(TEXT("The non-builder sees the same estimate"), FMath::IsNearlyEqual(F.State->GetPowerRate(F.Wallets[1]), 4., 1.e-9));
		T.TestEqual(TEXT("No Data without reward regions"), F.Wallets[0]->Data, 0);
	}));
	return true;
}

bool FTeamPoolConservationTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 3, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		ADepositSite* Deposit = F.DepositIn(F.Far);
		F.SpawnRig(F.Far, F.Wallets[0], 0);
		const int32 Before = Deposit->Remaining;
		// Pool per payment: three baselines (12) plus the rig (8) is 20, so 6 and 2/3 each.
		for (int32 Payment = 1; Payment <= 30; ++Payment)
		{
			F.Pay();
			int64 Sixtieths = 0;
			for (const ACommandPlayerState* Wallet : F.Wallets)
				Sixtieths += int64(Wallet->Resources) * 60 + Wallet->PowerCarry;
			if (Sixtieths != int64(20) * Payment * 60)
			{
				T.AddError(FString::Printf(TEXT("Payment %d lost or created Power: %lld sixtieths"), Payment, Sixtieths));
				return;
			}
		}
		for (const ACommandPlayerState* Wallet : F.Wallets)
		{
			T.TestEqual(TEXT("Thirty payments leave each commander exactly 200"), Wallet->Resources, 200);
			T.TestEqual(TEXT("With no fraction left over"), Wallet->PowerCarry, 0);
		}
		T.TestEqual(TEXT("The rig drained 8 per payment"), Deposit->Remaining, Before - 240);
	}));
	return true;
}

bool FTeamPoolRosterTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 3, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		F.SpawnRig(F.Far, F.Wallets[0], 0);
		F.Pay(2); // 20 split three ways is 6 and 2/3 per payment.
		ACommandPlayerState* Stayer = F.Wallets[0];
		ACommandPlayerState* Leaver = F.Wallets[2];
		T.TestEqual(TEXT("Two three-way payments give 13 and a 20/60 carry"), Stayer->Resources, 13);
		T.TestEqual(TEXT("The carry is a third"), Stayer->PowerCarry, 20);
		// The leaver's wallet and carry leave with them; two commanders split 16 and the stayer keeps their carry.
		F.State->RemovePlayerState(Leaver);
		F.Pay();
		T.TestEqual(TEXT("Two commanders split 16 evenly"), Stayer->Resources, 21);
		T.TestEqual(TEXT("The stayer's carry survives the roster change"), Stayer->PowerCarry, 20);
		T.TestEqual(TEXT("A departed commander is paid nothing"), Leaver->Resources, 13);
		// A joiner is reset like any new match: starting wallet, no Data, empty carry.
		Leaver->ResetForNewMatch();
		F.State->AddPlayerState(Leaver);
		T.TestEqual(TEXT("A joiner starts with the starting wallet"), Leaver->Resources, ACommandPlayerState::InitialResources);
		T.TestEqual(TEXT("and an empty carry"), Leaver->PowerCarry, 0);
		F.Pay();
		T.TestEqual(TEXT("The joiner is paid a third of 20"), Leaver->Resources, ACommandPlayerState::InitialResources + 6);
		T.TestEqual(TEXT("with a fresh fraction"), Leaver->PowerCarry, 40);
		T.TestEqual(TEXT("The stayer continues from their own carry"), Stayer->Resources, 21 + 7);
		T.TestEqual(TEXT("20/60 + 40/60 makes a whole unit"), Stayer->PowerCarry, 0);
	}));
	return true;
}

bool FTeamPoolReservesTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		int32 Normals = 0;
		for (const ADepositSite* Deposit : F.State->Deposits)
			if (IsValid(Deposit) && !Deposit->bRich)
			{
				++Normals;
				T.TestEqual(TEXT("A normal deposit holds 1200"), Deposit->Remaining, 1200);
			}
		T.TestTrue(TEXT("The map has normal deposits"), Normals > 0);
		ADepositSite* Rich = F.World->SpawnActorDeferred<ADepositSite>(ADepositSite::StaticClass(), FTransform(FVector(0.f, 0.f, 5.f)));
		if (!T.TestNotNull(TEXT("A rich deposit spawns"), Rich))
			return;
		Rich->bRich = true;
		Rich->FinishSpawning(FTransform(FVector(0.f, 0.f, 5.f)));
		T.TestEqual(TEXT("A rich deposit holds 1500"), Rich->Remaining, 1500);
		Rich->Destroy();
	}));
	return true;
}

bool FTeamPoolEmptyRosterTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		ADepositSite* Deposit = F.DepositIn(F.Far);
		F.SpawnRig(F.Far, F.Wallets[0], 0);
		const int32 Before = Deposit->Remaining;
		// A spectator slot is not a recipient: with nobody on the roster the rig neither pays nor drains.
		const int32 Slot = F.Wallets[0]->CommanderIndex;
		F.Wallets[0]->CommanderIndex = -1;
		F.Pay(3);
		F.Wallets[0]->CommanderIndex = Slot;
		T.TestEqual(TEXT("No recipient, no payment"), F.Wallets[0]->Resources, 0);
		T.TestEqual(TEXT("No recipient, no depletion"), Deposit->Remaining, Before);
		T.TestEqual(TEXT("No recipient, no Data"), F.Wallets[0]->Data, 0);
	}));
	return true;
}
#endif
