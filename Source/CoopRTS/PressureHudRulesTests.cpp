#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/PressureHud.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressureHudCutChipTest, "CoopRTS.Rules.PressureHud.CutChip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressureHudCutFocusTest, "CoopRTS.Rules.PressureHud.CutFocus",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressureHudStunChipTest, "CoopRTS.Rules.PressureHud.StunChip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressureHudStunWatchTest, "CoopRTS.Rules.PressureHud.StunWatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
FString Chip(int32 Regions, double Power, double Data)
{
	PressureHud::FCutLoss Loss;
	Loss.Regions = Regions;
	Loss.PowerPerSecond = Power;
	Loss.DataPerSecond = Data;
	TStringBuilder<96> Out;
	PressureHud::AppendCutChip(Out, Loss);
	return FString(Out.ToView());
}

FString Rate(double PerSecond)
{
	TStringBuilder<16> Out;
	PressureHud::AppendRate(Out, PerSecond);
	return FString(Out.ToView());
}

FString Stun(float Remaining)
{
	TStringBuilder<24> Out;
	PressureHud::AppendStunChip(Out, Remaining);
	return FString(Out.ToView());
}
}

bool FPressureHudCutChipTest::RunTest(const FString&)
{
	TestEqual(TEXT("Both rates and the region count"), Chip(2, 3., 1.), FString(TEXT("LINE CUT \u00D72  \u22123 Power/s  \u22121 Data/s")));
	TestEqual(TEXT("A cut that costs no Data lists only Power"), Chip(1, 1.5, 0.), FString(TEXT("LINE CUT \u00D71  \u22121.5 Power/s")));
	TestEqual(TEXT("A cut that costs no Power lists only Data"), Chip(1, 0., 1.), FString(TEXT("LINE CUT \u00D71  \u22121 Data/s")));
	TestEqual(TEXT("A cut region with nothing in it still reads as a cut"), Chip(3, 0., 0.), FString(TEXT("LINE CUT \u00D73")));
	TestEqual(TEXT("Whole rates print bare"), Rate(2.), FString(TEXT("2")));
	TestEqual(TEXT("Fractional rates print one decimal"), Rate(1.5), FString(TEXT("1.5")));
	TestEqual(TEXT("A share of four Power over three commanders rounds to a tenth"), Rate(4. / 3.), FString(TEXT("1.3")));
	TestEqual(TEXT("A rate within a twentieth of whole prints whole"), Rate(2.04), FString(TEXT("2")));
	PressureHud::FCutLoss Loss;
	TestFalse(TEXT("No cut regions, no chip"), Loss.Any());
	Loss.Regions = 1;
	TestTrue(TEXT("One cut region shows the chip"), Loss.Any());
	return true;
}

bool FPressureHudCutFocusTest::RunTest(const FString&)
{
	const int32 Cut[] = { 2, 5, 9 };
	TestEqual(TEXT("The first click focuses the first cut region"), PressureHud::NextCutFocus(Cut, INDEX_NONE), 2);
	TestEqual(TEXT("Repeated clicks advance"), PressureHud::NextCutFocus(Cut, 2), 5);
	TestEqual(TEXT("Repeated clicks advance again"), PressureHud::NextCutFocus(Cut, 5), 9);
	TestEqual(TEXT("After the last region the cycle wraps"), PressureHud::NextCutFocus(Cut, 9), 2);
	TestEqual(TEXT("A region that reconnected between clicks moves on to the next one above it"), PressureHud::NextCutFocus(Cut, 3), 5);
	TestEqual(TEXT("A previous region above every cut region wraps"), PressureHud::NextCutFocus(Cut, 40), 2);
	TestEqual(TEXT("Nothing cut, nothing to focus"), PressureHud::NextCutFocus(TConstArrayView<int32>(), 2), static_cast<int32>(INDEX_NONE));
	return true;
}

