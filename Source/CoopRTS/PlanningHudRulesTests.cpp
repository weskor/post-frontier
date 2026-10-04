#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/PlanningHudPolicy.h"

// Pure rule tests: no world, no actors. The planning HUD's texts and what Enter does (ui.md surface 10).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningHudClockTest, "CoopRTS.Rules.PlanningHud.Clock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningHudReadyTest, "CoopRTS.Rules.PlanningHud.Ready",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningHudChipsTest, "CoopRTS.Rules.PlanningHud.Chips",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningHudEnterTest, "CoopRTS.Rules.PlanningHud.Enter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningHudKitTest, "CoopRTS.Rules.PlanningHud.Kit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningHudSpotsTest, "CoopRTS.Rules.PlanningHud.Spots",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
using namespace PlanningHud;

FString Clock(double Remaining)
{
	TStringBuilder<48> Text;
	AppendClock(Text, Remaining);
	return FString(Text.ToView());
}

FString Ready(bool bReady, int32 Count, int32 Humans)
{
	TStringBuilder<64> Text;
	AppendReady(Text, bReady, Count, Humans);
	return FString(Text.ToView());
}

FString Chip(int32 Slot, EChip State)
{
	TStringBuilder<32> Text;
	AppendChip(Text, Slot, State);
	return FString(Text.ToView());
}

FString Confirm(bool bBarracks, bool bRig)
{
	TStringBuilder<128> Text;
	AppendConfirm(Text, bBarracks, bRig, TEXT("Barracks"), TEXT("Drill Rig"));
	return FString(Text.ToView());
}

// A commander in the phase with a kit, not Ready.
FEnterInput Editing()
{
	FEnterInput In;
	In.bPlanning = true;
	In.bHasKit = true;
	return In;
}
}

bool FPlanningHudClockTest::RunTest(const FString&)
{
	TestEqual(TEXT("The clock reads PLANNING and minutes:seconds"), Clock(47.), FString(TEXT("PLANNING 0:47")));
	TestEqual(TEXT("A started second still counts as a whole one"), Clock(46.2), FString(TEXT("PLANNING 0:47")));
	TestEqual(TEXT("The full phase reads 1:00"), Clock(60.), FString(TEXT("PLANNING 1:00")));
	TestEqual(TEXT("It reads 0:00 only once nothing is left"), Clock(0.), FString(TEXT("PLANNING 0:00")));
	TestEqual(TEXT("A negative remainder never goes below it"), Clock(-3.), FString(TEXT("PLANNING 0:00")));
	TStringBuilder<64> Left;
	AppendTimeLeft(Left, 47.);
	TestEqual(TEXT("The panel header says what is left and that nothing runs"), FString(Left.ToView()), FString(TEXT("0:47 left \u00B7 nothing runs yet")));
	TestFalse(TEXT("The clock rests until the last ten seconds"), IsPulsing(10.01));
	TestTrue(TEXT("and pulses in them"), IsPulsing(10.) && IsPulsing(.5));
	TestFalse(TEXT("but not once the phase is over"), IsPulsing(0.));
	TestEqual(TEXT("A resting clock is fully opaque"), PulseOpacity(30., .25), 1.f);
	float Low = 2.f, High = -1.f;
	for (int32 Step = 0; Step < 100; ++Step)
	{
		const float Opacity = PulseOpacity(5., Step / 100.);
		Low = FMath::Min(Low, Opacity);
		High = FMath::Max(High, Opacity);
	}
	TestTrue(TEXT("A pulsing clock waves between .55 and 1 once a second"), FMath::IsNearlyEqual(Low, .55f, .01f) && FMath::IsNearlyEqual(High, 1.f, .01f));
	TestEqual(TEXT("and repeats every second"), PulseOpacity(5., 3.3), PulseOpacity(5., 7.3), .001f);
	return true;
}

bool FPlanningHudReadyTest::RunTest(const FString&)
{
	TestEqual(TEXT("The Pause slot offers READY with the humans Ready over the humans in the phase"), Ready(false, 1, 2),
		FString(TEXT("[Enter] READY (1/2)")));
	TestEqual(TEXT("an empty count starts at zero"), Ready(false, 0, 2), FString(TEXT("[Enter] READY (0/2)")));
	TestEqual(TEXT("Once Ready the slot says so and what Enter does now"), Ready(true, 2, 2),
		FString(TEXT("\u2713 READY (2/2)  [Enter] Undo")));
	return true;
}

bool FPlanningHudChipsTest::RunTest(const FString&)
{
	TestTrue(TEXT("A commander with a piece still unplaced is placing"), ChipState(false, false) == EChip::Placing);
	TestTrue(TEXT("with every piece placed, placed"), ChipState(false, true) == EChip::Placed);
	TestTrue(TEXT("Ready wins over the kit"), ChipState(true, false) == EChip::Ready && ChipState(true, true) == EChip::Ready);
	TestEqual(TEXT("Chips name the commander from one"), Chip(0, EChip::Placing), FString(TEXT("C1 PLACING")));
	TestEqual(TEXT("and the state in words"), Chip(2, EChip::Placed), FString(TEXT("C3 PLACED")));
	TestEqual(TEXT("with a check mark on Ready"), Chip(1, EChip::Ready), FString(TEXT("C2 \u2713 READY")));
	return true;
}

