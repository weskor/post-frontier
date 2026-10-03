#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ForceOrderPolicy.h"
#include <limits>

// Pure rule tests: no world, no actors. Values are arbitrary; assertions are invariants.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceWaypointTest, "CoopRTS.Rules.ForceOrders.NextWaypoint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceConnectedTest, "CoopRTS.Rules.ForceOrders.ConnectedControl",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceSafeRegionTest, "CoopRTS.Rules.ForceOrders.SafeRegion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceThresholdTest, "CoopRTS.Rules.ForceOrders.Thresholds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceQueueTest, "CoopRTS.Rules.ForceOrders.QueueCapacity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceSpeedTest, "CoopRTS.Rules.ForceOrders.TravelSpeed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
// Eight regions: 0-1-3-6-5-4-2-0 ring, isolated 7.
const uint64 ControlGraph[] = {
	(uint64(1) << 1) | (uint64(1) << 2),
	(uint64(1) << 0) | (uint64(1) << 3),
	(uint64(1) << 0) | (uint64(1) << 4),
	(uint64(1) << 1) | (uint64(1) << 6),
	(uint64(1) << 2) | (uint64(1) << 5),
	(uint64(1) << 4) | (uint64(1) << 6),
	(uint64(1) << 3) | (uint64(1) << 5), 0
};
}

bool FForceWaypointTest::RunTest(const FString& Parameters)
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
		ForceOrders::NextWaypoint(Graph, 5, 0, 3), 1);
	TestEqual(TEXT("Reverse traversal also resolves ties deterministically"),
		ForceOrders::NextWaypoint(Graph, 5, 3, 0), 1);
	TestEqual(TEXT("An adjacent target is the next waypoint"),
		ForceOrders::NextWaypoint(Graph, 5, 0, 2), 2);
	TestEqual(TEXT("An already reached target returns the start"),
		ForceOrders::NextWaypoint(Graph, 5, 3, 3), 3);
	TestEqual(TEXT("An isolated start already at its target remains valid"),
		ForceOrders::NextWaypoint(Graph, 5, 4, 4), 4);
	TestEqual(TEXT("Cycles do not make an unreachable target reachable"),
		ForceOrders::NextWaypoint(Graph, 5, 0, 4), INDEX_NONE);
	TestEqual(TEXT("Isolated start cannot reach the connected component"),
		ForceOrders::NextWaypoint(Graph, 5, 4, 0), INDEX_NONE);

	// The lower-index branch is longer: 0-1-2-3 versus 0-4-3.
	const uint64 Unequal[] = {
		(uint64(1) << 1) | (uint64(1) << 4), uint64(1) << 2,
		uint64(1) << 3, 0, uint64(1) << 3
	};
	TestEqual(TEXT("Shortest path beats the first ascending-index branch"),
		ForceOrders::NextWaypoint(Unequal, 5, 0, 3), 4);
	TestEqual(TEXT("Missing graph rejects even an already reached target"),
		ForceOrders::NextWaypoint(nullptr, 5, 0, 0), INDEX_NONE);
	TestEqual(TEXT("Empty graph rejects"), ForceOrders::NextWaypoint(Graph, 0, 0, 0), INDEX_NONE);
	TestEqual(TEXT("Negative start rejects"), ForceOrders::NextWaypoint(Graph, 5, -1, 3), INDEX_NONE);
	TestEqual(TEXT("Out-of-range start rejects"), ForceOrders::NextWaypoint(Graph, 5, 5, 3), INDEX_NONE);
	TestEqual(TEXT("Negative target rejects"), ForceOrders::NextWaypoint(Graph, 5, 0, -1), INDEX_NONE);
	TestEqual(TEXT("Out-of-range target rejects"), ForceOrders::NextWaypoint(Graph, 5, 0, 5), INDEX_NONE);

	uint64 Wide[ForceOrders::MaxRegions] = {};
	Wide[0] = uint64(1) << 63;
	Wide[63] = uint64(1) << 0;
	TestEqual(TEXT("Highest supported region bit is traversable"),
		ForceOrders::NextWaypoint(Wide, ForceOrders::MaxRegions, 0, 63), 63);
	TestEqual(TEXT("Highest supported region can be the start"),
		ForceOrders::NextWaypoint(Wide, ForceOrders::MaxRegions, 63, 0), 0);
	TestEqual(TEXT("Adjacency bits outside the supplied graph are ignored"),
		ForceOrders::NextWaypoint(Wide, 2, 0, 1), INDEX_NONE);
	TestEqual(TEXT("Oversized graph rejects before reading it"),
		ForceOrders::NextWaypoint(Wide, ForceOrders::MaxRegions + 1, 0, 1), INDEX_NONE);
	return true;
}