bool FPressureHudStunChipTest::RunTest(const FString&)
{
	TestEqual(TEXT("Tenths of a second"), Stun(2.4f), FString(TEXT("STUN 2.4s")));
	TestEqual(TEXT("A fresh pulse stun"), Stun(3.f), FString(TEXT("STUN 3.0s")));
	TestEqual(TEXT("A Jammer stun"), Stun(5.f), FString(TEXT("STUN 5.0s")));
	TestEqual(TEXT("Tenths round up so the chip never reads zero while it shows"), Stun(.04f), FString(TEXT("STUN 0.1s")));
	TestEqual(TEXT("Just past a tenth rounds up to the next"), Stun(2.41f), FString(TEXT("STUN 2.5s")));

	TestFalse(TEXT("A building never stunned shows no chip"), PressureHud::StunChip(50., -1., 0.f).bShown);
	TestFalse(TEXT("A stun that ended shows no chip"), PressureHud::StunChip(50., 50., 3.f).bShown);
	const PressureHud::FStunChip Start = PressureHud::StunChip(50., 53., 3.f);
	TestTrue(TEXT("A live stun shows the chip"), Start.bShown);
	TestEqual(TEXT("The drain bar is full at the start"), Start.Drain, 1.f);
	TestEqual(TEXT("The chip carries the remaining time"), Start.Remaining, 3.f);
	TestEqual(TEXT("The drain bar empties in proportion"), PressureHud::StunChip(51.5, 53., 3.f).Drain, .5f);
	TestEqual(TEXT("A Jammer's five seconds drain over five seconds"), PressureHud::StunChip(52.5, 55., 5.f).Drain, .5f);
	TestEqual(TEXT("Without a recorded start the bar reads full, never past it"), PressureHud::StunChip(50., 53., 0.f).Drain, 1.f);
	TestEqual(TEXT("The bar never exceeds full if the stun was refreshed longer"), PressureHud::StunChip(50., 56., 3.f).Drain, 1.f);

	TStringBuilder<64> Feed;
	PressureHud::AppendStunFeed(Feed, TEXT("Barracks"), 1);
	TestEqual(TEXT("Own-building feed row"), FString(Feed.ToView()), FString(TEXT("Barracks 1 stunned by a Scrambler")));
	Feed.Reset();
	PressureHud::AppendStunFeed(Feed, TEXT("Workshop"), 0);
	TestEqual(TEXT("A building without a force number prints no number"), FString(Feed.ToView()), FString(TEXT("Workshop stunned by a Scrambler")));
	return true;
}

bool FPressureHudStunWatchTest::RunTest(const FString&)
{
	PressureHud::FStunWatch Watch;
	TestFalse(TEXT("A building that was never stunned is not fresh"), Watch.Observe(7, 10., -1.).bFresh);
	const PressureHud::FStunObservation First = Watch.Observe(7, 20., 23.);
	TestTrue(TEXT("A stun seen for the first time is fresh"), First.bFresh);
	TestTrue(TEXT("and posts a feed row"), First.bFeed);
	TestEqual(TEXT("and records its length at the start"), First.Peak, 3.f);
	const PressureHud::FStunObservation Same = Watch.Observe(7, 21., 23.);
	TestFalse(TEXT("The same end time a second later is not fresh"), Same.bFresh);
	TestEqual(TEXT("and keeps the stun's recorded length"), Same.Peak, 3.f);
	TestFalse(TEXT("Replication jitter on the end time is not fresh"), Watch.Observe(7, 21.5, 23.02).bFresh);
	TestFalse(TEXT("Once it ends it is not fresh"), Watch.Observe(7, 24., 23.).bFresh);

	// A second Scrambler pulse refreshes the stun while the first is still running.
	const PressureHud::FStunObservation Refresh = Watch.Observe(7, 22.5, 25.5);
	TestTrue(TEXT("A refreshed stun is fresh"), Refresh.bFresh);
	TestFalse(TEXT("but the building already posted a row 2.5 s ago"), Refresh.bFeed);
	TestEqual(TEXT("and the drain bar restarts from the refreshed length"), Refresh.Peak, 3.f);
	TestTrue(TEXT("A stun 5 s after the last row posts again"), Watch.Observe(7, 25.5, 28.6).bFeed);
	TestTrue(TEXT("Each building has its own throttle"), Watch.Observe(8, 25.6, 28.6).bFeed);
	TestEqual(TEXT("Each building has its own length"), Watch.Peak(8), 3.f);
	TestEqual(TEXT("A building never seen has none"), Watch.Peak(99), 0.f);
	return true;
}
#endif
