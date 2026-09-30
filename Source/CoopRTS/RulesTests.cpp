#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Rules/ProductionPolicy.h"
#include "Rules/PlacementPolicy.h"
#include "Rules/EconomyPolicy.h"
#include "Rules/OutcomePolicy.h"
#include "CommandGameState.h" // EMatchResult is declared in this pinned header; no actors are instantiated.

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
		for (const float Delta : {0.f, .016f, 60.f})
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
	for (const float Progress : {0.f, Duration * .5f, Duration})
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
	for (const float Delta : {0.f, .016f, 60.f})
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
	for (const float Progress : {0.f, Duration * .5f, Duration})
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementTerritoryTest, "CoopRTS.Rules.Placement.Territory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementOutpostTest, "CoopRTS.Rules.Placement.Outpost",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementPrecedenceTest, "CoopRTS.Rules.Placement.Precedence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementHeadquartersTest, "CoopRTS.Rules.Placement.HeadquartersBoundaries",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementProximityTest, "CoopRTS.Rules.Placement.ProximityBoundaries",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyIncomeTest, "CoopRTS.Rules.Economy.IncomeAndSaturation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyRefundTest, "CoopRTS.Rules.Economy.Refund",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyAffordabilityTest, "CoopRTS.Rules.Economy.Affordability",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutcomeHealthTest, "CoopRTS.Rules.Outcome.HealthAndTies",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	FPlacementInput PlacementReady()
	{
		FPlacementInput In{};
		In.Team = 0;
		In.FootprintRadius = 80.f;
		In.TerritoryRadius = 1200.f;
		In.HomePosition = FVector::ZeroVector;
		In.HostilePosition = FVector(8000.f, 0.f, 0.f);
		In.Position = FVector(PlacementPolicy::HomeTerritoryRadius * .7f, 0.f, 0.f);
		In.bInsidePlacementBounds = In.bHeadquartersAvailable = true;
		return In;
	}

	void ExpectPlacement(FAutomationTestBase& Test, const TCHAR* What, const FPlacementInput& In, EPlacementVerdict Expected)
	{
		Test.TestEqual(What, static_cast<int32>(PlacementPolicy::Evaluate(In).Verdict), static_cast<int32>(Expected));
	}
}

bool FPlacementTerritoryTest::RunTest(const FString& Parameters)
{
	FPlacementInput In = PlacementReady();
	ExpectPlacement(*this, TEXT("Home territory needs no outpost"), In, EPlacementVerdict::Valid);
	In.Position = FVector(3000.f, 0.f, 0.f);
	ExpectPlacement(*this, TEXT("Outside home needs established territory"), In, EPlacementVerdict::TerritoryRequired);
	TArray<FPlacementSector> Sectors = { { In.Position, In.Team, false, false, false, false } };
	In.Sectors = Sectors;
	ExpectPlacement(*this, TEXT("Captured alone does not establish territory"), In, EPlacementVerdict::TerritoryRequired);
	Sectors[0].bEstablishedForTeam = true;
	ExpectPlacement(*this, TEXT("Established friendly sector extends home"), In, EPlacementVerdict::Valid);
	In.Position.X += In.TerritoryRadius - In.FootprintRadius;
	ExpectPlacement(*this, TEXT("Sector boundary includes entire footprint"), In, EPlacementVerdict::Valid);
	In.Position.X += 1.f;
	ExpectPlacement(*this, TEXT("Footprint beyond sector boundary rejected"), In, EPlacementVerdict::TerritoryRequired);
	In.Position = Sectors[0].Position;
	Sectors[0].ControllingTeam = 5;
	ExpectPlacement(*this, TEXT("Enemy establishment is not home"), In, EPlacementVerdict::TerritoryRequired);
	In.Team = 5;
	ExpectPlacement(*this, TEXT("Same territory rules apply to enemy team"), In, EPlacementVerdict::Valid);
	Sectors[0].bFriendlyPresent = true;
	ExpectPlacement(*this, TEXT("Human presence contests enemy territory"), In, EPlacementVerdict::Contested);
	return true;
}

