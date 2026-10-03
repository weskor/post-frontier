#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ControllerInputPolicy.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FControllerInputTimingTest, "CoopRTS.Rules.ControllerInput.Timing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FControllerInputEdgePanTest, "CoopRTS.Rules.ControllerInput.EdgePan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FControllerInputGroundPointTest, "CoopRTS.Rules.ControllerInput.GroundPoint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FControllerInputPlacementWindowTest, "CoopRTS.Rules.ControllerInput.PlacementWindow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FControllerInputAlertCycleTest, "CoopRTS.Rules.ControllerInput.AlertCycle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FControllerInputMinimapTest, "CoopRTS.Rules.ControllerInput.Minimap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FControllerInputTimingTest::RunTest(const FString&)
{
	using namespace ControllerInputPolicy;
	TestTrue(TEXT("Second press exactly at the window is a double click"), IsDoubleClick(.3, 0.));
	TestFalse(TEXT("Second press after the window is a single click"), IsDoubleClick(.31, 0.));
	TestTrue(TEXT("A chord is live just inside its window"), IsBuildHotkeyLive(5.99, 4.));
	TestFalse(TEXT("A chord expires exactly at its window"), IsBuildHotkeyLive(6., 4.));
	TestEqual(TEXT("Fresh feedback is opaque"), FeedbackOpacity(0.), 1.f);
	TestEqual(TEXT("Feedback is opaque until its last second"), FeedbackOpacity(3.), 1.f);
	TestEqual(TEXT("Feedback is half faded half a second before expiry"), FeedbackOpacity(3.5), .5f);
	TestEqual(TEXT("Feedback is gone at expiry"), FeedbackOpacity(4.), 0.f);
	TestEqual(TEXT("Feedback never goes negative"), FeedbackOpacity(40.), 0.f);
	return true;
}

bool FControllerInputEdgePanTest::RunTest(const FString&)
{
	using namespace ControllerInputPolicy;
	const FIntPoint Size(1920, 1080);
	TestEqual(TEXT("Centre does not pan"), EdgePanAxis(FVector2D(960., 540.), Size), FVector2D::ZeroVector);
	TestEqual(TEXT("Top edge pans forward"), EdgePanAxis(FVector2D(960., 0.), Size), FVector2D(1., 0.));
	TestEqual(TEXT("Bottom edge pans back"), EdgePanAxis(FVector2D(960., 1079.), Size), FVector2D(-1., 0.));
	TestEqual(TEXT("Left edge pans left"), EdgePanAxis(FVector2D(0., 540.), Size), FVector2D(0., -1.));
	TestEqual(TEXT("Right edge pans right"), EdgePanAxis(FVector2D(1919., 540.), Size), FVector2D(0., 1.));
	TestEqual(TEXT("Corner pans on both axes"), EdgePanAxis(FVector2D(0., 0.), Size), FVector2D(1., -1.));
	TestEqual(TEXT("The margin is inclusive"), EdgePanAxis(FVector2D(960., 8.), Size), FVector2D(1., 0.));
	TestEqual(TEXT("Just inside the margin does not pan"), EdgePanAxis(FVector2D(960., 9.), Size), FVector2D::ZeroVector);
	TestEqual(TEXT("A mouse outside the viewport never pans"), EdgePanAxis(FVector2D(-1., 540.), Size), FVector2D::ZeroVector);
	TestEqual(TEXT("The far viewport edge is outside"), EdgePanAxis(FVector2D(1920., 540.), Size), FVector2D::ZeroVector);
	return true;
}

