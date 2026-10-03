#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ProductionPolicy.h"

// Pure rule tests: no world, no actors. Values are arbitrary; assertions are invariants.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProductionPrecedenceTest, "CoopRTS.Rules.Production.Precedence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProductionProgressPreservedTest, "CoopRTS.Rules.Production.ProgressPreserved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProductionDeploymentBoundaryTest, "CoopRTS.Rules.Production.DeploymentBoundary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProductionTerminalFreezeTest, "CoopRTS.Rules.Production.TerminalFreeze",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
constexpr float Duration = 7.5f;
constexpr int32 Capacity = 4;
constexpr int32 UnitCost = 35;

// A live, complete, configured, enabled producer with room, funds and half its work done.
FProductionInput Ready(float Progress = Duration * .5f, float DeltaSeconds = 0.f)
{
	FProductionInput In{};
	In.bMatchOngoing = In.bProducer = In.bComplete = In.bAlive = In.bConfigured = In.bForceValid = In.bEnabled = In.bWalletValid = true;
	In.Joined = 1;
	In.Travelling = 1;
	In.Capacity = Capacity;
	In.Balance = UnitCost;
	In.UnitCost = UnitCost;
	In.Progress = Progress;
	In.Duration = Duration;
	In.DeltaSeconds = DeltaSeconds;
	return In;
}

bool ExpectState(FAutomationTestBase& Test, const TCHAR* What, const FProductionInput& In, EProductionState Expected)
{
	return Test.TestEqual(What, static_cast<int32>(ProductionPolicy::Evaluate(In).State), static_cast<int32>(Expected));
}

// A masked state must neither move work nor ask the adapter to deploy, whatever the tick length.
bool ExpectHeld(FAutomationTestBase& Test, const TCHAR* What, FProductionInput In, EProductionState Expected)
{
	bool bOk = true;
	for (const float Delta : { 0.f, .016f, 60.f })
	{
		In.DeltaSeconds = Delta;
		const FProductionDecision Decision = ProductionPolicy::Evaluate(In);
		bOk &= Test.TestEqual(What, static_cast<int32>(Decision.State), static_cast<int32>(Expected));
		bOk &= Test.TestTrue(FString::Printf(TEXT("%s: progress held at %g for dt=%g"), What, In.Progress, Delta),
			Decision.NewProgress == In.Progress);
		bOk &= Test.TestFalse(FString::Printf(TEXT("%s: no deployment for dt=%g"), What, Delta), Decision.bDeploymentDue);
	}
	return bOk;
}
}

bool FProductionPrecedenceTest::RunTest(const FString& Parameters)
{
	// Every gate fails at once; clearing them one at a time walks the ladder in order,
	// so each state is proven to mask everything below it.
	FProductionInput In = Ready(Duration);
	In.bMatchOngoing = In.bProducer = In.bComplete = In.bAlive = In.bConfigured = In.bForceValid = In.bEnabled = In.bWalletValid = false;
	In.Joined = Capacity;
	In.Balance = UnitCost - 1;
	ExpectHeld(*this, TEXT("Finished match masks everything"), In, EProductionState::MatchFinished);
	In.bMatchOngoing = true;
	ExpectHeld(*this, TEXT("Non-producer masks construction"), In, EProductionState::NotProducer);
	In.bProducer = true;
	ExpectHeld(*this, TEXT("Dead producer is not a producer"), In, EProductionState::NotProducer);
	In.bAlive = true;
	ExpectHeld(*this, TEXT("Construction masks configuration"), In, EProductionState::UnderConstruction);
	In.bComplete = true;
	ExpectHeld(*this, TEXT("Configuration masks force validity"), In, EProductionState::Unconfigured);
	In.bConfigured = true;
	ExpectHeld(*this, TEXT("Missing force masks pause"), In, EProductionState::ForceUnavailable);
	In.bForceValid = true;
	ExpectHeld(*this, TEXT("Paused beats full, broke and blocked"), In, EProductionState::Paused);
	In.bEnabled = true;
	ExpectHeld(*this, TEXT("Full beats wallet, funds and blocked"), In, EProductionState::ForceComplete);
	In.Joined = Capacity - 1;
	In.Travelling = 0;
	ExpectHeld(*this, TEXT("Wallet validity beats funds and blocked"), In, EProductionState::WalletUnavailable);
	In.bWalletValid = true;
	ExpectHeld(*this, TEXT("Funds beat blocked"), In, EProductionState::InsufficientResources);
	In.Balance = UnitCost;
	ExpectState(*this, TEXT("Completed work with nothing else wrong is blocked deployment"), In, EProductionState::DeploymentBlocked);
	In.Progress = 0.f;
	ExpectState(*this, TEXT("Nothing wrong and work remaining is producing"), In, EProductionState::Producing);

	// Travellers count against capacity exactly like joined members.
	FProductionInput Travellers = Ready();
	Travellers.Joined = 0;
	Travellers.Travelling = Capacity;
	ExpectState(*this, TEXT("Travellers alone fill a force"), Travellers, EProductionState::ForceComplete);
	Travellers.Travelling = Capacity - 1;
	ExpectState(*this, TEXT("One vacancy re-opens production"), Travellers, EProductionState::Producing);

	// Exactly the unit price is enough; one short is not.
	FProductionInput Funds = Ready();
	Funds.Balance = UnitCost - 1;
	ExpectState(*this, TEXT("One resource short starves"), Funds, EProductionState::InsufficientResources);
	Funds.Balance = UnitCost;
	ExpectState(*this, TEXT("Exact price affords a unit"), Funds, EProductionState::Producing);
	return true;
}