bool FPlacementOutpostTest::RunTest(const FString& Parameters)
{
	FPlacementInput In = PlacementReady();
	In.bSectorBuilding = true;
	ExpectPlacement(*this, TEXT("Home alone cannot host a sector building"), In, EPlacementVerdict::CaptureRequired);
	In.Position = FVector(3000.f, 0.f, 0.f);
	TArray<FPlacementSector> Sectors = {
		{ In.Position + FVector(100.f, 0.f, 0.f), -1, false, false, false, false },
		{ In.Position + FVector(200.f, 0.f, 0.f), In.Team, false, false, false, false } };
	In.Sectors = Sectors;
	const FPlacementDecision Captured = PlacementPolicy::Evaluate(In);
	TestEqual(TEXT("Captured sector permits an outpost before establishment"),
		static_cast<int32>(Captured.Verdict), static_cast<int32>(EPlacementVerdict::Valid));
	TestEqual(TEXT("Uncaptured nearer sector is ignored"), Captured.TargetSectorIndex, 1);
	Sectors[1].bHasOutpost = true;
	ExpectPlacement(*this, TEXT("Existing outpost rejects another"), In, EPlacementVerdict::OutpostExists);
	Sectors[0].ControllingTeam = In.Team;
	TestEqual(TEXT("Nearest eligible sector selected"), PlacementPolicy::Evaluate(In).TargetSectorIndex, 0);
	Sectors[0].Position = Sectors[1].Position;
	TestEqual(TEXT("Equal distances retain first sector"), PlacementPolicy::Evaluate(In).TargetSectorIndex, 0);
	Sectors[0].Position = In.Position + FVector(In.TerritoryRadius - In.FootprintRadius, 0.f, 0.f);
	Sectors[1].ControllingTeam = -1;
	ExpectPlacement(*this, TEXT("Sector boundary admits an outpost"), In, EPlacementVerdict::Valid);
	Sectors[0].Position.X += 1.f;
	const FPlacementDecision Outside = PlacementPolicy::Evaluate(In);
	TestEqual(TEXT("No sector beyond boundary"), Outside.TargetSectorIndex, INDEX_NONE);
	TestEqual(TEXT("Outpost requires captured sector in range"),
		static_cast<int32>(Outside.Verdict), static_cast<int32>(EPlacementVerdict::CaptureRequired));
	return true;
}

bool FPlacementPrecedenceTest::RunTest(const FString& Parameters)
{
	FPlacementInput In = PlacementReady();
	TArray<FPlacementSector> Sectors = { { In.Position, In.Team, true, false, true, true } };
	TArray<FPlacementBuilding> Buildings = { { In.Position, 70.f, true } };
	TArray<FVector> Troops = { In.Position };
	In.Sectors = Sectors;
	In.Buildings = Buildings;
	In.EnemyTroops = Troops;
	In.Team = -1;
	In.bInsidePlacementBounds = In.bHeadquartersAvailable = false;
	ExpectPlacement(*this, TEXT("Invalid team masks bounds"), In, EPlacementVerdict::Invalid);
	In.Team = 0;
	ExpectPlacement(*this, TEXT("Bounds mask unavailable headquarters"), In, EPlacementVerdict::OutsideBounds);
	In.bInsidePlacementBounds = true;
	ExpectPlacement(*this, TEXT("Unavailable headquarters mask contention"), In, EPlacementVerdict::HeadquartersUnavailable);
	In.bHeadquartersAvailable = true;
	In.HostilePosition = In.Position;
	ExpectPlacement(*this, TEXT("Hostile HQ exclusion masks contention"), In, EPlacementVerdict::EnemyHeadquartersTooClose);
	In.HostilePosition = FVector(8000.f, 0.f, 0.f);
	ExpectPlacement(*this, TEXT("Contested beats home and established territory"), In, EPlacementVerdict::Contested);
	In.bSectorBuilding = true;
	ExpectPlacement(*this, TEXT("Contested beats existing outpost"), In, EPlacementVerdict::Contested);
	Sectors[0].bEnemyPresent = false;
	ExpectPlacement(*this, TEXT("Existing outpost beats troop proximity"), In, EPlacementVerdict::OutpostExists);
	In.bSectorBuilding = false;
	ExpectPlacement(*this, TEXT("Troops beat building overlap"), In, EPlacementVerdict::EnemyTroopsTooClose);
	In.EnemyTroops = {};
	ExpectPlacement(*this, TEXT("Overlap follows troops"), In, EPlacementVerdict::BuildingOverlap);
	In.Position = In.HomePosition;
	Buildings[0].Position = In.Position;
	ExpectPlacement(*this, TEXT("Overlap beats home HQ clearance"), In, EPlacementVerdict::BuildingOverlap);
	In.Buildings = {};
	ExpectPlacement(*this, TEXT("Home HQ clearance follows overlap"), In, EPlacementVerdict::HeadquartersTooClose);
	In.Position = FVector(3000.f, 0.f, 0.f);
	Troops[0] = In.Position;
	In.EnemyTroops = Troops;
	In.Sectors = {};
	ExpectPlacement(*this, TEXT("Missing territory beats troop proximity"), In, EPlacementVerdict::TerritoryRequired);
	return true;
}

