#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/CombatRangePolicy.h"

// Pure rule tests for the one weapon-range distance: attacker capsule to the target's edge.

namespace
{
struct FStructureCase
{
	const TCHAR* Name;
	float HalfSize;
};
// Footprint or hit-box half sizes: Barracks, Drill Rig, Workshop (Build/Content/buildings.json), HQ and Failover Node (hit boxes).
constexpr FStructureCase Structures[] = { { TEXT("Barracks"), 125.f }, { TEXT("Drill Rig"), 95.f }, { TEXT("Workshop"), 145.f },
	{ TEXT("HQ"), 150.f }, { TEXT("Failover Node"), 150.f } };
constexpr float Capsule = 34.f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatRangeUnitsTest, "CoopRTS.Rules.Combat.RangeUnits",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCombatRangeUnitsTest::RunTest(const FString& Parameters)
{
	using namespace CombatRangePolicy;
	// A unit is a point: range stays centre to centre, as the duel matrix was balanced.
	const FVector Center(100.f, 50.f, 7.f);
	const FVector2D Origin(100., 50.);
	const FRangeTarget Unit(Center);
	TestEqual(TEXT("A unit's distance is the centre distance"), EdgeDistance(Origin + FVector2D(300., 0.), Unit), 300.);
	TestEqual(TEXT("Direction does not matter for a point"), EdgeDistance(Origin + FVector2D(0., -300.), Unit), 300.);
	TestEqual(TEXT("On top of it the distance is zero"), EdgeDistance(Origin, Unit), 0.);
	for (const float Range : { 175.f, 300.f, 550.f, 1150.f })
	{
		TestTrue(TEXT("A unit is in range exactly at its weapon range"), InRange(Origin + FVector2D(Range, 0.), Unit, Range));
		TestFalse(TEXT("One centimetre further is out of range"), InRange(Origin + FVector2D(Range + 1., 0.), Unit, Range));
	}
	TestEqual(TEXT("Height is ignored"), EdgeDistance(FVector2D(300., 50.), FRangeTarget(FVector(100.f, 50.f, 9999.f))), 200.);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatRangeStructuresTest, "CoopRTS.Rules.Combat.RangeStructures",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCombatRangeStructuresTest::RunTest(const FString& Parameters)
{
	using namespace CombatRangePolicy;
	const FVector Center(-400.f, 900.f, 0.f);
	const FVector2D Origin(-400., 900.);
	for (const FStructureCase& Case : Structures)
	{
		const FString Name = Case.Name;
		const FRangeTarget Target = Box(Center, FVector2D(Case.HalfSize, Case.HalfSize), 0.f, Capsule);
		for (const FVector2D& Side : { FVector2D(1., 0.), FVector2D(-1., 0.), FVector2D(0., 1.), FVector2D(0., -1.) })
			TestEqual(*(Name + TEXT(": straight on, the edge is the centre distance less the half size and the capsule")),
				EdgeDistance(Origin + Side * (Case.HalfSize + Capsule + 100.), Target), 100.);
		// The corner: 100 cm off each axis, so sqrt(2) * 100 from the corner point.
		TestEqual(*(Name + TEXT(": the corner is measured to the corner point")),
			EdgeDistance(Origin + FVector2D(Case.HalfSize + 100., Case.HalfSize + 100.), Target), FMath::Sqrt(2.) * 100. - Capsule);
		TestEqual(*(Name + TEXT(": inside the footprint the distance is zero")), EdgeDistance(Origin + FVector2D(10., -20.), Target), 0.);
		// Reaching the cutout (half size plus the capsule) must leave every unit band able to fire,
		// which centre-to-centre distance could not for a melee unit against the large footprints.
		TestTrue(*(Name + TEXT(": a melee unit at the footprint plus capsule is in range")),
			InRange(Origin + FVector2D(Case.HalfSize + Capsule, 0.), Target, 175.));
		TestTrue(*(Name + TEXT(": melee range reaches 175 cm past the cutout")),
			InRange(Origin + FVector2D(Case.HalfSize + Capsule + 175., 0.), Target, 175.));
		TestFalse(*(Name + TEXT(": and no further")),
			InRange(Origin + FVector2D(Case.HalfSize + Capsule + 176., 0.), Target, 175.));
		// A turned box: 45 degrees puts a corner on the +X axis.
		const FRangeTarget Turned = Box(Center, FVector2D(Case.HalfSize, Case.HalfSize), 45.f, Capsule);
		TestEqual(*(Name + TEXT(": a box turned 45 degrees points a corner along the axis")),
			EdgeDistance(Origin + FVector2D(Case.HalfSize * FMath::Sqrt(2.) + Capsule + 100., 0.), Turned), 100., 1e-6);
		const FRangeTarget Quarter = Box(Center, FVector2D(Case.HalfSize, Case.HalfSize), 90.f, Capsule);
		TestEqual(*(Name + TEXT(": a square turned a quarter is the same square")),
			EdgeDistance(Origin + FVector2D(Case.HalfSize + Capsule + 100., 30.), Quarter),
			EdgeDistance(Origin + FVector2D(Case.HalfSize + Capsule + 100., 30.), Target), 1e-6);
	}
	const FRangeTarget Long = Box(Center, FVector2D(300., 50.), 90.f, 0.f);
	TestEqual(TEXT("A box's own axes turn with its yaw: the long axis runs along Y at a quarter turn"),
		EdgeDistance(Origin + FVector2D(350., 0.), Long), 300.);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatRangeStandoffTest, "CoopRTS.Rules.Combat.RangeStandoff",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCombatRangeStandoffTest::RunTest(const FString& Parameters)
{
	using namespace CombatRangePolicy;
	const FVector Center(0.f, 0.f, 0.f);
	for (const FStructureCase& Case : Structures)
	{
		const FRangeTarget Target = Box(Center, FVector2D(Case.HalfSize, Case.HalfSize), 30.f, Capsule);
		for (const FVector2D& From : { FVector2D(900., 20.), FVector2D(-700., 600.), FVector2D(40., -1200.) })
			for (const double Standoff : { 0., 140., 495. })
			{
				const FVector2D Point = PointAtEdgeDistance(From, Target, Standoff);
				TestEqual(*(FString(Case.Name) + TEXT(": the standoff point is the requested distance from the edge")),
					EdgeDistance(Point, Target), Standoff, 1e-6);
				// It lies on the ray from the nearest body point through the starting point.
				const FVector2D Nearest = NearestPoint(From, Target);
				TestTrue(*(FString(Case.Name) + TEXT(": the standoff point is on the same side as the starting point")),
					FVector2D::DotProduct((Point - Nearest).GetSafeNormal(), (From - Nearest).GetSafeNormal()) > .999999);
			}
		const FVector2D Out = PointAtEdgeDistance(FVector2D(5., 5.), Target, 100.);
		TestEqual(*(FString(Case.Name) + TEXT(": a unit inside the footprint is led out to the standoff")), EdgeDistance(Out, Target), 100., 1e-6);
	}
	const FVector2D AtCentre = PointAtEdgeDistance(FVector2D::ZeroVector, FRangeTarget(Center), 100.);
	TestEqual(TEXT("A degenerate start has a defined direction"), EdgeDistance(AtCentre, FRangeTarget(Center)), 100., 1e-6);
	return true;
}

#endif