bool FProductionProgressPreservedTest::RunTest(const FString& Parameters)
{
	for (const float Progress : { 0.f, Duration * .5f, Duration })
	{
		FProductionInput Paused = Ready(Progress);
		Paused.bEnabled = false;
		ExpectHeld(*this, TEXT("Pause keeps work"), Paused, EProductionState::Paused);

		FProductionInput Full = Ready(Progress);
		Full.Joined = Capacity;
		ExpectHeld(*this, TEXT("Full force keeps work"), Full, EProductionState::ForceComplete);

		FProductionInput NoWallet = Ready(Progress);
		NoWallet.bWalletValid = false;
		ExpectHeld(*this, TEXT("Missing wallet keeps work"), NoWallet, EProductionState::WalletUnavailable);

		FProductionInput Starved = Ready(Progress);
		Starved.Balance = UnitCost - 1;
		ExpectHeld(*this, TEXT("Starvation keeps work"), Starved, EProductionState::InsufficientResources);
	}

	// Producing advances by exactly the tick, never past the duration, never backwards.
	const FProductionDecision Step = ProductionPolicy::Evaluate(Ready(1.f, .25f));
	TestTrue(TEXT("Producing advances by the tick"), Step.NewProgress == 1.25f);
	TestFalse(TEXT("Partial work is not due"), Step.bDeploymentDue);
	const FProductionDecision Clamped = ProductionPolicy::Evaluate(Ready(1.f, Duration * 10.f));
	TestTrue(TEXT("Progress clamps to the duration"), Clamped.NewProgress == Duration);
	const FProductionDecision Idle = ProductionPolicy::Evaluate(Ready(1.f, 0.f));
	TestTrue(TEXT("Zero tick leaves work untouched"), Idle.NewProgress == 1.f);
	const FProductionDecision Reversed = ProductionPolicy::Evaluate(Ready(1.f, -1.f));
	TestTrue(TEXT("Negative tick never regresses work"), Reversed.NewProgress == 1.f);
	return true;
}

bool FProductionDeploymentBoundaryTest::RunTest(const FString& Parameters)
{
	const float Remaining = Duration - Duration * .5f;
	const FProductionDecision Under = ProductionPolicy::Evaluate(Ready(Duration * .5f, Remaining * .999f));
	TestEqual(TEXT("Just short of the duration is still producing"), static_cast<int32>(Under.State), static_cast<int32>(EProductionState::Producing));
	TestTrue(TEXT("Just short of the duration is below it"), Under.NewProgress < Duration);
	TestFalse(TEXT("Just short of the duration is not due"), Under.bDeploymentDue);

	const FProductionDecision Exact = ProductionPolicy::Evaluate(Ready(Duration * .5f, Remaining));
	TestEqual(TEXT("Reaching the duration this tick is still producing"), static_cast<int32>(Exact.State), static_cast<int32>(EProductionState::Producing));
	TestTrue(TEXT("Reaching the duration lands exactly on it"), Exact.NewProgress == Duration);
	TestTrue(TEXT("Reaching the duration is due"), Exact.bDeploymentDue);

	const FProductionDecision Over = ProductionPolicy::Evaluate(Ready(Duration * .5f, Remaining * 3.f));
	TestTrue(TEXT("Overshoot clamps to the duration"), Over.NewProgress == Duration);
	TestTrue(TEXT("Overshoot is due"), Over.bDeploymentDue);

	// Work already complete: no further advance, but every tick retries deployment.
	for (const float Delta : { 0.f, .016f, 60.f })
	{
		const FProductionDecision Blocked = ProductionPolicy::Evaluate(Ready(Duration, Delta));
		TestEqual(TEXT("Completed work is blocked deployment"), static_cast<int32>(Blocked.State), static_cast<int32>(EProductionState::DeploymentBlocked));
		TestTrue(TEXT("Blocked deployment keeps completed work"), Blocked.NewProgress == Duration);
		TestTrue(TEXT("Blocked deployment retries"), Blocked.bDeploymentDue);
	}
	return true;
}

bool FProductionTerminalFreezeTest::RunTest(const FString& Parameters)
{
	// A finished match freezes producers regardless of how ready they are, including completed work.
	for (const float Progress : { 0.f, Duration * .5f, Duration })
	{
		FProductionInput Finished = Ready(Progress);
		Finished.bMatchOngoing = false;
		ExpectHeld(*this, TEXT("Finished match freezes production"), Finished, EProductionState::MatchFinished);

		FProductionInput Dead = Ready(Progress);
		Dead.bAlive = false;
		ExpectHeld(*this, TEXT("Destroyed producer freezes production"), Dead, EProductionState::NotProducer);

		FProductionInput Lost = Ready(Progress);
		Lost.bForceValid = false;
		ExpectHeld(*this, TEXT("Lost force freezes production"), Lost, EProductionState::ForceUnavailable);
	}
	return true;
}

#endif