bool FForceConnectedTest::RunTest(const FString& Parameters)
{
	// Owned region 3 is cut off by uncontrolled 1; 5 remains connected through 2-4.
	const uint64* Graph = ControlGraph;
	const uint64 Controlled = (uint64(1) << 0) | (uint64(1) << 2) | (uint64(1) << 3)
		| (uint64(1) << 4) | (uint64(1) << 5) | (uint64(1) << 7);
	const uint64 Connected = Controlled & ~((uint64(1) << 3) | (uint64(1) << 7));
	TestEqual(TEXT("Control must connect to HQ through controlled regions"),
		ForceOrders::ConnectedMask(Graph, 8, 0, Controlled), Connected);
	TestEqual(TEXT("Uncontrolled HQ has no controlled component"),
		ForceOrders::ConnectedMask(Graph, 8, 1, Controlled), uint64(0));
	TestEqual(TEXT("Isolated controlled HQ connects only itself"),
		ForceOrders::ConnectedMask(Graph, 8, 7, Controlled), uint64(1) << 7);
	TestEqual(TEXT("Missing graph rejects"), ForceOrders::ConnectedMask(nullptr, 8, 0, Controlled), uint64(0));
	TestEqual(TEXT("Empty graph rejects"), ForceOrders::ConnectedMask(Graph, 0, 0, Controlled), uint64(0));
	TestEqual(TEXT("Negative count rejects"), ForceOrders::ConnectedMask(Graph, -1, 0, Controlled), uint64(0));
	TestEqual(TEXT("Oversized graph rejects"), ForceOrders::ConnectedMask(Graph, 65, 0, Controlled), uint64(0));
	TestEqual(TEXT("Negative HQ rejects"), ForceOrders::ConnectedMask(Graph, 8, -1, Controlled), uint64(0));
	TestEqual(TEXT("Out-of-range HQ rejects"), ForceOrders::ConnectedMask(Graph, 8, 8, Controlled), uint64(0));
	uint64 Wide[ForceOrders::MaxRegions] = {};
	Wide[0] = uint64(1) << 63;
	Wide[63] = uint64(1) << 0;
	const uint64 Endpoints = uint64(1) | (uint64(1) << 63);
	TestEqual(TEXT("Highest bit is included in connected control"),
		ForceOrders::ConnectedMask(Wide, 64, 63, Endpoints), Endpoints);
	TestEqual(TEXT("Out-of-range control and adjacency bits are ignored"),
		ForceOrders::ConnectedMask(Wide, 1, 0, Endpoints), uint64(1));
	return true;
}