bool FPlacementHeadquartersTest::RunTest(const FString& Parameters)
{
	FPlacementInput In = PlacementReady();
	In.Position = FVector(PlacementPolicy::HomeTerritoryRadius - In.FootprintRadius, 0.f, 0.f);
	ExpectPlacement(*this, TEXT("Home boundary inclusive"), In, EPlacementVerdict::Valid);
	In.Position.X += 1.f;
	ExpectPlacement(*this, TEXT("Outside home boundary rejected"), In, EPlacementVerdict::TerritoryRequired);
	In.Position.X = PlacementPolicy::HeadquartersClearance + In.FootprintRadius;
	ExpectPlacement(*this, TEXT("Home HQ exclusion boundary inclusive"), In, EPlacementVerdict::HeadquartersTooClose);
	In.Position.X += 1.f;
	ExpectPlacement(*this, TEXT("Outside home HQ exclusion allowed"), In, EPlacementVerdict::Valid);
	In.Position = In.HostilePosition - FVector(PlacementPolicy::HostileHeadquartersClearance + In.FootprintRadius, 0.f, 0.f);
	TArray<FPlacementSector> Sectors = { { In.Position, In.Team, false, false, true, false } };
	In.Sectors = Sectors;
	ExpectPlacement(*this, TEXT("Hostile exclusion boundary inclusive"), In, EPlacementVerdict::EnemyHeadquartersTooClose);
	In.Position.X -= 1.f;
	ExpectPlacement(*this, TEXT("Outside hostile exclusion may use established territory"), In, EPlacementVerdict::Valid);
	return true;
}

bool FPlacementProximityTest::RunTest(const FString& Parameters)
{
	FPlacementInput In = PlacementReady();
	TArray<FPlacementBuilding> Buildings = { { In.Position, 70.f, true } };
	In.Buildings = Buildings;
	Buildings[0].Position.X += In.FootprintRadius + Buildings[0].FootprintRadius + PlacementPolicy::BuildingClearance;
	ExpectPlacement(*this, TEXT("Exact overlap threshold rejected"), In, EPlacementVerdict::BuildingOverlap);
	Buildings[0].Position.X += 1.f;
	ExpectPlacement(*this, TEXT("One unit beyond overlap allowed"), In, EPlacementVerdict::Valid);
	Buildings[0].Position = In.Position;
	Buildings[0].bAlive = false;
	ExpectPlacement(*this, TEXT("Dead building does not overlap"), In, EPlacementVerdict::Valid);
	TArray<FVector> Troops = { In.Position + FVector(In.FootprintRadius + PlacementPolicy::EnemyTroopClearance, 0.f, 5000.f) };
	In.EnemyTroops = Troops;
	ExpectPlacement(*this, TEXT("Troop boundary inclusive and ignores height"), In, EPlacementVerdict::EnemyTroopsTooClose);
	Troops[0].X += 1.f;
	ExpectPlacement(*this, TEXT("Beyond troop boundary allowed"), In, EPlacementVerdict::Valid);
	return true;
}

