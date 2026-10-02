#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Rules/ProductionPolicy.h"
#include "Rules/PlacementPolicy.h"
#include "Rules/EconomyPolicy.h"
#include "Rules/OutcomePolicy.h"
#include "Rules/GoalPath.h"
#include "Rules/CombatPolicy.h"
#include "Rules/TargetingPolicy.h"
#include "Rules/PursuitPolicy.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementGridTest, "CoopRTS.Rules.Placement.BuildGrid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementTerritoryTest, "CoopRTS.Rules.Placement.Territory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementBuildTerritoryTest, "CoopRTS.Rules.Placement.BuildTerritory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementDepositTest, "CoopRTS.Rules.Placement.Deposit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementPolygonTest, "CoopRTS.Rules.Placement.Polygon",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementPrecedenceTest, "CoopRTS.Rules.Placement.Precedence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementHeadquartersTest, "CoopRTS.Rules.Placement.HeadquartersBoundaries",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlacementProximityTest, "CoopRTS.Rules.Placement.ProximityBoundaries",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyIncomeTest, "CoopRTS.Rules.Economy.IncomeAndSaturation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyJevScalingTest, "CoopRTS.Rules.Economy.JevPlayerCount",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyExtractorTest, "CoopRTS.Rules.Economy.ExtractorDepletionAndOwner",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyRefundTest, "CoopRTS.Rules.Economy.Refund",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyAffordabilityTest, "CoopRTS.Rules.Economy.Affordability",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutcomeHealthTest, "CoopRTS.Rules.Outcome.HealthAndTies",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
const FVector2D HomePolygon[] = { { -1000., -1000. }, { 1000., -1000. }, { 1000., 1000. }, { -1000., 1000. } };
const FPlacementRegion HomeRegions[] = { { 0, HomePolygon, 0, false } };

FPlacementInput PlacementReady()
{
	FPlacementInput In{};
	In.Team = 0;
	In.FootprintRadius = 80.f;
	In.HomePosition = FVector::ZeroVector;
	In.HostilePosition = FVector(8000.f, 0.f, 0.f);
	In.Position = FVector(600.f, 0.f, 0.f);
	In.bInsidePlacementBounds = In.bHeadquartersAvailable = true;
	In.Regions = HomeRegions;
	return In;
}

void ExpectPlacement(FAutomationTestBase& Test, const TCHAR* What, const FPlacementInput& In, EPlacementVerdict Expected)
{
	Test.TestEqual(What, static_cast<int32>(PlacementPolicy::Evaluate(In).Verdict), static_cast<int32>(Expected));
}
}

bool FPlacementGridTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Barracks occupies five cells"), PlacementPolicy::FootprintCells(125.f), 5);
	TestEqual(TEXT("Extractor rounds up to four cells"), PlacementPolicy::FootprintCells(95.f), 4);
	TestEqual(TEXT("Workshop rounds up to six cells"), PlacementPolicy::FootprintCells(145.f), 6);
	TestEqual(TEXT("Exact cell boundary does not add a cell"), PlacementPolicy::FootprintCells(100.f), 4);
	TestEqual(TEXT("Extent just beyond a cell boundary adds a cell"), PlacementPolicy::FootprintCells(100.1f), 5);
	const struct
	{
		const TCHAR* Name;
		float HalfExtent;
		FVector Position, Expected;
	} Cases[] = {
		{ TEXT("Odd positive"), 125.f, FVector(31., 83., 17.125), FVector(25., 75., 17.125) },
		{ TEXT("Odd positive ties"), 125.f, FVector(0., 50., -4.5), FVector(25., 75., -4.5) },
		{ TEXT("Odd negative"), 125.f, FVector(-31., -83., 91.25), FVector(-25., -75., 91.25) },
		{ TEXT("Odd negative ties"), 125.f, FVector(-50., -100., -17.125), FVector(-25., -75., -17.125) },
		{ TEXT("Even positive"), 95.f, FVector(24., 76., 23.75), FVector(0., 100., 23.75) },
		{ TEXT("Even positive ties"), 95.f, FVector(25., 75., -1.25), FVector(50., 100., -1.25) },
		{ TEXT("Even negative"), 95.f, FVector(-24., -76., 8.5), FVector(0., -100., 8.5) },
		{ TEXT("Even negative ties"), 95.f, FVector(-25., -75., -33.75), FVector(0., -50., -33.75) },
		{ TEXT("Workshop even"), 145.f, FVector(24., -76., 103.125), FVector(0., -100., 103.125) }
	};
	for (const auto& Case : Cases)
	{
		const FVector Snapped = PlacementPolicy::SnapToBuildGrid(Case.Position, Case.HalfExtent);
		TestTrue(FString::Printf(TEXT("%s snaps to exact footprint-aligned XY"), Case.Name),
			Snapped.X == Case.Expected.X && Snapped.Y == Case.Expected.Y);
		TestEqual(FString::Printf(TEXT("%s preserves Z exactly"), Case.Name), Snapped.Z, Case.Position.Z);
		const FVector Again = PlacementPolicy::SnapToBuildGrid(Snapped, Case.HalfExtent);
		TestTrue(FString::Printf(TEXT("%s snapping is idempotent"), Case.Name),
			Again.X == Snapped.X && Again.Y == Snapped.Y && Again.Z == Snapped.Z);
	}
	return true;
}

