#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "TeamEconomyFixture.h"

// Data from structure kills, atomic mixed-cost spending, and gifts between commanders.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamStructureKillTest, "CoopRTS.Economy.Team.StructureKillData",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamMixedSpendTest, "CoopRTS.Economy.Team.MixedSpend",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamGiftTest, "CoopRTS.Economy.Team.Gifts",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamGiftLogTest, "CoopRTS.Economy.Team.GiftLog",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTeamStructureKillTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		AArmyUnit* Attacker = F.SpawnAttacker();
		if (!T.TestNotNull(TEXT("A friendly attacker spawns"), Attacker))
			return;
		ACommandBuilding* Barracks = F.SpawnBarracks(5, 1.f, 0);
		ACommandBuilding* Rig = F.SpawnRig(F.Far, nullptr, 5);
		ACommandBuilding* Building = F.SpawnBarracks(5, .5f, 1);
		ACommandBuilding* Cancelled = F.SpawnBarracks(5, .3f, 2);
		ACommandBuilding* Friendly = F.SpawnBarracks(0, 1.f, 3);
		if (!T.TestTrue(TEXT("The fixture buildings spawn"), Barracks && Rig && Building && Cancelled && Friendly))
			return;
		F.Step();
		// A finished JEV Barracks pays 60 Data into the pool, 30 to each of two commanders, exactly once.
		Barracks->ReceiveAttack(Barracks->Health, Attacker);
		T.TestFalse(TEXT("The Barracks is destroyed"), IsValid(Barracks));
		F.Step();
		T.TestEqual(TEXT("The first commander gets half of 60"), F.Wallets[0]->Data, 30);
		T.TestEqual(TEXT("The second commander gets half of 60"), F.Wallets[1]->Data, 30);
		F.Step();
		F.Pay();
		T.TestEqual(TEXT("The kill pays once, however long the building is gone"), F.Wallets[0]->Data, 30);
		T.TestEqual(TEXT("and pays no Power"), F.Wallets[0]->Resources, 4);
		// A finished JEV Drill Rig counts too, and its kill frees the deposit.
		Rig->ReceiveAttack(Rig->Health, Attacker);
		F.Step();
		T.TestEqual(TEXT("A finished Drill Rig pays 60 Data"), F.Wallets[0]->Data, 60);
		// A building still under construction pays nothing when destroyed.
		Building->ReceiveAttack(Building->Health, Attacker);
		F.Step();
		T.TestEqual(TEXT("An unfinished building pays no Data"), F.Wallets[0]->Data, 60);
		// A cancelled building pays nothing.
		T.TestTrue(TEXT("JEV cancels its unfinished Barracks"), FCommandService::CancelBuilding(F.State->EnemyCommander, Cancelled).IsAccepted());
		F.Step();
		T.TestEqual(TEXT("A cancelled building pays no Data"), F.Wallets[0]->Data, 60);
		// A human building is not a JEV structure.
		Friendly->ReceiveAttack(Friendly->Health, F.SpawnHostileIn(F.Neck));
		F.Step();
		T.TestEqual(TEXT("Losing our own building pays no Data"), F.Wallets[0]->Data, 60);
		T.TestEqual(TEXT("JEV earns no Data from its kills"), F.State->EnemyCommander->Data, 0);
	}));
	return true;
}

bool FTeamMixedSpendTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		ACommandPlayerState* Wallet = F.Wallets[0];
		Wallet->Resources = 100;
		Wallet->Data = 49;
		T.TestFalse(TEXT("49 Data cannot pay 100 Power + 50 Data"), Wallet->TrySpend(FResourceCost{ 100, 50 }));
		T.TestEqual(TEXT("A rejected spend leaves Power untouched"), Wallet->Resources, 100);
		T.TestEqual(TEXT("and Data untouched"), Wallet->Data, 49);
		Wallet->Data = 50;
		Wallet->Resources = 99;
		T.TestFalse(TEXT("99 Power cannot pay 100 Power + 50 Data"), Wallet->TrySpend(FResourceCost{ 100, 50 }));
		T.TestTrue(TEXT("Both untouched"), Wallet->Resources == 99 && Wallet->Data == 50);
		Wallet->Resources = 100;
		T.TestTrue(TEXT("Exact balances pay both"), Wallet->TrySpend(FResourceCost{ 100, 50 }));
		T.TestTrue(TEXT("and both are debited"), Wallet->Resources == 0 && Wallet->Data == 0);
		Wallet->Data = 40;
		T.TestTrue(TEXT("A Data-only price spends only Data"), Wallet->TrySpend(FResourceCost{ 0, 40 }));
		T.TestEqual(TEXT("Data is debited"), Wallet->Data, 0);
		Wallet->Data = 7;
		Wallet->DataCarry = 30;
		Wallet->ResetForNewMatch();
		T.TestTrue(TEXT("A new match clears Data and both carries"), Wallet->Data == 0 && Wallet->DataCarry == 0 && Wallet->PowerCarry == 0);
		T.TestEqual(TEXT("and restores the starting Power"), Wallet->Resources, ACommandPlayerState::InitialResources);
	}));
	return true;
}

