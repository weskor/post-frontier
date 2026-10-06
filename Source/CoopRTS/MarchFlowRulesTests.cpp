#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/LanePolicy.h"
#include "Rules/MarchSpeedPolicy.h"
#include "Rules/PassThroughPolicy.h"
#include "Rules/RegionTraitPolicy.h"

// Pure rule tests: no world, no actors.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarchSpeedBandsTest, "CoopRTS.Rules.MarchFlow.SpeedBands",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarchSpeedMembersTest, "CoopRTS.Rules.MarchFlow.MemberLag",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarchOpenMemberTest, "CoopRTS.Rules.MarchFlow.OpenPerMember",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLaneAllocationTest, "CoopRTS.Rules.MarchFlow.LaneAllocation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLaneOffsetTest, "CoopRTS.Rules.MarchFlow.LaneOffsets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPassThroughRouteTest, "CoopRTS.Rules.MarchFlow.PassThrough",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMarchSpeedBandsTest::RunTest(const FString& Parameters)
{
	using namespace MarchSpeedPolicy;
	TestEqual(TEXT("A member on its slot keeps the force's speed"), Factor(0.f), 1.f);
	TestEqual(TEXT("Lag inside the band changes nothing, behind"), Factor(BandHalfWidth), 1.f);
	TestEqual(TEXT("Lag inside the band changes nothing, ahead"), Factor(-BandHalfWidth), 1.f);
	TestEqual(TEXT("Half the ramp behind is half the catch-up bonus"), Factor(BandHalfWidth + RampLength / 2.f), 1.f + (MaxCatchUp - 1.f) / 2.f);
	TestEqual(TEXT("Far behind the member runs at the catch-up limit"), Factor(BandHalfWidth + RampLength), MaxCatchUp);
	TestEqual(TEXT("The limit holds however far behind"), Factor(5000.f), MaxCatchUp);
	TestEqual(TEXT("Half the ramp ahead eases half way"), Factor(-(BandHalfWidth + RampLength / 2.f)), 1.f - (1.f - MinAhead) / 2.f);
	TestEqual(TEXT("Far ahead the member slows to the lower limit"), Factor(-5000.f), MinAhead);
	TestTrue(TEXT("The catch-up bonus is 10 to 15 percent"), MaxCatchUp >= 1.1f && MaxCatchUp <= 1.15f);
	return true;
}

bool FMarchSpeedMembersTest::RunTest(const FString& Parameters)
{
	using namespace MarchSpeedPolicy;
	const FVector2D Heading(1., 0.);
	// Two members on slots at the same place: one 300 cm behind the other along the heading.
	const FMember Pair[] = { { FVector2D(-300., 0.), FVector2D::ZeroVector }, { FVector2D(300., 0.), FVector2D::ZeroVector } };
	TestEqual(TEXT("The trailing member lags by its distance from the mean"), Lag(Pair, 0, Heading), 300.f);
	TestEqual(TEXT("The leading member is ahead by the same"), Lag(Pair, 1, Heading), -300.f);
	TArray<float, TInlineAllocator<8>> Factors;
	Factors.Reset();
	MarchSpeedPolicy::Factors(Pair, Heading, Factors);
	TestTrue(TEXT("The trailing member speeds up, the leading one eases"), Factors[0] > 1.f && Factors[1] < 1.f);

	// The same positions are in formation when the slots are that far apart.
	const FMember InFormation[] = { { FVector2D(-300., 0.), FVector2D(-300., 0.) }, { FVector2D(300., 0.), FVector2D(300., 0.) } };
	MarchSpeedPolicy::Factors(InFormation, Heading, Factors);
	TestTrue(TEXT("A formation spread along the heading is not a lag"), Factors[0] == 1.f && Factors[1] == 1.f);

	// Sideways offsets are not lag.
	const FMember Abreast[] = { { FVector2D(0., -900.), FVector2D::ZeroVector }, { FVector2D(0., 900.), FVector2D::ZeroVector } };
	MarchSpeedPolicy::Factors(Abreast, Heading, Factors);
	TestTrue(TEXT("Members abreast keep the force's speed"), Factors[0] == 1.f && Factors[1] == 1.f);

	MarchSpeedPolicy::Factors(Pair, FVector2D::ZeroVector, Factors);
	TestTrue(TEXT("Without a heading every member keeps the force's speed"), Factors[0] == 1.f && Factors[1] == 1.f);
	MarchSpeedPolicy::Factors(TConstArrayView<FMember>(Pair, 1), Heading, Factors);
	TestTrue(TEXT("A single member has nobody to keep up with"), Factors.Num() == 1 && Factors[0] == 1.f);
	return true;
}