bool FPlacementTerritoryTest::RunTest(const FString& Parameters)
{
	FPlacementInput In = PlacementReady();
	ExpectPlacement(*this, TEXT("Controlled main provides build rights"), In, EPlacementVerdict::Valid);
	In.Position = FVector(3000.f, 0.f, 0.f);
	ExpectPlacement(*this, TEXT("Outside polygons has no build rights"), In, EPlacementVerdict::TerritoryRequired);
	const FVector2D Polygon[] = { { 2000., -1000. }, { 4000., -1000. }, { 4000., 1000. }, { 2000., 1000. } };
	FPlacementRegion Regions[] = { { 7, Polygon, 0, false } };
	In.Regions = Regions;
	ExpectPlacement(*this, TEXT("Capture grants rights without an extractor"), In, EPlacementVerdict::Valid);
	TestEqual(TEXT("Decision identifies region, not collection offset"), PlacementPolicy::Evaluate(In).RegionIndex, 7);
	Regions[0].ControllingTeam = -1;
	ExpectPlacement(*this, TEXT("Neutral region has no build rights"), In, EPlacementVerdict::TerritoryRequired);
	Regions[0].ControllingTeam = 5;
	ExpectPlacement(*this, TEXT("Enemy region has no human build rights"), In, EPlacementVerdict::TerritoryRequired);
	In.Team = 5;
	ExpectPlacement(*this, TEXT("Enemy builds in its controlled region"), In, EPlacementVerdict::Valid);
	Regions[0].bContested = true;
	ExpectPlacement(*this, TEXT("Hostile presence contests entire region"), In, EPlacementVerdict::Contested);
	Regions[0].bContested = false;
	In.Position.X = 4000.f - In.FootprintRadius;
	ExpectPlacement(*this, TEXT("All square corners on boundary are allowed"), In, EPlacementVerdict::Valid);
	In.Position.X += .01f;
	ExpectPlacement(*this, TEXT("Any corner beyond region rejects footprint"), In, EPlacementVerdict::TerritoryRequired);
	TestEqual(TEXT("Living human main ignores enemy capture anchor"), PlacementPolicy::RegionController(true, 0, true, 5), 0);
	TestEqual(TEXT("Living enemy main ignores friendly capture anchor"), PlacementPolicy::RegionController(true, 5, true, 0), 5);
	TestEqual(TEXT("Destroyed main HQ removes control"), PlacementPolicy::RegionController(true, 0, false, 0), -1);
	TestEqual(TEXT("Non-main ownership comes from capture anchor"), PlacementPolicy::RegionController(false, 0, true, 5), 5);
	TestEqual(TEXT("Neutral anchor remains neutral"), PlacementPolicy::RegionController(false, -1, true, -1), -1);
	return true;
}