bool FTeamGiftTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		ACommandPlayerState* From = F.Wallets[0];
		ACommandPlayerState* To = F.Wallets[1];
		ACommandPlayerState* Outsider = F.Spawn(4, false);
		From->Resources = 300;
		From->Data = 80;
		To->Resources = 5;
		T.TestTrue(TEXT("A Power gift is accepted"), FCommandService::Gift(From, To, EEconomyResource::Power, 100).IsAccepted());
		T.TestTrue(TEXT("Power moved atomically"), From->Resources == 200 && To->Resources == 105);
		T.TestTrue(TEXT("A Data gift is accepted"), FCommandService::Gift(From, To, EEconomyResource::Data, 80).IsAccepted());
		T.TestTrue(TEXT("The whole Data balance moved"), From->Data == 0 && To->Data == 80);
		T.TestEqual(TEXT("Both gifts are logged"), F.State->GiftLog.Num(), 2);
		if (F.State->GiftLog.Num() == 2)
		{
			const FGiftLogEntry& Entry = F.State->GiftLog[1];
			T.TestTrue(TEXT("The entry names sender, recipient, resource and amount"),
				Entry.SenderSlot == From->CommanderIndex && Entry.RecipientSlot == To->CommanderIndex
					&& Entry.Resource == EEconomyResource::Data && Entry.Amount == 80);
		}
		// Every rejection changes nothing and logs nothing.
		const int32 Logged = F.State->GiftLog.Num();
		const auto Rejected = [&](const TCHAR* Why, FCommandResult Result) {
			T.TestFalse(Why, Result.IsAccepted());
			T.TestTrue(FString(Why) + TEXT(" explains itself"), !Result.Message.IsEmpty());
			T.TestTrue(FString(Why) + TEXT(" changes no balance"), From->Resources == 200 && To->Resources == 105 && From->Data == 0 && To->Data == 80);
			T.TestEqual(FString(Why) + TEXT(" is not logged"), F.State->GiftLog.Num(), Logged);
		};
		Rejected(TEXT("A gift to oneself"), FCommandService::Gift(From, From, EEconomyResource::Power, 10));
		Rejected(TEXT("A gift to the enemy"), FCommandService::Gift(From, F.State->EnemyCommander, EEconomyResource::Power, 10));
		Rejected(TEXT("A gift to a commander outside the roster"), FCommandService::Gift(From, Outsider, EEconomyResource::Power, 10));
		Rejected(TEXT("A gift to nobody"), FCommandService::Gift(From, nullptr, EEconomyResource::Power, 10));
		Rejected(TEXT("An overdraft"), FCommandService::Gift(From, To, EEconomyResource::Power, 201));
		Rejected(TEXT("An overdraft of Data"), FCommandService::Gift(From, To, EEconomyResource::Data, 1));
		Rejected(TEXT("A zero gift"), FCommandService::Gift(From, To, EEconomyResource::Power, 0));
		Rejected(TEXT("A negative gift"), FCommandService::Gift(To, From, EEconomyResource::Power, -50));
		Rejected(TEXT("A gift from the enemy"), FCommandService::Gift(F.State->EnemyCommander, To, EEconomyResource::Power, 1));
		Rejected(TEXT("A gift from outside the roster"), FCommandService::Gift(Outsider, To, EEconomyResource::Power, 1));
		To->Resources = MAX_int32 - 50;
		T.TestFalse(TEXT("A gift that would overflow the wallet is refused"), FCommandService::Gift(From, To, EEconomyResource::Power, 100).IsAccepted());
		T.TestTrue(TEXT("and nothing is lost"), From->Resources == 200 && To->Resources == MAX_int32 - 50);
		To->Resources = 105;
		// A gift after the battle ended is refused.
		F.State->MatchResult = EMatchResult::Victory;
		T.TestFalse(TEXT("No gifts once the battle is over"), FCommandService::Gift(From, To, EEconomyResource::Power, 10).IsAccepted());
		F.State->MatchResult = EMatchResult::Ongoing;
		Outsider->Destroy();
	}));
	return true;
}

bool FTeamGiftLogTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		F.Wallets[0]->Resources = 1000;
		for (int32 Amount = 1; Amount <= 25; ++Amount)
			if (!FCommandService::Gift(F.Wallets[0], F.Wallets[1], EEconomyResource::Power, Amount).IsAccepted())
			{
				T.AddError(FString::Printf(TEXT("Gift %d was refused"), Amount));
				return;
			}
		T.TestEqual(TEXT("The log keeps the last 20 gifts"), F.State->GiftLog.Num(), 20);
		if (F.State->GiftLog.Num() == 20)
		{
			T.TestEqual(TEXT("The oldest kept gift is the sixth"), F.State->GiftLog[0].Amount, 6);
			T.TestEqual(TEXT("The newest is the last"), F.State->GiftLog[19].Amount, 25);
		}
		T.TestEqual(TEXT("All 25 gifts moved Power"), F.Wallets[1]->Resources, 325);
	}));
	return true;
}
#endif
