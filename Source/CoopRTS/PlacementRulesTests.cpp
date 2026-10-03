#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/PlacementPolicy.h"

// Pure rule tests: no world, no actors. Values are arbitrary; assertions are invariants.

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

#endif