bool FPlacementBuildTerritoryTest::RunTest(const FString& Parameters)
{
	FPlacementInput In = PlacementReady();
	auto InTerritory = [&In]() { return PlacementPolicy::EvaluateTerritory(In).Verdict == EPlacementVerdict::Valid; };
	In.Position = In.HomePosition;
	TestTrue(TEXT("Territory query excludes HQ clearance"), InTerritory());
	In.Position.X = 1000.f - In.FootprintRadius;
	TestTrue(TEXT("Exact polygon edge includes footprint"), InTerritory());
	In.Position.X += 1.f;
	TestFalse(TEXT("Straddling polygon edge rejected"), InTerritory());
	const FVector2D Adjacent[] = { { 1000., -1000. }, { 2000., -1000. }, { 2000., 1000. }, { 1000., 1000. } };
	FPlacementRegion Regions[] = { HomeRegions[0], { 1, Adjacent, 0, false } };
	In.Regions = Regions;
	In.Position.X = 1000.f;
	TestFalse(TEXT("Two controlled polygons cannot jointly cover one footprint"), InTerritory());
	In.Position.X = 600.f;
	In.bInsidePlacementBounds = false;
	TestFalse(TEXT("Arena bounds still restrict controlled regions"), InTerritory());
	In.bInsidePlacementBounds = true;
	Regions[0].bContested = true;
	TestFalse(TEXT("Contested main has no build rights"), InTerritory());
	Regions[0].bContested = false;
	const FPlacementBuilding Buildings[] = { { In.Position, 80.f, true } };
	const FVector Troops[] = { In.Position };
	In.Buildings = Buildings;
	In.EnemyTroops = Troops;
	In.HostilePosition = In.Position;
	TestTrue(TEXT("Territory excludes overlap, troop proximity and hostile HQ clearance"), InTerritory());
	In.bHeadquartersAvailable = false;
	TestFalse(TEXT("Unavailable home HQ disallows building"), InTerritory());
	return true;
}

bool FPlacementPolygonTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Polygon centre is inside"), PlacementPolicy::ContainsPoint(HomePolygon, FVector2D::ZeroVector));
	TestTrue(TEXT("Polygon edge is inside"), PlacementPolicy::ContainsPoint(HomePolygon, FVector2D(1000., 0.)));
	TestTrue(TEXT("Polygon corner is inside"), PlacementPolicy::ContainsPoint(HomePolygon, FVector2D(-1000., 1000.)));
	TestFalse(TEXT("Point beyond edge is outside"), PlacementPolicy::ContainsPoint(HomePolygon, FVector2D(1000.01, 0.)));
	const FVector2D Clockwise[] = { { -1000., -1000. }, { -1000., 1000. }, { 1000., 1000. }, { 1000., -1000. }, { -1000., -1000. } };
	TestTrue(TEXT("Clockwise polygon with repeated closing vertex works"), PlacementPolicy::ContainsPoint(Clockwise, FVector2D(200., -300.)));
	TestFalse(TEXT("Empty polygon excludes every point"), PlacementPolicy::ContainsPoint({}, FVector2D::ZeroVector));
	const FVector2D Segment[] = { { 0., 0. }, { 1., 0. } };
	TestFalse(TEXT("Two vertices are not a region"), PlacementPolicy::ContainsPoint(Segment, FVector2D(.5, 0.)));
	const FVector2D Concave[] = { { 0., 0. }, { 600., 0. }, { 600., 200. }, { 200., 200. }, { 200., 600. }, { 0., 600. } };
	TestTrue(TEXT("Concave polygon includes a side arm"), PlacementPolicy::ContainsPoint(Concave, FVector2D(500., 100.)));
	TestFalse(TEXT("Concave indentation is outside"), PlacementPolicy::ContainsPoint(Concave, FVector2D(300., 300.)));
	TestFalse(TEXT("Centre and cardinal samples cannot mask a diagonal corner outside"),
		PlacementPolicy::ContainsFootprint(Concave, FVector(150., 150., 9000.), 100.f));
	TestTrue(TEXT("XY footprint fitting exactly along concave edge is accepted"),
		PlacementPolicy::ContainsFootprint(Concave, FVector(100., 100., -9000.), 100.f));
	const FVector2D Diamond[] = { { 0., -1000. }, { 1000., 0. }, { 0., 1000. }, { -1000., 0. } };
	TestTrue(TEXT("Square corners exactly on oblique edges fit"), PlacementPolicy::ContainsFootprint(Diamond, FVector(840., 0., 0.), 80.f));
	TestFalse(TEXT("Square corners beyond oblique edges fail"), PlacementPolicy::ContainsFootprint(Diamond, FVector(841., 0., 0.), 80.f));
	const FVector2D SnapBoundary[] = { { -1000., -1000. }, { 999., -1000. }, { 999., 1000. }, { -1000., 1000. } };
	const FVector Requested(873., 0., 23.);
	TestTrue(TEXT("Unsnapped request can fit near a boundary"), PlacementPolicy::ContainsFootprint(SnapBoundary, Requested, 125.f));
	const FVector Snapped = PlacementPolicy::SnapToBuildGrid(Requested, 125.f);
	TestFalse(TEXT("Territory checks snapped centre and corners, not cursor"), PlacementPolicy::ContainsFootprint(SnapBoundary, Snapped, 125.f));
	return true;
}