bool FForceSafeRegionTest::RunTest(const FString& Parameters)
{
	const uint64* Graph = ControlGraph;
	const uint64 Controlled = (uint64(1) << 6) - 1;
	const uint64 Hostiles = uint64(1) | (uint64(1) << 1) | (uint64(1) << 2);
	TestEqual(TEXT("Last held safe region wins over a nearer safe region"),
		ForceOrders::SafeRegion(Graph, 8, 6, 0, 4, Controlled, Hostiles), 4);
	TestEqual(TEXT("Equal nearest safe regions choose the lower index"),
		ForceOrders::SafeRegion(Graph, 8, 6, 0, INDEX_NONE, Controlled, Hostiles), 3);
	TestEqual(TEXT("Hostile last held is ignored"),
		ForceOrders::SafeRegion(Graph, 8, 6, 0, 3, Controlled, Hostiles | (uint64(1) << 3)), 5);
	TestEqual(TEXT("Cut-off last held is not safe despite being controlled"),
		ForceOrders::SafeRegion(Graph, 8, 6, 0, 3, Controlled & ~(uint64(1) << 1), Hostiles), 5);
	TestEqual(TEXT("An already safe source is nearest"),
		ForceOrders::SafeRegion(Graph, 8, 4, 0, INDEX_NONE, Controlled, Hostiles), 4);
	TestEqual(TEXT("No safe region falls back to HQ even when HQ is hostile"),
		ForceOrders::SafeRegion(Graph, 8, 6, 0, 4, Controlled, Controlled), 0);
	TestEqual(TEXT("No controlled HQ component falls back to HQ"),
		ForceOrders::SafeRegion(Graph, 8, 6, 0, 4, Controlled & ~uint64(1), 0), 0);
	TestEqual(TEXT("No reachable safe region falls back to HQ"),
		ForceOrders::SafeRegion(Graph, 8, 7, 0, 4, Controlled, Hostiles), 0);
	TestEqual(TEXT("Out-of-range last held is ignored"),
		ForceOrders::SafeRegion(Graph, 8, 6, 0, 8, Controlled, Hostiles), 3);

	// At equal distance BFS discovers 5 before 4; safe-region ties still choose 4.
	const uint64 Ties[] = { (uint64(1) << 1) | (uint64(1) << 2), uint64(1) << 5,
		uint64(1) << 4, (uint64(1) << 4) | (uint64(1) << 5), 0, 0 };
	const uint64 SafeControl = (uint64(1) << 3) | (uint64(1) << 4) | (uint64(1) << 5);
	TestEqual(TEXT("Tie uses destination index, not BFS discovery order"),
		ForceOrders::SafeRegion(Ties, 6, 0, 3, INDEX_NONE, SafeControl, uint64(1) << 3), 4);
	const uint64 UnreachableHeld[] = { uint64(1) << 1, uint64(1) << 5, 0,
		(uint64(1) << 4) | (uint64(1) << 5), 0, 0 };
	TestEqual(TEXT("Connected last held must also be reachable from the source"),
		ForceOrders::SafeRegion(UnreachableHeld, 6, 0, 3, 4, SafeControl, uint64(1) << 3), 5);
	TestEqual(TEXT("Missing graph rejects before HQ fallback"),
		ForceOrders::SafeRegion(nullptr, 8, 6, 0, 4, Controlled, Hostiles), INDEX_NONE);
	TestEqual(TEXT("Empty graph rejects"),
		ForceOrders::SafeRegion(Graph, 0, 6, 0, 4, Controlled, Hostiles), INDEX_NONE);
	TestEqual(TEXT("Oversized graph rejects"),
		ForceOrders::SafeRegion(Graph, 65, 6, 0, 4, Controlled, Hostiles), INDEX_NONE);
	TestEqual(TEXT("Negative source rejects"),
		ForceOrders::SafeRegion(Graph, 8, -1, 0, 4, Controlled, Hostiles), INDEX_NONE);
	TestEqual(TEXT("Out-of-range source rejects"),
		ForceOrders::SafeRegion(Graph, 8, 8, 0, 4, Controlled, Hostiles), INDEX_NONE);
	TestEqual(TEXT("Negative HQ rejects"),
		ForceOrders::SafeRegion(Graph, 8, 6, -1, 4, Controlled, Hostiles), INDEX_NONE);
	TestEqual(TEXT("Out-of-range HQ rejects"),
		ForceOrders::SafeRegion(Graph, 8, 6, 8, 4, Controlled, Hostiles), INDEX_NONE);
	uint64 Wide[ForceOrders::MaxRegions] = {};
	Wide[0] = uint64(1) << 63;
	Wide[63] = uint64(1) << 0;
	TestEqual(TEXT("Highest region bit can be the safe destination"),
		ForceOrders::SafeRegion(Wide, 64, 0, 63, INDEX_NONE, uint64(1) | (uint64(1) << 63), uint64(1)), 63);
	return true;
}

