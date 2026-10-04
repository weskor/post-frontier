#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/SupplyDeliveryPolicy.h"

// Pure rule tests: no world, no actors. The picture is a ring of six regions
// 0-1-2-3 on one side and 0-4-3 on the other, with 5 hanging off 3.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSupplyDelayTest, "CoopRTS.Rules.SupplyDelivery.Delay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSupplyHopsTest, "CoopRTS.Rules.SupplyDelivery.Hops",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSupplyTransitionsTest, "CoopRTS.Rules.SupplyDelivery.Transitions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSupplyRefundTest, "CoopRTS.Rules.SupplyDelivery.Refund",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
constexpr uint64 Bit(int32 Region) { return uint64(1) << Region; }

struct FRing
{
	uint64 Graph[6] = {};
	FRing()
	{
		const int32 Links[][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 0, 4 }, { 4, 3 }, { 3, 5 } };
		for (const int32* Link : Links)
		{
			Graph[Link[0]] |= Bit(Link[1]);
			Graph[Link[1]] |= Bit(Link[0]);
		}
	}
	int32 Hops(uint64 Connected, int32 From, int32 To) const { return SupplyDelivery::Hops(Graph, 6, Connected, From, To); }
};
constexpr uint64 All = 0b111111;
}

bool FSupplyDelayTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Same region still costs the base delay"), SupplyDelivery::Delay(0), 4.);
	TestEqual(TEXT("One hop adds two seconds"), SupplyDelivery::Delay(1), 6.);
	TestEqual(TEXT("A force two hops out is served in eight seconds"), SupplyDelivery::Delay(2), 8.);
	TestEqual(TEXT("Five hops"), SupplyDelivery::Delay(5), 14.);
	TestEqual(TEXT("A negative hop count never shortens the base delay"), SupplyDelivery::Delay(-3), 4.);
	return true;
}

bool FSupplyHopsTest::RunTest(const FString& Parameters)
{
	const FRing Ring;
	TestEqual(TEXT("Same region is zero hops"), Ring.Hops(All, 2, 2), 0);
	TestEqual(TEXT("Neighbours are one hop"), Ring.Hops(All, 0, 1), 1);
	TestEqual(TEXT("The short side of the ring wins"), Ring.Hops(All, 0, 3), 2);
	TestEqual(TEXT("Hops are symmetric"), Ring.Hops(All, 3, 0), 2);
	TestEqual(TEXT("A region behind the ring"), Ring.Hops(All, 0, 5), 3);
	TestEqual(TEXT("Losing a region on the short side lengthens the path"), Ring.Hops(All & ~Bit(4), 0, 3), 3);
	TestEqual(TEXT("Losing both sides of the ring disconnects"), Ring.Hops(All & ~Bit(4) & ~Bit(2), 0, 3), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("A start outside the connected mask has no route"), Ring.Hops(All & ~Bit(0), 0, 3), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("An end outside the connected mask has no route"), Ring.Hops(All & ~Bit(3), 0, 3), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Out-of-range start"), Ring.Hops(All, -1, 3), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Out-of-range end"), Ring.Hops(All, 0, 6), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("No graph"), SupplyDelivery::Hops(nullptr, 6, All, 0, 1), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Empty graph"), SupplyDelivery::Hops(Ring.Graph, 0, All, 0, 0), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Oversized graph"), SupplyDelivery::Hops(Ring.Graph, 65, All, 0, 1), static_cast<int32>(INDEX_NONE));

	TestEqual(TEXT("No living member is an empty route"), static_cast<int32>(SupplyDelivery::Route(true, 2)), static_cast<int32>(SupplyDelivery::ERoute::Empty));
	TestEqual(TEXT("An empty force is empty even with no path"), static_cast<int32>(SupplyDelivery::Route(true, INDEX_NONE)), static_cast<int32>(SupplyDelivery::ERoute::Empty));
	TestEqual(TEXT("A path makes a route"), static_cast<int32>(SupplyDelivery::Route(false, 0)), static_cast<int32>(SupplyDelivery::ERoute::Connected));
	TestEqual(TEXT("No path is cut off"), static_cast<int32>(SupplyDelivery::Route(false, INDEX_NONE)), static_cast<int32>(SupplyDelivery::ERoute::CutOff));
	return true;
}