bool FPlacementDepositTest::RunTest(const FString& Parameters)
{
	const FVector Requested(10., 20., 9000.);
	FPlacementDeposit Deposits[] = {
		{ Requested + FVector(50., 0., -9000.), 3, 0, true, false },
		{ Requested + FVector(100., 0., -9000.), 3, -1, false, false },
		{ Requested + FVector(150., 0., -9000.), 3, 0, false, true },
		{ Requested + FVector(200., 0., -9000.), 4, 0, false, false },
		{ Requested + FVector(250., 0., -9000.), 4, 0, false, false }
	};
	TestEqual(TEXT("Occupied, neutral and contested closer deposits are excluded"),
		PlacementPolicy::SelectFreeDeposit(0, Requested, Deposits), 3);
	Deposits[3].bOccupied = true;
	TestEqual(TEXT("Reservation includes unfinished construction"), PlacementPolicy::SelectFreeDeposit(0, Requested, Deposits), 4);
	Deposits[4].Position = Requested + FVector(300., 0., -9000.);
	TestEqual(TEXT("300 cm snap radius is inclusive and ignores height"), PlacementPolicy::SelectFreeDeposit(0, Requested, Deposits), 4);
	Deposits[4].Position.X += .01;
	TestEqual(TEXT("Beyond 300 cm cannot snap"), PlacementPolicy::SelectFreeDeposit(0, Requested, Deposits), INDEX_NONE);
	Deposits[3].bOccupied = false;
	Deposits[4].Position = Deposits[3].Position;
	TestEqual(TEXT("Equal distance preserves stable deposit ordering"), PlacementPolicy::SelectFreeDeposit(0, Requested, Deposits), 3);
	Deposits[3].ControllingTeam = 5;
	TestEqual(TEXT("Enemy selection uses enemy control"), PlacementPolicy::SelectFreeDeposit(5, Requested, Deposits), 3);
	TestEqual(TEXT("Invalid team cannot claim a deposit"), PlacementPolicy::SelectFreeDeposit(-1, Requested, Deposits), INDEX_NONE);
	TestEqual(TEXT("No deposits rejects placement"), PlacementPolicy::SelectFreeDeposit(0, Requested, {}), INDEX_NONE);
	return true;
}

bool FPlacementPrecedenceTest::RunTest(const FString& Parameters)
{
	FPlacementInput In = PlacementReady();
	FPlacementRegion Regions[] = { { 0, HomePolygon, 0, true } };
	FPlacementBuilding Buildings[] = { { In.Position, 70.f, true } };
	FVector Troops[] = { In.Position };
	In.Regions = Regions;
	In.Buildings = Buildings;
	In.EnemyTroops = Troops;
	In.Team = -1;
	In.bInsidePlacementBounds = In.bHeadquartersAvailable = false;
	ExpectPlacement(*this, TEXT("Invalid team masks bounds"), In, EPlacementVerdict::Invalid);
	In.Team = 0;
	ExpectPlacement(*this, TEXT("Bounds mask unavailable headquarters"), In, EPlacementVerdict::OutsideBounds);
	In.bInsidePlacementBounds = true;
	ExpectPlacement(*this, TEXT("Unavailable HQ masks contention"), In, EPlacementVerdict::HeadquartersUnavailable);
	In.bHeadquartersAvailable = true;
	In.HostilePosition = In.Position;
	ExpectPlacement(*this, TEXT("Hostile HQ clearance masks contention"), In, EPlacementVerdict::EnemyHeadquartersTooClose);
	In.HostilePosition = FVector(8000.f, 0.f, 0.f);
	ExpectPlacement(*this, TEXT("Contested region masks troop proximity"), In, EPlacementVerdict::Contested);
	Regions[0].bContested = false;
	ExpectPlacement(*this, TEXT("Troops mask overlap"), In, EPlacementVerdict::EnemyTroopsTooClose);
	In.EnemyTroops = {};
	ExpectPlacement(*this, TEXT("Overlap follows troops"), In, EPlacementVerdict::BuildingOverlap);
	In.Position = In.HomePosition;
	Buildings[0].Position = In.Position;
	ExpectPlacement(*this, TEXT("Overlap masks home HQ clearance"), In, EPlacementVerdict::BuildingOverlap);
	In.Buildings = {};
	ExpectPlacement(*this, TEXT("Home HQ clearance follows overlap"), In, EPlacementVerdict::HeadquartersTooClose);
	In.Position = FVector(3000.f, 0.f, 0.f);
	Troops[0] = In.Position;
	In.EnemyTroops = Troops;
	ExpectPlacement(*this, TEXT("Missing territory masks troop proximity"), In, EPlacementVerdict::TerritoryRequired);
	return true;
}