bool FControllerInputGroundPointTest::RunTest(const FString&)
{
	using namespace ControllerInputPolicy;
	FVector Point;
	TestTrue(TEXT("A ray descending to the ground hits it"), GroundPoint(FVector(0., 0., 100.), FVector(1., 0., -1.), Point));
	TestEqual(TEXT("The hit lies on the ray at Z = 0"), Point, FVector(100., 0., 0.));
	TestFalse(TEXT("A ray parallel to the ground never hits"), GroundPoint(FVector(0., 0., 100.), FVector(1., 0., 0.), Point));
	TestFalse(TEXT("A ray rising away from the ground never hits"), GroundPoint(FVector(0., 0., 100.), FVector(1., 0., 1.), Point));
	TestFalse(TEXT("A ray starting on the ground is not ahead of it"), GroundPoint(FVector(0., 0., 0.), FVector(1., 0., -1.), Point));
	TestFalse(TEXT("A non-finite intersection is rejected"), GroundPoint(FVector(0., 0., std::numeric_limits<double>::infinity()), FVector(1., 0., -1.), Point));
	return true;
}

bool FControllerInputPlacementWindowTest::RunTest(const FString&)
{
	using namespace ControllerInputPolicy;
	const FPlacementWindow Small = PlacementWindow(2);
	TestEqual(TEXT("A small footprint gets eight cells of padding per side"), Small.WindowCells, 18);
	TestEqual(TEXT("A small footprint's margin"), Small.Margin, 8);
	const FPlacementWindow Capped = PlacementWindow(10);
	TestEqual(TEXT("The window is capped at 22 cells per axis"), Capped.WindowCells, 22);
	TestEqual(TEXT("A capped window shrinks the margin"), Capped.Margin, 6);
	TestEqual(TEXT("An odd leftover cell goes to the far side"), PlacementWindow(7).Margin, 7);
	return true;
}

bool FControllerInputAlertCycleTest::RunTest(const FString&)
{
	using namespace ControllerInputPolicy;
	const int32 Sequences[] = { 3, 5, 8 };
	TestEqual(TEXT("An empty list has no alert"), AlertCycleSequence(TConstArrayView<int32>(), 0, 0), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("A new alert is focused first"), AlertCycleSequence(Sequences, 5, 0), 8);
	TestEqual(TEXT("Seen newest and focused newest steps back one"), AlertCycleSequence(Sequences, 8, 8), 5);
	TestEqual(TEXT("Stepping back again reaches the oldest"), AlertCycleSequence(Sequences, 8, 5), 3);
	TestEqual(TEXT("The oldest alert stays focused"), AlertCycleSequence(Sequences, 8, 3), 3);
	TestEqual(TEXT("Seen newest without a focused alert jumps to the newest"), AlertCycleSequence(Sequences, 8, 99), 8);
	return true;
}

bool FControllerInputMinimapTest::RunTest(const FString&)
{
	using namespace ControllerInputPolicy;
	const FVector2D Half(2000., 1000.), Origin(100., 50.);
	TestEqual(TEXT("The arena centre is the minimap centre"), MinimapPoint(FVector2D::ZeroVector, Half, Origin, 200.), FVector2D(200., 150.));
	TestEqual(TEXT("Positive world X is up-screen"), MinimapPoint(FVector2D(2000., 0.), Half, Origin, 200.), FVector2D(200., 50.));
	TestEqual(TEXT("Positive world Y is right-screen"), MinimapPoint(FVector2D(0., 1000.), Half, Origin, 200.), FVector2D(300., 150.));
	TestEqual(TEXT("A zero offset has no diamond distance"), MinimapDiamondDistance(FVector2D::ZeroVector, Half, 200.), 0.);
	TestEqual(TEXT("Distance is symmetric and adds both axes"), MinimapDiamondDistance(FVector2D(-2000., 1000.), Half, 200.), 200.);
	TestTrue(TEXT("A marker edge is inside its pick box"), IsWithinMarker(FVector2D(13., 7.), FVector2D(10., 10.), 3.));
	TestFalse(TEXT("Past the pick box on one axis misses"), IsWithinMarker(FVector2D(13.1, 10.), FVector2D(10., 10.), 3.));
	return true;
}
#endif