bool FSupplyTransitionsTest::RunTest(const FString& Parameters)
{
	using namespace SupplyDelivery;
	const auto Is = [&](const TCHAR* What, EOutcome Actual, EOutcome Expected) {
		TestEqual(What, static_cast<int32>(Actual), static_cast<int32>(Expected));
	};

	// A new recruit starts its delay at the first connected evaluation and arrives exactly 4 s + 2 s per hop later.
	FRecruit Recruit;
	Is(TEXT("The first connected evaluation only starts the clock"), Advance(Recruit, ERoute::Connected, 2, 100.), EOutcome::Pending);
	TestFalse(TEXT("The recruit is now in transit"), Recruit.bWaiting);
	TestEqual(TEXT("It is due eight seconds out for two hops"), Recruit.ReadyAt, 108.);
	Is(TEXT("Just before the delay it is still in transit"), Advance(Recruit, ERoute::Connected, 2, 107.99), EOutcome::Pending);
	Is(TEXT("At the delay it arrives"), Advance(Recruit, ERoute::Connected, 2, 108.), EOutcome::Arrive);

	// Cutting the chain sends the recruit back to the producer; the force moving does not change its due time.
	FRecruit Cut;
	Advance(Cut, ERoute::Connected, 1, 10.);
	Is(TEXT("A cut chain holds an in-transit recruit"), Advance(Cut, ERoute::CutOff, INDEX_NONE, 12.), EOutcome::Pending);
	TestTrue(TEXT("The held recruit waits at the producer"), Cut.bWaiting);
	Is(TEXT("A held recruit never arrives while cut off"), Advance(Cut, ERoute::CutOff, INDEX_NONE, 1000.), EOutcome::Pending);
	TestTrue(TEXT("It keeps waiting however long the cut lasts"), Cut.bWaiting);

	// Reconnecting restarts the whole delay, not the remainder.
	Is(TEXT("Reconnecting starts the clock again"), Advance(Cut, ERoute::Connected, 3, 1000.), EOutcome::Pending);
	TestEqual(TEXT("The restarted delay is the full 4 s + 2 s per hop"), Cut.ReadyAt, 1010.);
	Is(TEXT("The restarted delay holds until it ends"), Advance(Cut, ERoute::Connected, 3, 1009.9), EOutcome::Pending);
	Is(TEXT("Then it arrives"), Advance(Cut, ERoute::Connected, 3, 1010.), EOutcome::Arrive);

	// An empty force has nobody to deliver to: in transit or waiting, the recruit leaves the producer's exit.
	FRecruit Flying;
	Advance(Flying, ERoute::Connected, 4, 0.);
	Is(TEXT("A wiped force takes its recruit in transit at the exit"), Advance(Flying, ERoute::Empty, INDEX_NONE, 1.), EOutcome::AtProducer);
	TestTrue(TEXT("It is held at the producer until the exit takes it"), Flying.bWaiting);
	Is(TEXT("A waiting recruit of an empty force is offered the exit too"), Advance(Flying, ERoute::Empty, INDEX_NONE, 2.), EOutcome::AtProducer);

	// Counting keeps in-transit and waiting recruits apart.
	FRecruit Waiting, Moving;
	Advance(Moving, ERoute::Connected, 0, 0.);
	const FCounts Counts = Count({ Waiting, Moving, Moving });
	TestEqual(TEXT("Recruits in transit"), Counts.InTransit, 2);
	TestEqual(TEXT("Recruits waiting at the producer"), Counts.Waiting, 1);
	const FCounts None = Count({});
	TestTrue(TEXT("No recruits, no counts"), None.InTransit == 0 && None.Waiting == 0);
	return true;
}

bool FSupplyRefundTest::RunTest(const FString& Parameters)
{
	using namespace SupplyDelivery;
	FRecruit A, B, C;
	A.Paid = 45;
	B.Paid = 45;
	B.bWaiting = false;
	C.Paid = 35;
	TestEqual(TEXT("A dead producer returns exactly what its recruits cost, in transit or waiting"), Refund({ A, B, C }), 125);
	TestEqual(TEXT("Nothing pending refunds nothing"), Refund({}), 0);
	C.Paid = -10;
	TestEqual(TEXT("A refund is never negative"), Refund({ A, C }), 45);
	return true;
}
#endif
