#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevScalingTest, "CoopRTS.Economy.JevScaling",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
class FJevScalingScenario : public IAutomationLatentCommand
{
public:
	explicit FJevScalingScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 45.)
			return Fail(TEXT("JEV scaling scenario exceeded 45 seconds"));
		UWorld* World = ArmyTestSetup::World();
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		ACommandPlayerController* PC = World ? ArmyTestSetup::Controller(World) : nullptr;
		ACommandPlayerState* Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!ArmyTestSetup::MapReady(State) || !Wallet || Wallet->CommanderIndex < 0
			|| !IsValid(State->EnemyCommander))
			return false;
		if (Stage == 0)
		{
			// Only the real game-state income tick may change these wallets.
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->Destroy();
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				It->Destroy();
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				It->Destroy();
			Other = World->SpawnActor<ACommandPlayerState>();
			InvalidSlot = World->SpawnActor<ACommandPlayerState>();
			if (!Other.IsValid() || !InvalidSlot.IsValid())
				return Fail(TEXT("Roster fixtures could not spawn"));
			Other->CommanderIndex = -1; // A spectator is not a human commander.
			InvalidSlot->CommanderIndex = 5;
			Other->Resources = InvalidSlot->Resources = 0;
			State->AddPlayerState(Other.Get());
			State->AddPlayerState(InvalidSlot.Get());
			Wallet->Resources = State->EnemyCommander->Resources = 0;
			Stage = 1;
			return false;
		}
		if (!Other.IsValid() || !InvalidSlot.IsValid()
			|| State->GetIncomePerSecond(Wallet) != 2 || InvalidSlot->Resources != 0)
			return Fail(TEXT("Human income stays unscaled; invalid slots receive no baseline"));
		const double ExpectedRate = Stage == 1 || Stage == 3 ? 2. : 2.6;
		if (!FMath::IsNearlyEqual(State->GetEnemyBaselineIncomePerSecond(), ExpectedRate, 1.e-12))
			return Fail(TEXT("Exact JEV baseline rate follows the current commander roster"));
		if (Wallet->Resources == HumanExpected)
		{
			if (Other->Resources != OtherExpected)
				return Fail(TEXT("No baseline is paid between the host's two-second payments"));
			return false;
		}
		HumanExpected += 4;
		if (Wallet->Resources != HumanExpected)
			return Fail(TEXT("Human commander receives exactly four per two-second income payment"));
		switch (Stage)
		{
		case 1:
			if (State->EnemyCommander->Resources != 4)
				return Fail(TEXT("One commander plus spectators leaves JEV at the solo baseline"));
			Other->CommanderIndex = Wallet->CommanderIndex == 0 ? 1 : 0;
			Stage = 2;
			break;
		case 2:
			OtherExpected += 4;
			if (State->EnemyCommander->Resources != 9 || Other->Resources != OtherExpected)
				return Fail(TEXT("Two commanders pay JEV five with a carried fifth of Power, not the flat four"));
			State->RemovePlayerState(Other.Get());
			Stage = 3;
			break;
		case 3:
			if (State->EnemyCommander->Resources != 13 || Other->Resources != OtherExpected)
				return Fail(TEXT("A departing commander restores solo payment without losing the fraction"));
			State->AddPlayerState(Other.Get());
			Stage = 4;
			break;
		case 4:
			OtherExpected += 4;
			++RejoinedPayments;
			if (State->EnemyCommander->Resources != 13 + 5 * RejoinedPayments + (RejoinedPayments == 4 ? 1 : 0)
				|| Other->Resources != OtherExpected)
				return Fail(TEXT("Rejoining restores scaling and retains the pre-departure fractional remainder"));
			if (RejoinedPayments == 4)
			{
				Test->AddInfo(TEXT("JEV received 26 Power across five two-commander payments (2.6/s); two solo payments added eight. Human payments stayed four, and spectator/invalid slots paid zero."));
				return true;
			}
			break;
		}
		return false;
	}

private:
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		return true;
	}

	FAutomationTestBase* Test;
	double Started;
	int32 Stage = 0, HumanExpected = 0, OtherExpected = 0, RejoinedPayments = 0;
	TWeakObjectPtr<ACommandPlayerState> Other, InvalidSlot;
};
}

bool FJevScalingTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevScalingScenario(this));
	return true;
}

#endif