bool FForceThresholdTest::RunTest(const FString& Parameters)
{
	for (const uint8 Threshold : { uint8(25), uint8(40), uint8(60) })
	{
		TestTrue(TEXT("Below selected threshold withdraws"), ForceOrders::ShouldWithdraw(Threshold - 1, 100, Threshold));
		TestFalse(TEXT("Exactly selected threshold does not withdraw"), ForceOrders::ShouldWithdraw(Threshold, 100, Threshold));
		TestFalse(TEXT("Above selected threshold does not withdraw"), ForceOrders::ShouldWithdraw(Threshold + 1, 100, Threshold));
	}
	TestFalse(TEXT("Never threshold disables withdrawal even at zero strength"), ForceOrders::ShouldWithdraw(0, 4, 0));
	TestTrue(TEXT("Fractional threshold is compared without integer truncation"), ForceOrders::ShouldWithdraw(1, 3, 40));
	TestFalse(TEXT("Exact quarter strength stays at 25 percent"), ForceOrders::ShouldWithdraw(1, 4, 25));
	TestFalse(TEXT("Zero capacity does not withdraw"), ForceOrders::ShouldWithdraw(0, 0, 40));
	TestFalse(TEXT("Negative capacity does not withdraw"), ForceOrders::ShouldWithdraw(0, -1, 40));
	TestFalse(TEXT("Negative alive count rejects"), ForceOrders::ShouldWithdraw(-1, 4, 40));
	TestFalse(TEXT("Percentage above 100 rejects"), ForceOrders::ShouldWithdraw(0, 4, 101));
	TestTrue(TEXT("Percentage products do not overflow at maximum capacity"), ForceOrders::ShouldWithdraw(1, MAX_int32, 60));
	TestFalse(TEXT("Full maximum capacity does not withdraw"), ForceOrders::ShouldWithdraw(MAX_int32, MAX_int32, 60));

	const int32 Expected[] = { 0, 1, 2, 3, 4, 4, 5 };
	for (int32 ForceSlots = 1; ForceSlots <= 6; ++ForceSlots)
	{
		TestEqual(TEXT("Resume count rounds 80 percent upward"), ForceOrders::ResumeCount(ForceSlots), Expected[ForceSlots]);
		TestFalse(TEXT("One below resume count cannot resume"), ForceOrders::ShouldResume(Expected[ForceSlots] - 1, ForceSlots));
		TestTrue(TEXT("Exactly resume count resumes"), ForceOrders::ShouldResume(Expected[ForceSlots], ForceSlots));
		TestTrue(TEXT("Full capacity resumes"), ForceOrders::ShouldResume(ForceSlots, ForceSlots));
	}
	TestEqual(TEXT("Zero capacity has zero resume count"), ForceOrders::ResumeCount(0), 0);
	TestEqual(TEXT("Negative capacity has zero resume count"), ForceOrders::ResumeCount(-1), 0);
	TestFalse(TEXT("Zero capacity does not resume"), ForceOrders::ShouldResume(0, 0));
	TestFalse(TEXT("Negative capacity does not resume"), ForceOrders::ShouldResume(1, -1));
	TestFalse(TEXT("Negative joined strength does not resume"), ForceOrders::ShouldResume(-1, 4));
	TestEqual(TEXT("Maximum capacity resume count avoids overflow"), ForceOrders::ResumeCount(MAX_int32), 1717986918);
	return true;
}