bool FPlanningHudEnterTest::RunTest(const FString&)
{
	TestTrue(TEXT("Enter outside planning does nothing"), Enter(FEnterInput()) == EEnter::Ignore);
	FEnterInput NoKit = Editing();
	NoKit.bHasKit = false;
	TestTrue(TEXT("and so does Enter without a kit"), Enter(NoKit) == EEnter::Ignore);
	TestTrue(TEXT("A finished kit readies at once"), Enter(Editing()) == EEnter::Ready);

	FEnterInput Unplaced = Editing();
	Unplaced.bDefaultsNeeded = true;
	TestTrue(TEXT("An unplaced piece asks first and sends nothing"), Enter(Unplaced) == EEnter::Confirm);
	Unplaced.bConfirmLive = true;
	TestTrue(TEXT("and a second Enter while the question is live readies"), Enter(Unplaced) == EEnter::Ready);

	FEnterInput Done = Unplaced;
	Done.bReady = true;
	TestTrue(TEXT("Enter on a Ready commander un-readies, defaults or not"), Enter(Done) == EEnter::Unready);
	Done.bDefaultsNeeded = false;
	Done.bConfirmLive = false;
	TestTrue(TEXT("whatever the question's state"), Enter(Done) == EEnter::Unready);

	TestTrue(TEXT("The question lives for four seconds"), IsConfirmLive(10., 6.5) && IsConfirmLive(10., 6.));
	TestFalse(TEXT("and then lapses, so a late Enter asks again"), IsConfirmLive(10.1, 6.));
	TestFalse(TEXT("A question never asked is not live"), IsConfirmLive(10., -1000.));
	TestFalse(TEXT("and one asked in the future is not either"), IsConfirmLive(5., 6.));

	TestEqual(TEXT("The question names the Barracks and the default spot"), Confirm(true, false),
		FString(TEXT("Barracks not placed: default spot. Press Enter again")));
	TestEqual(TEXT("or the Drill Rig"), Confirm(false, true), FString(TEXT("Drill Rig not placed: default spot. Press Enter again")));
	TestEqual(TEXT("or both"), Confirm(true, true),
		FString(TEXT("Barracks and Drill Rig not placed: default spots. Press Enter again")));
	TStringBuilder<128> Renamed;
	AppendConfirm(Renamed, false, true, TEXT("Barracks"), TEXT("Extractor"));
	TestEqual(TEXT("The question says whatever the catalogue calls the piece"), FString(Renamed.ToView()),
		FString(TEXT("Extractor not placed: default spot. Press Enter again")));
	return true;
}

bool FPlanningHudKitTest::RunTest(const FString&)
{
	const auto Text = [](EKitCard State) {
		TStringBuilder<64> Line;
		AppendKitCard(Line, State);
		return FString(Line.ToView());
	};
	TestTrue(TEXT("A piece not placed with a deposit to take is free to place"), KitCard(false, false, true) == EKitCard::Free);
	TestTrue(TEXT("a placed piece can be moved"), KitCard(true, false, true) == EKitCard::Placed);
	TestTrue(TEXT("Ready locks it, placed or not"), KitCard(true, true, true) == EKitCard::Locked && KitCard(false, true, true) == EKitCard::Locked);
	TestTrue(TEXT("A Rig with no free deposit is paid back instead"), KitCard(false, false, false) == EKitCard::NoDeposit);
	TestTrue(TEXT("but a Rig that stands is placed whatever the deposits are"), KitCard(true, false, false) == EKitCard::Placed);
	TestEqual(TEXT("Free cards say so"), Text(EKitCard::Free), FString(TEXT("FREE KIT \u00B7 place")));
	TestEqual(TEXT("placed ones can be moved"), Text(EKitCard::Placed), FString(TEXT("PLACED \u00B7 click to move")));
	TestEqual(TEXT("a Rig with no deposit says what happens at 0:00"), Text(EKitCard::NoDeposit), FString(TEXT("no free deposit \u00B7 Power back at 0:00")));
	TestEqual(TEXT("and a locked kit says why it does not respond"), Text(EKitCard::Locked), FString(TEXT("READY \u00B7 locked")));
	return true;
}

bool FPlanningHudSpotsTest::RunTest(const FString&)
{
	const FVector Home(1000., -500., 20.);
	TestEqual(TEXT("There are nine rings of 32 directions"), SpotCount, 288);
	const FVector First = DefaultBarracksSpot(Home, 0, 0);
	TestTrue(TEXT("The first spot is 380 uu from home along +X, at the building height"),
		First.Equals(FVector(1380., -500., 5.), .01));
	TestTrue(TEXT("a quarter turn later it is along +Y"), DefaultBarracksSpot(Home, 0, 8).Equals(FVector(1000., -120., 5.), .01));
	TestTrue(TEXT("The next ring is 160 uu further out"), DefaultBarracksSpot(Home, 0, 32).Equals(FVector(1540., -500., 5.), .01));
	TestTrue(TEXT("and the last ring is 1660 uu out"), FMath::IsNearlyEqual(FVector::Dist2D(DefaultBarracksSpot(Home, 0, SpotCount - 32), Home), 380. + 8 * 160., .01));
	TestTrue(TEXT("The far side mirrors the rings through its headquarters"),
		DefaultBarracksSpot(Home, 5, 0).Equals(FVector(620., -500., 5.), .01));
	return true;
}
#endif
