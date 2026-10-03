#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ForceSelectionPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceSelectionAccessTest, "CoopRTS.Rules.Selection.Access",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceSelectionNumbersTest, "CoopRTS.Rules.Selection.Numbers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceSelectionBoxTest, "CoopRTS.Rules.Selection.ScreenBox",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FForceSelectionAccessTest::RunTest(const FString&)
{
	using namespace ForceSelectionPolicy;
	const auto Expect = [this](const TCHAR* Message, int32 Local, int32 Owner, bool bSameTeam, bool bAlive, EAccess Expected) {
		TestEqual(Message, static_cast<int32>(ResolveAccess(Local, Owner, bSameTeam, bAlive)), static_cast<int32>(Expected));
	};
	Expect(TEXT("Own living force is command-selectable"), 0, 0, true, true, EAccess::Command);
	Expect(TEXT("Last human commander owns its own force"), 4, 4, true, true, EAccess::Command);
	Expect(TEXT("Friendly teammate is read-only"), 0, 1, true, true, EAccess::Inspect);
	Expect(TEXT("Same commander number never overrides enemy team"), 0, 0, false, true, EAccess::None);
	Expect(TEXT("Enemy is neither command-selected nor inspected"), 0, 5, false, true, EAccess::None);
	Expect(TEXT("Dead own force is unavailable"), 0, 0, true, false, EAccess::None);
	Expect(TEXT("Dead teammate force is unavailable"), 0, 1, true, false, EAccess::None);
	Expect(TEXT("Unassigned local commander cannot inspect"), INDEX_NONE, 0, true, true, EAccess::None);
	Expect(TEXT("Unassigned owner cannot inspect"), 0, INDEX_NONE, true, true, EAccess::None);
	Expect(TEXT("Matching unassigned indices do not confer ownership"), INDEX_NONE, INDEX_NONE, true, true, EAccess::None);
	return true;
}

bool FForceSelectionNumbersTest::RunTest(const FString&)
{
	using namespace ForceSelectionPolicy;
	for (const bool bSolo : { false, true })
	{
		const int32 Last = bSolo ? 5 : 4;
		TestFalse(TEXT("Negative number is not selectable"), IsNumberAvailable(-1, bSolo));
		TestFalse(TEXT("Zero is not a force number"), IsNumberAvailable(0, bSolo));
		for (int32 Number = 1; Number <= Last; ++Number)
			TestTrue(FString::Printf(TEXT("%s force number %d is selectable"), bSolo ? TEXT("Solo") : TEXT("Co-op"), Number),
				IsNumberAvailable(Number, bSolo));
		TestFalse(TEXT("First number past mode limit is unavailable"), IsNumberAvailable(Last + 1, bSolo));
		TestFalse(TEXT("Arbitrary large number is unavailable"), IsNumberAvailable(MAX_int32, bSolo));
	}
	return true;
}

bool FForceSelectionBoxTest::RunTest(const FString&)
{
	using namespace ForceSelectionPolicy;
	const FVector2D Corners[] = { FVector2D(10., 20.), FVector2D(30., 20.), FVector2D(10., 60.), FVector2D(30., 60.) };
	// Both axes can be reversed independently; every edge is inclusive.
	for (const FVector2D& Start : Corners)
	{
		const FVector2D End(40. - Start.X, 80. - Start.Y);
		TestTrue(TEXT("Interior badge is inside"), IsInScreenBox(FVector2D(20., 40.), Start, End));
		for (const FVector2D& Corner : Corners)
			TestTrue(TEXT("Corner badge is inside"), IsInScreenBox(Corner, Start, End));
		TestTrue(TEXT("Left edge badge is inside"), IsInScreenBox(FVector2D(10., 40.), Start, End));
		TestTrue(TEXT("Bottom edge badge is inside"), IsInScreenBox(FVector2D(20., 60.), Start, End));
		TestFalse(TEXT("Just left is outside"), IsInScreenBox(FVector2D(9.99, 40.), Start, End));
		TestFalse(TEXT("Just right is outside"), IsInScreenBox(FVector2D(30.01, 40.), Start, End));
		TestFalse(TEXT("Just above is outside"), IsInScreenBox(FVector2D(20., 19.99), Start, End));
		TestFalse(TEXT("Just below is outside"), IsInScreenBox(FVector2D(20., 60.01), Start, End));
	}
	const FVector2D Point(17., 29.);
	TestTrue(TEXT("Zero-area box includes its exact point"), IsInScreenBox(Point, Point, Point));
	TestFalse(TEXT("Zero-area box excludes a neighbour"), IsInScreenBox(Point + FVector2D(.01, 0.), Point, Point));
	TestTrue(TEXT("Zero-width box includes points along its line"), IsInScreenBox(FVector2D(10., 40.), Corners[0], Corners[2]));
	TestFalse(TEXT("Zero-width box excludes off-line points"), IsInScreenBox(FVector2D(10.01, 40.), Corners[0], Corners[2]));
	return true;
}
#endif