bool FEconomyIncomeTest::RunTest(const FString& Parameters)
{
	constexpr int32 Baseline = 7, PerSite = 3, Sites = 4, TickSeconds = 2;
	const int32 Income = EconomyPolicy::IncomePerTick(Baseline, PerSite, Sites, TickSeconds);
	TestEqual(TEXT("Baseline paid without sites"), EconomyPolicy::IncomePerTick(Baseline, PerSite, 0, TickSeconds), Baseline * TickSeconds);
	TestEqual(TEXT("Each site adds its share"), Income - EconomyPolicy::IncomePerTick(Baseline, PerSite, Sites - 1, TickSeconds), PerSite * TickSeconds);
	TestEqual(TEXT("Tick duration scales income"), Income, EconomyPolicy::IncomePerTick(Baseline, PerSite, Sites, 1) * TickSeconds);
	TestEqual(TEXT("Normal addition exact"), EconomyPolicy::AddResources(19, Income), 19 + Income);
	TestEqual(TEXT("Exact cap reachable"), EconomyPolicy::AddResources(MAX_int32 - Income, Income), MAX_int32);
	TestEqual(TEXT("Addition saturates past cap"), EconomyPolicy::AddResources(MAX_int32 - Income + 1, Income), MAX_int32);
	TestEqual(TEXT("Capped wallet stays capped"), EconomyPolicy::AddResources(MAX_int32, MAX_int32), MAX_int32);
	TestEqual(TEXT("Zero addition preserves balance"), EconomyPolicy::AddResources(19, 0), 19);
	TestEqual(TEXT("Negative addition does not debit"), EconomyPolicy::AddResources(19, -1), 19);
	return true;
}

bool FEconomyRefundTest::RunTest(const FString& Parameters)
{
	constexpr int32 Cost = 19;
	TestEqual(TEXT("No progress refunds full cost"), EconomyPolicy::CancellationRefund(Cost, 0.f), Cost);
	TestEqual(TEXT("Completed work has no refund"), EconomyPolicy::CancellationRefund(Cost, 1.f), 0);
	TestEqual(TEXT("Fractional remainder rounds down, not nearest"), EconomyPolicy::CancellationRefund(Cost, .5f), Cost / 2);
	TestTrue(TEXT("Further progress cannot increase refund"),
		EconomyPolicy::CancellationRefund(Cost, .75f) <= EconomyPolicy::CancellationRefund(Cost, .5f));
	TestEqual(TEXT("Zero-cost cancellation returns nothing"), EconomyPolicy::CancellationRefund(0, .5f), 0);
	return true;
}

bool FEconomyAffordabilityTest::RunTest(const FString& Parameters)
{
	constexpr int32 Cost = 19;
	TestFalse(TEXT("One short cannot spend"), EconomyPolicy::CanAfford(Cost - 1, Cost));
	TestTrue(TEXT("Exact balance can spend"), EconomyPolicy::CanAfford(Cost, Cost));
	TestTrue(TEXT("Surplus can spend"), EconomyPolicy::CanAfford(Cost + 1, Cost));
	TestFalse(TEXT("Zero cost is rejected by wallet"), EconomyPolicy::CanAfford(Cost, 0));
	TestFalse(TEXT("Negative cost cannot mint resources"), EconomyPolicy::CanAfford(Cost, -1));
	return true;
}

bool FOutcomeHealthTest::RunTest(const FString& Parameters)
{
	const auto ExpectOutcome = [this](const TCHAR* What, int32 FriendlyHealth, int32 EnemyHealth, EMatchResult Expected)
	{
		const EMatchResult Result = OutcomePolicy::Evaluate({ FriendlyHealth, EnemyHealth,
			EMatchResult::Ongoing, EMatchResult::Victory, EMatchResult::Defeat });
		TestEqual(What, static_cast<int32>(Result), static_cast<int32>(Expected));
	};
	ExpectOutcome(TEXT("Both headquarters alive keeps match ongoing"), 1, 1, EMatchResult::Ongoing);
	ExpectOutcome(TEXT("Only enemy headquarters destroyed wins"), 1, 0, EMatchResult::Victory);
	ExpectOutcome(TEXT("Only friendly headquarters destroyed loses"), 0, 1, EMatchResult::Defeat);
	ExpectOutcome(TEXT("Simultaneous destruction is defeat"), 0, 0, EMatchResult::Defeat);
	ExpectOutcome(TEXT("Enemy health below zero remains destroyed"), 1, -1, EMatchResult::Victory);
	ExpectOutcome(TEXT("Friendly health below zero still takes precedence"), -1, 0, EMatchResult::Defeat);
	return true;
}

#endif