bool FPlacementHeadquartersTest::RunTest(const FString& Parameters)
{
	FPlacementInput In = PlacementReady();
	In.Position.X = PlacementPolicy::HeadquartersClearance + In.FootprintRadius;
	ExpectPlacement(*this, TEXT("Home HQ exclusion boundary inclusive"), In, EPlacementVerdict::HeadquartersTooClose);
	In.Position.X += 1.f;
	ExpectPlacement(*this, TEXT("Outside home HQ exclusion allowed"), In, EPlacementVerdict::Valid);
	const FVector2D Forward[] = { { 6000., -2000. }, { 8000., -2000. }, { 8000., 2000. }, { 6000., 2000. } };
	const FPlacementRegion Regions[] = { { 2, Forward, 0, false } };
	In.Regions = Regions;
	In.Position = In.HostilePosition - FVector(PlacementPolicy::HostileHeadquartersClearance + In.FootprintRadius, 0.f, 0.f);
	ExpectPlacement(*this, TEXT("Hostile HQ exclusion boundary inclusive"), In, EPlacementVerdict::EnemyHeadquartersTooClose);
	In.Position.X -= 1.f;
	ExpectPlacement(*this, TEXT("Outside hostile exclusion can use controlled region"), In, EPlacementVerdict::Valid);
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

bool FEconomyJevScalingTest::RunTest(const FString& Parameters)
{
	const double Expected[] = { 1., 1.3, 1.6, 1.9, 2.2 };
	for (int32 Count = 1; Count <= 5; ++Count)
		TestTrue(FString::Printf(TEXT("%d human commanders use the specified JEV factor"), Count),
			FMath::IsNearlyEqual(EconomyPolicy::JevPlayerCountFactor(Count), Expected[Count - 1], 1.e-12));
	for (const int32 Count : { MIN_int32, -1, 0 })
		TestEqual(TEXT("Invalid counts use the solo factor"), EconomyPolicy::JevPlayerCountFactor(Count), 1.);
	return true;
}

bool FEconomyIncomeTest::RunTest(const FString& Parameters)
{
	constexpr int32 Income = 19;
	TestEqual(TEXT("Normal addition exact"), EconomyPolicy::AddResources(19, Income), 19 + Income);
	TestEqual(TEXT("Exact cap reachable"), EconomyPolicy::AddResources(MAX_int32 - Income, Income), MAX_int32);
	TestEqual(TEXT("Addition saturates past cap"), EconomyPolicy::AddResources(MAX_int32 - Income + 1, Income), MAX_int32);
	TestEqual(TEXT("Capped wallet stays capped"), EconomyPolicy::AddResources(MAX_int32, MAX_int32), MAX_int32);
	TestEqual(TEXT("Zero addition preserves balance"), EconomyPolicy::AddResources(19, 0), 19);
	TestEqual(TEXT("Negative addition does not debit"), EconomyPolicy::AddResources(19, -1), 19);
	return true;
}

bool FEconomyExtractorTest::RunTest(const FString& Parameters)
{
	FExtractorPaymentInput In{ 4, 2400, 2, 0, 2, 0, 2, true, true };
	FExtractorPayment Payment = EconomyPolicy::ExtractorPayment(In);
	TestEqual(TEXT("Normal extractor pays eight per two seconds"), Payment.Amount, 8);
	TestEqual(TEXT("Normal extraction subtracts exactly payment"), Payment.Remaining, 2392);
	In.RatePerSecond = 6;
	In.Remaining = 3000;
	Payment = EconomyPolicy::ExtractorPayment(In);
	TestEqual(TEXT("Rich extractor pays twelve per two seconds"), Payment.Amount, 12);
	TestEqual(TEXT("Rich extraction subtracts exactly payment"), Payment.Remaining, 2988);
	In.Remaining = 5;
	Payment = EconomyPolicy::ExtractorPayment(In);
	TestEqual(TEXT("Final payment is capped by remaining deposit"), Payment.Amount, 5);
	TestEqual(TEXT("Final payment depletes deposit to zero"), Payment.Remaining, 0);
	In.Remaining = Payment.Remaining;
	TestEqual(TEXT("Empty deposit stops income"), EconomyPolicy::ExtractorPayment(In).Amount, 0);
	In.Remaining = 23;
	for (const bool bAlive : { false, true })
		for (const bool bComplete : { false, true })
		{
			In.bAlive = bAlive;
			In.bComplete = bComplete;
			Payment = EconomyPolicy::ExtractorPayment(In);
			TestEqual(TEXT("Only completed living extractors pay"), Payment.Amount, bAlive && bComplete ? 12 : 0);
			TestEqual(TEXT("Unpaid extractors never consume deposit"), Payment.Remaining, bAlive && bComplete ? 11 : 23);
		}
	In.RecipientCommander = 1;
	Payment = EconomyPolicy::ExtractorPayment(In);
	TestEqual(TEXT("Another friendly commander receives no extraction"), Payment.Amount, 0);
	TestEqual(TEXT("Wrong recipient leaves deposit untouched"), Payment.Remaining, In.Remaining);
	In.RecipientCommander = In.OwnerCommander;
	In.RecipientTeam = 5;
	TestEqual(TEXT("Enemy wallet cannot receive human extraction"), EconomyPolicy::ExtractorPayment(In).Amount, 0);
	In.OwnerTeam = In.RecipientTeam = 5;
	In.OwnerCommander = In.RecipientCommander = -1;
	Payment = EconomyPolicy::ExtractorPayment(In);
	TestEqual(TEXT("Enemy extractor pays its own enemy wallet"), Payment.Amount, 12);
	In.RecipientTeam = 0;
	In.RecipientCommander = 0;
	TestEqual(TEXT("Enemy extraction cannot fund a human wallet"), EconomyPolicy::ExtractorPayment(In).Amount, 0);
	In.OwnerTeam = In.RecipientTeam = 0;
	In.OwnerCommander = In.RecipientCommander = 0;
	In.RatePerSecond = MAX_int32;
	In.TickSeconds = MAX_int32;
	In.Remaining = MAX_int32;
	TestEqual(TEXT("Large extraction multiply cannot overflow"), EconomyPolicy::ExtractorPayment(In).Amount, MAX_int32);
	In.TickSeconds = 0;
	TestEqual(TEXT("Zero-duration extraction consumes nothing"), EconomyPolicy::ExtractorPayment(In).Remaining, MAX_int32);
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
	const auto ExpectOutcome = [this](const TCHAR* What, int32 FriendlyHealth, int32 EnemyHealth, EMatchResult Expected) {
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGoalPathTest, "CoopRTS.Rules.Goals.NextWaypoint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGoalPathTest::RunTest(const FString& Parameters)
{
	// Two equally short branches (0-1-3 and 0-2-3), a cycle, and isolated 4.
	const uint64 Graph[] = {
		(uint64(1) << 1) | (uint64(1) << 2),
		(uint64(1) << 0) | (uint64(1) << 3),
		(uint64(1) << 0) | (uint64(1) << 3),
		(uint64(1) << 1) | (uint64(1) << 2),
		0
	};
	TestEqual(TEXT("Equal shortest paths choose ascending region indices"),
		ForceGoals::NextWaypoint(Graph, 5, 0, 3), 1);
	TestEqual(TEXT("Reverse traversal also resolves ties deterministically"),
		ForceGoals::NextWaypoint(Graph, 5, 3, 0), 1);
	TestEqual(TEXT("An adjacent target is the next waypoint"),
		ForceGoals::NextWaypoint(Graph, 5, 0, 2), 2);
	TestEqual(TEXT("An already reached target returns the start"),
		ForceGoals::NextWaypoint(Graph, 5, 3, 3), 3);
	TestEqual(TEXT("An isolated start already at its target remains valid"),
		ForceGoals::NextWaypoint(Graph, 5, 4, 4), 4);
	TestEqual(TEXT("Cycles do not make an unreachable target reachable"),
		ForceGoals::NextWaypoint(Graph, 5, 0, 4), INDEX_NONE);
	TestEqual(TEXT("Isolated start cannot reach the connected component"),
		ForceGoals::NextWaypoint(Graph, 5, 4, 0), INDEX_NONE);

	// The lower-index branch is longer: 0-1-2-3 versus 0-4-3.
	const uint64 Unequal[] = {
		(uint64(1) << 1) | (uint64(1) << 4), uint64(1) << 2,
		uint64(1) << 3, 0, uint64(1) << 3
	};
	TestEqual(TEXT("Shortest path beats the first ascending-index branch"),
		ForceGoals::NextWaypoint(Unequal, 5, 0, 3), 4);
	TestEqual(TEXT("Missing graph rejects even an already reached target"),
		ForceGoals::NextWaypoint(nullptr, 5, 0, 0), INDEX_NONE);
	TestEqual(TEXT("Empty graph rejects"), ForceGoals::NextWaypoint(Graph, 0, 0, 0), INDEX_NONE);
	TestEqual(TEXT("Negative start rejects"), ForceGoals::NextWaypoint(Graph, 5, -1, 3), INDEX_NONE);
	TestEqual(TEXT("Out-of-range start rejects"), ForceGoals::NextWaypoint(Graph, 5, 5, 3), INDEX_NONE);
	TestEqual(TEXT("Negative target rejects"), ForceGoals::NextWaypoint(Graph, 5, 0, -1), INDEX_NONE);
	TestEqual(TEXT("Out-of-range target rejects"), ForceGoals::NextWaypoint(Graph, 5, 0, 5), INDEX_NONE);

	uint64 Wide[ForceGoals::MaxRegions] = {};
	Wide[0] = uint64(1) << 63;
	Wide[63] = uint64(1) << 0;
	TestEqual(TEXT("Highest supported region bit is traversable"),
		ForceGoals::NextWaypoint(Wide, ForceGoals::MaxRegions, 0, 63), 63);
	TestEqual(TEXT("Highest supported region can be the start"),
		ForceGoals::NextWaypoint(Wide, ForceGoals::MaxRegions, 63, 0), 0);
	TestEqual(TEXT("Adjacency bits outside the supplied graph are ignored"),
		ForceGoals::NextWaypoint(Wide, 2, 0, 1), INDEX_NONE);
	TestEqual(TEXT("Oversized graph rejects before reading it"),
		ForceGoals::NextWaypoint(Wide, ForceGoals::MaxRegions + 1, 0, 1), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCounterMatrixTest, "CoopRTS.Rules.Combat.CounterMatrix",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTargetingOrderTest, "CoopRTS.Rules.Combat.TargetingOrder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCounterMatrixTest::RunTest(const FString& Parameters)
{
	const EArmorClass Armors[] = { EArmorClass::Light, EArmorClass::Heavy, EArmorClass::Shielded, EArmorClass::Structure };
	const EDamageType Types[] = { EDamageType::Kinetic, EDamageType::Piercing, EDamageType::Demolition, EDamageType::EMP };
	// Independent matrix: EMP's shield behavior is not an HP class bonus.
	const int32 Expected[][4] = {
		{ 60, 40, 40, 40 },
		{ 40, 60, 40, 40 },
		{ 40, 40, 40, 60 },
		{ 40, 40, 40, 40 }
	};
	for (int32 Type = 0; Type < 4; ++Type)
		for (int32 Armor = 0; Armor < 4; ++Armor)
		{
			const FString Label = FString::Printf(TEXT("Damage type %d against armor %d"), Type, Armor);
			TestEqual(Label, CombatPolicy::Damage(40, Types[Type], Armors[Armor]), Expected[Type][Armor]);
			TestEqual(Label + TEXT(" bonus classification"), CombatPolicy::IsStrongAgainst(Types[Type], Armors[Armor]),
				Expected[Type][Armor] == 60);
		}
	TestEqual(TEXT("Half HP from an odd boosted hit truncates"), CombatPolicy::Damage(15, EDamageType::Kinetic, EArmorClass::Light), 22);
	TestEqual(TEXT("Zero damage stays zero"), CombatPolicy::Damage(0, EDamageType::Demolition, EArmorClass::Structure), 0);
	return true;
}

bool FTargetingOrderTest::RunTest(const FString& Parameters)
{
	const EDamageType Types[] = { EDamageType::Kinetic, EDamageType::Piercing, EDamageType::Demolition };
	const EArmorClass Preferred[] = { EArmorClass::Light, EArmorClass::Heavy, EArmorClass::Structure };
	const EArmorClass Other[] = { EArmorClass::Heavy, EArmorClass::Light, EArmorClass::Light };
	for (int32 Type = 0; Type < 3; ++Type)
	{
		FTargetSelection Selection;
		TestEqual(TEXT("No eligible enemies means no target"), Selection.Index, INDEX_NONE);
		Selection.Consider(Types[Type], 0, Other[Type], 1.);
		Selection.Consider(Types[Type], 1, Preferred[Type], 100.);
		TestEqual(TEXT("Counter armor outranks a nearer non-counter"), Selection.Index, 1);
		Selection.Consider(Types[Type], 2, Other[Type], .5);
		TestEqual(TEXT("Later nearer non-counter cannot displace a counter"), Selection.Index, 1);
		Selection.Consider(Types[Type], 3, Preferred[Type], 25.);
		TestEqual(TEXT("Nearest matching armor wins"), Selection.Index, 3);
		Selection.Consider(Types[Type], 4, Preferred[Type], 25.);
		TestEqual(TEXT("Exact tie retains the first candidate"), Selection.Index, 3);

		FTargetSelection Fallback;
		Fallback.Consider(Types[Type], 0, EArmorClass::Shielded, 100.);
		Fallback.Consider(Types[Type], 1, Other[Type], 4.);
		TestEqual(TEXT("Without a counter target nearest wins across classes"), Fallback.Index, 1);
	}
	FTargetSelection EMP;
	EMP.Consider(EDamageType::EMP, 0, EArmorClass::Shielded, 100.);
	EMP.Consider(EDamageType::EMP, 1, EArmorClass::Heavy, 25.);
	TestEqual(TEXT("EMP has no HP preference until shield rules are built"), EMP.Index, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPursuitTransitionsTest, "CoopRTS.Rules.Combat.PursuitTransitions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPursuitTransitionsTest::RunTest(const FString& Parameters)
{
	const FVector Origin = FVector::ZeroVector;
	for (const float Range : { 175.f, 560.f, 1150.f })
	{
		const auto Decide = [&](float Distance, bool bPursuing, bool bChanged, const FVector& LastGoal) {
			return PursuitPolicy::Evaluate(Origin, FVector(Distance, 0.f, 0.f), Range,
				Origin, 2000.f, 0.f, bPursuing, bChanged, LastGoal);
		};
		const FPursuitDecision Firing = Decide(Range, true, false, Origin);
		TestTrue(TEXT("Exact weapon boundary fires"), Firing.bInRange);
		TestFalse(TEXT("In-range unit does not issue pursuit"), Firing.bIssueMove);
		// All bands cross the former hysteresis window; for 1150 the old window
		// ends before weapon range, but first out-of-range movement still matters.
		for (const float Distance : { Range + 1.f, Range + 25.f, FMath::Max(Range + 1.f, .82f * Range + 129.f) })
		{
			const FPursuitDecision Leaving = Decide(Distance, false, false, Origin);
			TestTrue(TEXT("Leaving firing range always starts pursuit"), Leaving.bIssueMove);
			TestTrue(TEXT("Pursuit endpoint is inside weapon range"),
				FVector::Dist2D(Leaving.Goal, FVector(Distance, 0.f, 0.f)) < Range);
			TestFalse(TEXT("Stationary target does not reissue an accepted move"),
				Decide(Distance, true, false, Leaving.Goal).bIssueMove);
			TestTrue(TEXT("A finished path outside range must resume even with an unchanged endpoint"),
				Decide(Distance, false, false, Leaving.Goal).bIssueMove);
			TestTrue(TEXT("Switch to a nearby target bypasses prior goal hysteresis"),
				Decide(Distance + 10.f, true, true, Leaving.Goal).bIssueMove);
			TestFalse(TEXT("Target drift exactly at 130 cm does not reissue"),
				Decide(Distance + 130.f, true, false, Leaving.Goal).bIssueMove);
			TestTrue(TEXT("Target drift above 130 cm reissues"),
				Decide(Distance + 131.f, true, false, Leaving.Goal).bIssueMove);
		}
	}
	const FPursuitDecision Clamped = PursuitPolicy::Evaluate(Origin, FVector(1000.f, 0.f, 0.f),
		175.f, Origin, 500.f, 7.f, false, false, Origin);
	TestTrue(TEXT("Pursuit goal stays inside order leash"), FMath::IsNearlyEqual(Clamped.Goal.Size2D(), 500.));
	TestEqual(TEXT("Pursuit goal keeps order height"), Clamped.Goal.Z, 7.);
	return true;
}

#endif