bool FForceQueueTest::RunTest(const FString& Parameters)
{
	for (int32 Count = 0; Count < 3; ++Count)
		TestTrue(TEXT("Vacant total order slot can be queued"), ForceOrders::CanQueue(Count, true));
	TestFalse(TEXT("Three total orders include active and fill queue"), ForceOrders::CanQueue(3, true));
	TestFalse(TEXT("Overfull queue cannot append"), ForceOrders::CanQueue(4, true));
	TestTrue(TEXT("Replacement is allowed when all total slots are full"), ForceOrders::CanQueue(3, false));
	TestTrue(TEXT("Replacement can repair an overfull queue"), ForceOrders::CanQueue(4, false));
	TestFalse(TEXT("Negative queued count rejects"), ForceOrders::CanQueue(-1, true));
	TestFalse(TEXT("Negative replacement count rejects"), ForceOrders::CanQueue(-1, false));
	return true;
}

bool FForceSpeedTest::RunTest(const FString& Parameters)
{
	const float Speeds[] = { 420.f, 180.f, 300.f };
	TestEqual(TEXT("Mixed selection uses the slowest member"), ForceOrders::SlowestSpeed(MakeArrayView(Speeds)), 180.f);
	const float Reordered[] = { 180.f, 420.f, 300.f };
	TestEqual(TEXT("Slowest speed does not depend on selection order"), ForceOrders::SlowestSpeed(MakeArrayView(Reordered)), 180.f);
	const float Single[] = { 320.f };
	TestEqual(TEXT("Single member retains its base speed"), ForceOrders::SlowestSpeed(MakeArrayView(Single)), 320.f);
	TestEqual(TEXT("No members have no travel speed"), ForceOrders::SlowestSpeed(TConstArrayView<float>()), 0.f);
	const float Stopped[] = { 420.f, 0.f };
	TestEqual(TEXT("A zero-speed empty orphan does not constrain a moving selection"), ForceOrders::SlowestSpeed(MakeArrayView(Stopped)), 420.f);
	const float OnlyStopped[] = { 0.f, 0.f };
	TestEqual(TEXT("Only zero-speed empty orphans have no travel speed"), ForceOrders::SlowestSpeed(MakeArrayView(OnlyStopped)), 0.f);
	const float Negative[] = { 420.f, -1.f };
	TestEqual(TEXT("Negative member speed rejects"), ForceOrders::SlowestSpeed(MakeArrayView(Negative)), 0.f);
	const float Infinite[] = { 420.f, std::numeric_limits<float>::infinity() };
	const float NotANumber[] = { std::numeric_limits<float>::quiet_NaN(), 180.f };
	TestEqual(TEXT("Infinite member speed rejects"), ForceOrders::SlowestSpeed(MakeArrayView(Infinite)), 0.f);
	TestEqual(TEXT("NaN member speed rejects"), ForceOrders::SlowestSpeed(MakeArrayView(NotANumber)), 0.f);
	TestEqual(TEXT("Ordinary travel retains the base speed"), ForceOrders::TravelSpeed(180.f, false), 180.f);
	TestEqual(TEXT("Retreat adds exactly 25 percent to selection speed"),
		ForceOrders::TravelSpeed(ForceOrders::SlowestSpeed(MakeArrayView(Speeds)), true), 225.f);
	TestEqual(TEXT("Retreat does not move a stationary force"), ForceOrders::TravelSpeed(0.f, true), 0.f);
	TestEqual(TEXT("Negative base speed rejects"), ForceOrders::TravelSpeed(-1.f, true), 0.f);
	TestEqual(TEXT("Infinite base speed rejects"), ForceOrders::TravelSpeed(Infinite[1], true), 0.f);
	TestEqual(TEXT("NaN base speed rejects"), ForceOrders::TravelSpeed(NotANumber[0], false), 0.f);
	return true;
}

#endif
