#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "EnemyCommander.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPauseBudgetTest, "CoopRTS.Rules.PauseBudget",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPauseBudgetTest::RunTest(const FString& Parameters)
{
	FPauseBudget Coop;
	TestTrue(TEXT("Fresh battle can pause"), Coop.CanPause(true));
	Coop.Begin(true, 100.);
	TestFalse(TEXT("An active pause cannot be restarted"), Coop.CanPause(true));
	TestEqual(TEXT("Co-op starts with exactly sixty real seconds"), Coop.Remaining(100.), 60.);
	TestEqual(TEXT("Countdown advances while simulation time is frozen"), Coop.Remaining(159.5), .5);
	TestFalse(TEXT("Pause lasts through the last fraction of a second"), Coop.Expired(159.999));
	TestTrue(TEXT("Deadline expires at sixty seconds"), Coop.Expired(160.));
	TestEqual(TEXT("Late countdown never goes negative"), Coop.Remaining(170.), 0.);
	Coop.Resume();
	TestFalse(TEXT("Expiry cannot refund the shared pause"), Coop.CanPause(true));
	FPauseBudget Early;
	Early.Begin(true, 100.);
	Early.Resume();
	TestFalse(TEXT("Early resume cannot refund the shared pause"), Early.CanPause(true));
	FPauseBudget Solo;
	Solo.Begin(false, 100.);
	TestFalse(TEXT("Solo does not auto-expire"), Solo.Expired(10000.));
	Solo.Resume();
	TestTrue(TEXT("Solo can pause again"), Solo.CanPause(false));
	TestFalse(TEXT("Solo never spends the co-op budget"), Solo.bSpent);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSoloPauseTest, "CoopRTS.Pause.SoloActive",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

class FSoloPauseScenario : public IAutomationLatentCommand
{
public:
	explicit FSoloPauseScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
	bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 25.)
			return Fail(TEXT("Solo pause scenario timed out"));
		if (!State.IsValid())
		{
			UWorld* World = ArmyTestSetup::World();
			if (!World)
				return false;
			ACommandGameState* Match = World->GetGameState<ACommandGameState>();
			ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
			if (!ArmyTestSetup::MapReady(Match) || !PC || !PC->GetPlayerState<ACommandPlayerState>()
				|| PC->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0)
				return false;
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->Destroy();
			for (ACommandBuilding* Building : Match->Buildings)
				if (IsValid(Building) && Building->TeamIndex == 5 && Building->IsProducer())
					FCommandService::ConfigureProduction(Match->EnemyCommander, Building,
						Building->bForceConfigured ? Building->ProductionRole : static_cast<EUnitRole>(255), false);
			State = Match;
			Controller = PC;
			Army = ArmyTestSetup::SpawnGroup(World, PC, 0, ArmyTestSetup::FromFriendlyHQ(Match, 1700.f, 600.f, 100.f));
			if (!Army.IsValid())
				return Fail(TEXT("Pause fixture army could not spawn"));
			Destination = Army->GetHomeLocation() + FVector(0.f, 1200.f, 0.f);
			StageStarted = Now;
			return false;
		}
		if (!Controller.IsValid() || !Army.IsValid())
			return Fail(TEXT("Pause fixture disappeared"));
		UWorld* World = State->GetWorld();
		ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		if (Stage == 0 && Now - StageStarted >= 3.)
		{
			// Wait for real navmesh readiness before pausing; path queries still run while paused.
			if (!FCommandService::IssueOrder(Wallet, Army.Get(), EArmyOrder::Move, Destination))
				return false;
			FCommandService::IssueOrder(Wallet, Army.Get(), EArmyOrder::Hold, Army->GetCenter());
			Controller->ServerPause();
			Test->TestFalse(TEXT("Engine pause RPC cannot bypass the command budget"), World->IsPaused());
			if (!FCommandService::Pause(Controller.Get()))
				return Fail(TEXT("Solo pause command rejected"));
			Test->TestTrue(TEXT("Pause freezes the actual world with game UI still open"), World->IsPaused() && State->IsActivePaused() && Controller->GetUIScreen() == ECommandScreen::Game);
			Controller->ServerPause();
			Test->TestTrue(TEXT("Engine pause RPC cannot resume active pause"), World->IsPaused());
			SimulationTime = World->GetTimeSeconds();
			Balance = Wallet->Resources;
			Center = Army->GetCenter();
			if (!FCommandService::IssueOrder(Wallet, Army.Get(), EArmyOrder::Move, Destination))
				return Fail(TEXT("Order given during pause rejected"));
			Test->TestTrue(TEXT("Order intent applies immediately while paused"), Army->Order == EArmyOrder::Move && FVector::Dist2D(Army->Destination, Destination) < 100.);
			Next(Now);
		}
		else if (Stage == 1 && Now - StageStarted >= 1.)
		{
			Test->TestEqual(TEXT("Simulation clock stays frozen across real time"), World->GetTimeSeconds(), SimulationTime);
			Test->TestEqual(TEXT("Income stops while paused"), Wallet->Resources, Balance);
			Test->TestTrue(TEXT("Paused order cannot move characters"), FVector::Dist2D(Army->GetCenter(), Center) < 1.);
			State->RefreshSoloMenuPause(Controller.Get(), true);
			State->RefreshSoloMenuPause(Controller.Get(), false);
			Test->TestTrue(TEXT("Closing menu preserves active pause"), World->IsPaused() && State->IsActivePaused());
			if (!FCommandService::Resume(Controller.Get()))
				return Fail(TEXT("Solo resume command rejected"));
			Next(Now);
		}
		else if (Stage == 2 && World->GetTimeSeconds() >= SimulationTime + 2.)
		{
			Test->TestFalse(TEXT("Resume clears world pause"), World->IsPaused());
			Test->TestTrue(TEXT("Orders given paused physically execute after resume"), FVector::Dist2D(Army->GetCenter(), Destination) + 100. < FVector::Dist2D(Center, Destination));
			Test->TestTrue(TEXT("Income resumes with simulation"), Wallet->Resources > Balance);
			Test->TestTrue(TEXT("Solo pause is reusable"), FCommandService::Pause(Controller.Get()).IsAccepted());
			Test->TestTrue(TEXT("Second solo resume succeeds"), FCommandService::Resume(Controller.Get()).IsAccepted());
			return true;
		}
		return false;
	}
private:
	bool Fail(const TCHAR* Reason)
	{
		Test->AddError(Reason);
		if (Controller.IsValid() && State.IsValid() && State->IsActivePaused())
			FCommandService::Resume(Controller.Get());
		return true;
	}
	void Next(double Now)
	{
		++Stage;
		StageStarted = Now;
	}
	FAutomationTestBase* Test;
	TWeakObjectPtr<ACommandGameState> State;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Army;
	double Started;
	double StageStarted = 0.;
	double SimulationTime = 0.;
	int32 Stage = 0;
	int32 Balance = 0;
	FVector Destination = FVector::ZeroVector;
	FVector Center = FVector::ZeroVector;
};

bool FSoloPauseTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSoloPauseScenario(this));
	return true;
}

#endif