bool FMarchOpenMemberTest::RunTest(const FString& Parameters)
{
	using namespace MarchSpeedPolicy;
	const float Open = RegionTraitPolicy::SpeedMultiplier(ERegionTrait::Open);
	const float Plain = RegionTraitPolicy::SpeedMultiplier(ERegionTrait::None);
	TestTrue(TEXT("Open applies to a member by its own region"), Open > 1.f && Plain == 1.f);
	// A member that runs ahead in Open ground is eased by the same factor as any other, so it ends up
	// no faster than the force: the bonus cannot pull the force apart.
	TestTrue(TEXT("An Open member far ahead of its slot runs no faster than the force"), Open * Factor(-5000.f) <= 1.f);
	TestTrue(TEXT("An Open member on its slot keeps the full bonus"), Open * Factor(0.f) == Open);
	// A plain member far behind catches up faster than an Open one on its slot.
	TestTrue(TEXT("A laggard on plain ground outruns an Open member in formation"), Plain * Factor(5000.f) > Open * Factor(0.f) * .99f);
	return true;
}

bool FLaneAllocationTest::RunTest(const FString& Parameters)
{
	using namespace LanePolicy;
	TestEqual(TEXT("The first force takes the anchor's own lane"), Allocate({}), 0);
	const int32 One[] = { 0 };
	TestEqual(TEXT("The next takes the next lane"), Allocate(One), 1);
	const int32 Gap[] = { 0, 2 };
	TestEqual(TEXT("A freed lane is taken before a later one"), Allocate(Gap), 1);
	const int32 WithNone[] = { INDEX_NONE, 0 };
	TestEqual(TEXT("Forces without a lane do not hold one"), Allocate(WithNone), 1);
	TArray<int32> Taken;
	for (int32 Force = 0; Force < 12; ++Force)
	{
		const int32 Lane = Allocate(Taken);
		TestFalse(TEXT("Twelve forces get twelve different lanes"), Taken.Contains(Lane));
		Taken.Add(Lane);
	}
	TestEqual(TEXT("Allocation is deterministic for the same set"), Allocate(Taken), Allocate(Taken));
	for (int32 Force = 12; Force < LaneCount; ++Force)
		Taken.Add(Allocate(Taken));
	TestEqual(TEXT("With every lane taken once the lanes repeat, lowest first"), Allocate(Taken), 0);
	return true;
}

bool FLaneOffsetTest::RunTest(const FString& Parameters)
{
	using namespace LanePolicy;
	const FVector2D Heading(0., 1.);
	TestEqual(TEXT("Lane 0 is the anchor"), Offset(0, Heading), FVector2D::ZeroVector);
	// Unreal axes: facing +Y, the right hand is -X.
	TestEqual(TEXT("Lane 1 is one spacing to the right of the heading"), Offset(1, Heading), FVector2D(-LaneSpacing, 0.));
	TestEqual(TEXT("Lane 2 is one spacing to the left"), Offset(2, Heading), FVector2D(LaneSpacing, 0.));
	TestEqual(TEXT("Lane 3 is two to the right"), Offset(3, Heading), FVector2D(-2.f * LaneSpacing, 0.));
	TestEqual(TEXT("The second rank lies behind the anchor"), Offset(LanesPerRank, Heading), FVector2D(0., -RankDepth));
	TestEqual(TEXT("The offset follows the heading: facing +X the right hand is +Y"), Offset(1, FVector2D(1., 0.)), FVector2D(0., LaneSpacing));
	TestEqual(TEXT("A zero heading is a fixed axis, not an error"), Offset(1, FVector2D::ZeroVector), Offset(1, FVector2D(1., 0.)));

	float Closest = TNumericLimits<float>::Max(), Farthest = 0.f;
	for (int32 A = 0; A < LaneCount; ++A)
	{
		Farthest = FMath::Max(Farthest, static_cast<float>(Offset(A, Heading).Size()));
		for (int32 B = A + 1; B < LaneCount; ++B)
			Closest = FMath::Min(Closest, static_cast<float>(FVector2D::Distance(Offset(A, Heading), Offset(B, Heading))));
	}
	TestTrue(TEXT("No two lanes are closer than a rank depth"), Closest >= RankDepth - .01f);
	TestTrue(TEXT("Every lane stays within 800 cm of the anchor"), Farthest <= 800.f);
	TestEqual(TEXT("Lanes repeat after LaneCount"), Offset(LaneCount + 1, Heading), Offset(1, Heading));
	return true;
}

bool FPassThroughRouteTest::RunTest(const FString& Parameters)
{
	using namespace PassThroughPolicy;
	// A line of six regions 0-1-2-3-4-5.
	uint64 Graph[6] = {};
	for (int32 Index = 0; Index < 6; ++Index)
	{
		if (Index > 0)
			Graph[Index] |= uint64(1) << (Index - 1);
		if (Index < 5)
			Graph[Index] |= uint64(1) << (Index + 1);
	}
	const auto Bit = [](int32 Region) { return uint64(1) << Region; };
	FRegions Open;
	Open.Controlled = Bit(1) | Bit(2) | Bit(3) | Bit(4);
	Open.Anchored = Bit(1) | Bit(2) | Bit(3) | Bit(4) | Bit(5);

	TestEqual(TEXT("Controlled ground is passed: one leg of MaxSegments regions"), Waypoint(Graph, 6, 0, 5, INDEX_NONE, Open), MaxSegments);
	TestEqual(TEXT("A target within the leg is the waypoint"), Waypoint(Graph, 6, 3, 5, INDEX_NONE, Open), 5);
	TestEqual(TEXT("An adjacent target is the waypoint"), Waypoint(Graph, 6, 4, 5, INDEX_NONE, Open), 5);

	FRegions Uncontrolled = Open;
	Uncontrolled.Controlled = Bit(1) | Bit(3) | Bit(4);
	TestEqual(TEXT("The leg ends at the first anchored region the team does not control"), Waypoint(Graph, 6, 0, 5, INDEX_NONE, Uncontrolled), 2);
	TestEqual(TEXT("Standing next to it the waypoint is still that region"), Waypoint(Graph, 6, 1, 5, INDEX_NONE, Uncontrolled), 2);

	FRegions NoAnchor = Uncontrolled;
	NoAnchor.Anchored &= ~Bit(2);
	TestEqual(TEXT("A region without a capture anchor is passed"), Waypoint(Graph, 6, 0, 5, INDEX_NONE, NoAnchor), MaxSegments);

	FRegions Hostile = Open;
	Hostile.Hostiles = Bit(2);
	TestEqual(TEXT("A hostile unit in a controlled region stops the leg there"), Waypoint(Graph, 6, 0, 5, INDEX_NONE, Hostile), 2);

	TestTrue(TEXT("Controlled anchored ground passes"), CanPass(Open, 2));
	TestFalse(TEXT("Uncontrolled anchored ground does not"), CanPass(Uncontrolled, 2));
	TestFalse(TEXT("Hostile ground does not"), CanPass(Hostile, 2));
	TestFalse(TEXT("A region outside the mask never passes"), CanPass(Open, 64));

	// Keeping the waypoint already applied.
	TestEqual(TEXT("A waypoint ahead with passable ground before it is kept"), Waypoint(Graph, 6, 1, 5, 3, Open), 3);
	TestEqual(TEXT("Without it the leg would end further on"), Waypoint(Graph, 6, 1, 5, INDEX_NONE, Open), 4);
	TestEqual(TEXT("It is dropped when ground before it needs securing"), Waypoint(Graph, 6, 1, 5, 3, Uncontrolled), 2);
	TestEqual(TEXT("It is dropped when a hostile unit stands before it"), Waypoint(Graph, 6, 1, 5, 3, Hostile), 2);
	TestEqual(TEXT("Standing in the applied waypoint orders on"), Waypoint(Graph, 6, 3, 5, 3, Open), 5);
	TestEqual(TEXT("A waypoint behind the force is not kept"), Waypoint(Graph, 6, 3, 5, 1, Open), 5);
	TestEqual(TEXT("A waypoint off the route is not kept"), Waypoint(Graph, 6, 3, 5, 0, Open), 5);

	TestEqual(TEXT("Standing in the target the route is the target"), Waypoint(Graph, 6, 5, 5, INDEX_NONE, Open), 5);
	uint64 Split[3] = { Bit(1), Bit(0), 0 };
	TestEqual(TEXT("An unreachable target has no waypoint"), Waypoint(Split, 3, 0, 2, INDEX_NONE, Open), static_cast<int32>(INDEX_NONE));
	return true;
}
#endif
