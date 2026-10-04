#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/MapPresentationPolicy.h"
#include "Rules/PlacementPolicy.h"

// Pure rule tests: no world, no actors. Flash window, cable visibility, pulse timing, plate and glyph placement.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapFlashWindowTest, "CoopRTS.Rules.MapPresentation.FlashWindow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapFlashPatternTest, "CoopRTS.Rules.MapPresentation.FlashPattern",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapCableVisibilityTest, "CoopRTS.Rules.MapPresentation.CableVisibility",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapCutObserverTest, "CoopRTS.Rules.MapPresentation.CutObserver",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapLateObserverTest, "CoopRTS.Rules.MapPresentation.LateObserver",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapPulseTimingTest, "CoopRTS.Rules.MapPresentation.PulseTiming",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapPlateOrderTest, "CoopRTS.Rules.MapPresentation.PlateOrder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapGlyphTest, "CoopRTS.Rules.MapPresentation.Glyphs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapHatchTest, "CoopRTS.Rules.MapPresentation.Hatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapTextTest, "CoopRTS.Rules.MapPresentation.Text",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
using namespace MapPresentation;

constexpr float Line = 13.f;

FPlate Plate(bool bTrait, float NameWidth = 70.f)
{
	return LayoutPlate({ NameWidth, Line, bTrait ? 62.f : 0.f, bTrait ? 12.f : 0.f, bTrait });
}

FRegionLink Link(bool bControlled, bool bConnected)
{
	return { bControlled, bConnected };
}

bool Inside(const FRectF& Inner, const FRectF& Outer)
{
	return Inner.X >= Outer.X && Inner.Y >= Outer.Y && Inner.Right() <= Outer.Right() && Inner.Bottom() <= Outer.Bottom();
}
}

bool FMapFlashWindowTest::RunTest(const FString&)
{
	TestTrue(TEXT("A change at the moment it is seen plays"), FlashPlays(100.f, 100.f));
	TestTrue(TEXT("A change under 3 s old plays"), FlashPlays(102.9f, 100.f));
	TestFalse(TEXT("A change exactly 3 s old shows only the steady state"), FlashPlays(103.f, 100.f));
	TestFalse(TEXT("A change 5 s old shows only the steady state"), FlashPlays(105.f, 100.f));
	TestFalse(TEXT("A team that never changed has nothing to flash"), FlashPlays(1.f, 0.f));
	TestTrue(TEXT("A stamp slightly ahead of the client's clock plays from its start"), FlashPlays(99.9f, 100.f));
	TestEqual(TEXT("and its age is clamped to zero"), ChangeAge(99.9f, 100.f), 0.f);
	TestFalse(TEXT("A stamp far ahead has not happened"), FlashPlays(90.f, 100.f));
	TestTrue(TEXT("The spark is at full strength at the change"), FMath::IsNearlyEqual(SparkAlpha(0.f), 1.f));
	TestTrue(TEXT("and gone by half a second"), SparkAlpha(SparkSeconds) == 0.f && SparkAlpha(1.f) == 0.f);
	TestTrue(TEXT("and fades in between"), SparkAlpha(SparkSeconds * .5f) > 0.f && SparkAlpha(SparkSeconds * .5f) < 1.f);
	return true;
}

bool FMapFlashPatternTest::RunTest(const FString&)
{
	int32 Lit = 0;
	bool bPrevious = false;
	for (int32 Step = 0; Step < 1200; ++Step)
	{
		const bool bNow = FlashLit(Step / 1000.f);
		Lit += bNow && !bPrevious;
		bPrevious = bNow;
	}
	TestEqual(TEXT("The border lights three times within one second"), Lit, FlashCount);
	TestTrue(TEXT("It starts lit"), FlashLit(0.f));
	TestFalse(TEXT("Each flash goes dark before the next"), FlashLit(FlashSeconds / FlashCount * .75f));
	TestFalse(TEXT("It is dark from one second on of the flash clock"), FlashLit(FlashSeconds) || FlashLit(2.f));
	TestFalse(TEXT("A change that has not happened is not lit"), FlashLit(-1.f));
	return true;
}

bool FMapCableVisibilityTest::RunTest(const FString&)
{
	const FRegionLink Connected = Link(true, true), Cut = Link(true, false), Enemy = Link(false, false);
	TestEqual(TEXT("Two connected regions share a live cable"), ClassifyCable(Connected, Connected), ECable::Live);
	TestEqual(TEXT("Two cut-off regions share a dimmed cable beyond the cut"), ClassifyCable(Cut, Cut), ECable::Beyond);
	TestEqual(TEXT("A cut-off region and an enemy neighbour have no cable of their own: only the observer knows one broke"),
		ClassifyCable(Cut, Enemy), ECable::None);
	TestEqual(TEXT("A connected region next to an enemy region has no cable"), ClassifyCable(Connected, Enemy), ECable::None);
	TestEqual(TEXT("Two regions the team does not hold have none"), ClassifyCable(Enemy, Enemy), ECable::None);
	TestEqual(TEXT("A connected and a cut-off region cannot both exist next to each other"), ClassifyCable(Connected, Cut), ECable::None);
	TestTrue(TEXT("Cut off means held but not connected"), IsCutOff(Cut) && !IsCutOff(Connected) && !IsCutOff(Enemy));
	const uint64 Before = 0b1111, After = 0b0011, Held = 0b0110;
	TestEqual(TEXT("Only held regions that left the mask are announced"), NewlyCutOff(Before, After, Held), uint64(0b0100));
	TestEqual(TEXT("A region that stays connected is not"), NewlyCutOff(After, After, Held), uint64(0));
	TestEqual(TEXT("A region that reconnects is not"), NewlyCutOff(After, Before, Held), uint64(0));
	TestEqual(TEXT("A mask with no region means the main fell: nothing is announced as cut"), NewlyCutOff(Before, 0, Held), uint64(0));
	return true;
}

namespace
{
constexpr uint64 Bit(int32 Region)
{
	return uint64(1) << Region;
}

// A chain 0-1-2-3 with a spur 1-4; region 5 is a neutral neighbour of 2 that never held a cable.
struct FChain
{
	uint64 Neighbours[8] = { Bit(1), Bit(0) | Bit(2) | Bit(4), Bit(1) | Bit(3) | Bit(5), Bit(2), Bit(1), Bit(2), 0, 0 };
	FCutObserver Observer;

	// The team holds Held and reaches Connected as of ChangedAt, seen at Now; Opponent holds the rest named.
	void See(uint64 Connected, uint64 Held, uint64 Opponent, float ChangedAt, float Now)
	{
		FObservation In;
		In.Connected = Connected;
		In.Held = Held;
		In.CutOff = Held & ~Connected;
		In.Opponent = Opponent;
		In.Neighbours = Neighbours;
		In.ChangedAt = ChangedAt;
		In.Now = Now;
		Observe(Observer, In);
	}
};
}

bool FMapCutObserverTest::RunTest(const FString&)
{
	const uint64 Whole = Bit(0) | Bit(1) | Bit(2) | Bit(3);
	// The enemy takes region 1 at t = 100: regions 2 and 3 are cut off, region 0 is not.
	{
		FChain Chain;
		Chain.See(Whole, Whole, 0, 50.f, 100.f);
		TestTrue(TEXT("Nothing is cut or flashing while the chain is whole"), Chain.Observer.FlashAge(2, 100.f) < 0.f);
		Chain.See(Bit(0), Bit(0) | Bit(2) | Bit(3), Bit(1), 100.f, 100.f);
		TestTrue(TEXT("Regions 2 and 3 start flashing the moment their cut is first seen"),
			Chain.Observer.FlashAge(2, 100.f) == 0.f && Chain.Observer.FlashAge(3, 100.f) == 0.f);
		TestTrue(TEXT("Region 0 was not cut"), Chain.Observer.FlashAge(0, 100.f) < 0.f);
		TestTrue(TEXT("The flash clock runs from the local sight, not the server stamp"),
			FMath::IsNearlyEqual(Chain.Observer.FlashAge(2, 100.4f), .4f, .001f));
		TestTrue(TEXT("Only the cable to the lost region 1 snapped, from the cut region next to it"),
			Chain.Observer.IsSnapped(2, 1) && !Chain.Observer.IsSnapped(3, 2) && !Chain.Observer.IsSnapped(2, 5));
		TestFalse(TEXT("A neutral neighbour that never held a cable leaves no stub"), Chain.Observer.IsSnapped(2, 5));
		// The team takes an unrelated region 4: the mask changes again, but 2 and 3 are not newly cut.
		Chain.See(Bit(0), Bit(0) | Bit(2) | Bit(3) | Bit(4), Bit(1), 101.f, 101.f);
		TestEqual(TEXT("An unrelated later change does not restart a flash"), Chain.Observer.FlashAge(2, 101.f), 1.f);
		TestTrue(TEXT("nor lose the stub"), Chain.Observer.IsSnapped(2, 1));
		Chain.See(Bit(0), Bit(0) | Bit(2) | Bit(3) | Bit(4), Bit(1), 101.f, 105.f);
		TestTrue(TEXT("Once the clock runs out the region is steady"), Chain.Observer.FlashAge(2, 105.f) >= FlashSeconds);
		// Reconnection clears the cut and the stub.
		Chain.See(Whole | Bit(4), Whole | Bit(4), 0, 106.f, 106.f);
		TestTrue(TEXT("A reconnected region stops flashing"), Chain.Observer.FlashAge(2, 106.f) < 0.f);
		TestFalse(TEXT("and its stub is gone"), Chain.Observer.IsSnapped(2, 1));
	}
	// A second cut while the first is still flashing flashes only the new region.
	{
		FChain Chain;
		Chain.See(Whole, Whole, 0, 50.f, 100.f);
		Chain.See(Bit(0) | Bit(1) | Bit(2), Bit(0) | Bit(1) | Bit(2), Bit(3), 100.f, 100.f);
		Chain.See(Bit(0) | Bit(1), Bit(0) | Bit(1) | Bit(2), Bit(3), 100.5f, 100.5f);
		TestTrue(TEXT("The newly cut region flashes from its own start"), Chain.Observer.FlashAge(2, 100.5f) == 0.f);
	}
	return true;
}

bool FMapLateObserverTest::RunTest(const FString&)
{
	const uint64 Whole = Bit(0) | Bit(1) | Bit(2) | Bit(3);
	// A client joining later: a recent cut flashes from first sight, an old one is steady.
	{
		FChain Recent;
		Recent.See(Bit(0), Bit(0) | Bit(2) | Bit(3), Bit(1), 100.f, 102.f);
		TestTrue(TEXT("A cut first seen 2 s late still plays, for every cut region"),
			Recent.Observer.FlashAge(2, 102.f) == 0.f && Recent.Observer.FlashAge(3, 102.f) == 0.f);
		TestTrue(TEXT("and its stub is guessed from the opponent's neighbour, never the neutral one"),
			Recent.Observer.IsSnapped(2, 1) && !Recent.Observer.IsSnapped(2, 5));
		FChain Old;
		Old.See(Bit(0), Bit(0) | Bit(2) | Bit(3), Bit(1), 100.f, 103.5f);
		TestTrue(TEXT("A cut first seen 3.5 s late shows only the steady state"),
			Old.Observer.FlashAge(2, 103.5f) < 0.f && Old.Observer.IsSnapped(2, 1));
		FChain Slow;
		Slow.See(Whole, Whole, 0, 50.f, 100.f);
		Slow.See(Bit(0), Bit(0) | Bit(2) | Bit(3), Bit(1), 100.f, 103.5f);
		TestTrue(TEXT("A change that reaches the client's frame after the window is steady too"), Slow.Observer.FlashAge(2, 103.5f) < 0.f);
	}
	// The main falling empties the mask: that is the end of the battle, not a cut.
	{
		FChain Chain;
		Chain.See(Whole, Whole, 0, 50.f, 100.f);
		Chain.See(0, Whole, 0, 100.f, 100.f);
		TestTrue(TEXT("No region flashes when the mask empties"), Chain.Observer.FlashAge(1, 100.f) < 0.f && Chain.Observer.FlashAge(3, 100.f) < 0.f);
	}
	return true;
}

bool FMapPulseTimingTest::RunTest(const FString&)
{
	constexpr float Radius = 400.f;
	TestTrue(TEXT("A unit that never pulsed has no age"), PulseAge(10.f, -1.f) < 0.f && PulseAge(10.f, 0.f) < 0.f);
	TestTrue(TEXT("A pulse at this instant has age zero"), PulseAge(10.f, 10.f) == 0.f);
	TestTrue(TEXT("A pulse stamped slightly ahead of the client clock starts at zero"), PulseAge(9.9f, 10.f) == 0.f);
	TestTrue(TEXT("A pulse stamped far ahead has not happened"), PulseAge(5.f, 10.f) < 0.f);
	TestTrue(TEXT("A pulse that has fully played out has no age"), PulseAge(11.f, 10.f) < 0.f);
	TestTrue(TEXT("A client joining later sees no ring for an old pulse"), !PulseRing(PulseAge(60.f, 10.f), Radius).bActive);
	const FPulseRing Start = PulseRing(0.f, Radius), Middle = PulseRing(PulseRingSeconds * .5f, Radius);
	const FPulseRing Full = PulseRing(PulseRingSeconds, Radius), Fade = PulseRing(PulseRingSeconds + PulseFadeSeconds * .5f, Radius);
	TestTrue(TEXT("The ring starts at the unit"), Start.bActive && Start.Radius == 0.f);
	TestTrue(TEXT("grows linearly"), FMath::IsNearlyEqual(Middle.Radius, Radius * .5f));
	TestTrue(TEXT("reaches the pulse radius after 0.4 s"), FMath::IsNearlyEqual(Full.Radius, Radius) && Full.Alpha == 1.f);
	TestTrue(TEXT("then holds its radius and fades"), FMath::IsNearlyEqual(Fade.Radius, Radius) && Fade.Alpha > 0.f && Fade.Alpha < 1.f);
	TestFalse(TEXT("and is gone after the fade"), PulseRing(PulseRingSeconds + PulseFadeSeconds, Radius).bActive);
	TestFalse(TEXT("A unit without a pulse radius draws no ring"), PulseRing(.1f, 0.f).bActive);
	TestTrue(TEXT("A spark on a target at the Scrambler is at once"), PulseSparkAlpha(0.f, 0.f, Radius) > .99f);
	TestEqual(TEXT("A spark on a target at the rim waits for the ring"), PulseSparkAlpha(PulseRingSeconds * .5f, Radius, Radius), 0.f);
	TestTrue(TEXT("and lights when the ring arrives"), PulseSparkAlpha(PulseRingSeconds, Radius, Radius) > .99f);
	TestTrue(TEXT("A spark on a target at half the radius lights at half the time"),
		PulseSparkAlpha(PulseRingSeconds * .5f, Radius * .5f, Radius) > .99f
			&& PulseSparkAlpha(PulseRingSeconds * .25f, Radius * .5f, Radius) == 0.f);
	TestEqual(TEXT("A spark ends after its duration"), PulseSparkAlpha(PulseSparkSeconds, 0.f, Radius), 0.f);
	TestEqual(TEXT("A target outside the radius never sparks"), PulseSparkAlpha(PulseRingSeconds, Radius + 1.f, Radius), 0.f);
	return true;
}

bool FMapPlateOrderTest::RunTest(const FString&)
{
	const FPlate Bare = Plate(false), Trait = Plate(true);
	TestEqual(TEXT("The plate's top edge is the same with or without a trait, so the JEV badge above never moves"),
		Bare.Plate.Y, Trait.Plate.Y);
	TestEqual(TEXT("and equals the stack's fixed offset"), Trait.Plate.Y, -PlateTopAboveAnchor(Line));
	TestTrue(TEXT("The trait grows the plate downward"), Trait.Plate.Bottom() > Bare.Plate.Bottom());
	TestTrue(TEXT("Glyph, name, word and bar sit inside the plate"),
		Inside(Trait.Glyph, Trait.Plate) && Inside(Trait.Name, Trait.Plate) && Inside(Trait.Word, Trait.Plate)
			&& Inside(Trait.Bar, Trait.Plate));
	TestTrue(TEXT("The glyph is left of the name and the word is under it"),
		Trait.Glyph.Right() <= Trait.Name.X && Trait.Word.Y >= Trait.Name.Bottom() && Trait.Word.X == Trait.Name.X);
	TestFalse(TEXT("Glyph, name, word and bar never overlap"),
		Trait.Glyph.Intersects(Trait.Name) || Trait.Glyph.Intersects(Trait.Word) || Trait.Name.Intersects(Trait.Word)
			|| Trait.Bar.Intersects(Trait.Name) || Trait.Bar.Intersects(Trait.Word) || Trait.Bar.Intersects(Trait.Glyph));
	TestEqual(TEXT("The glyph is 20 px"), Trait.Glyph.W, 20.f);
	TestTrue(TEXT("A long name widens the plate rather than overlapping the glyph"),
		Plate(true, 200.f).Plate.W >= 200.f + Trait.Glyph.W && !Plate(true, 200.f).Glyph.Intersects(Plate(true, 200.f).Name));
	TestTrue(TEXT("The chips start below the plate"), Trait.ChipTop > Trait.Plate.Bottom() && Bare.ChipTop > Bare.Plate.Bottom());
	const float First = Trait.ChipTop;
	TestEqual(TEXT("A lone Fortify chip takes the first slot"), ChipTop(First, false, true, EChip::Fortified), First);
	TestEqual(TEXT("A lone CUT OFF chip takes the first slot"), ChipTop(First, true, false, EChip::CutOff), First);
	TestTrue(TEXT("With both, CUT OFF leads and Fortify sits below it without overlap"),
		ChipTop(First, true, true, EChip::CutOff) == First
			&& ChipTop(First, true, true, EChip::Fortified) >= First + ChipHeight(EChip::CutOff));
	TestTrue(TEXT("An absent chip has no slot"), ChipTop(First, false, true, EChip::CutOff) < 0.f && ChipTop(First, true, false, EChip::Fortified) < 0.f);
	const FVector2D Glyph = MinimapGlyphOrigin();
	const float Right = Glyph.X + MinimapGlyphSize, Bottom = Glyph.Y + MinimapGlyphSize;
	TestTrue(TEXT("The minimap glyph sits top-left of the node: right and above it"), Right <= 0.f && Bottom <= 0.f);
	TestTrue(TEXT("and clear of the Fortify ring that wraps the node"),
		FVector2D(Right, Bottom).Size() > MinimapNodeClearRadius);
	return true;
}

bool FMapGlyphTest::RunTest(const FString&)
{
	const ERegionTrait Traits[] = { ERegionTrait::HighGround, ERegionTrait::Cover, ERegionTrait::Open, ERegionTrait::Hazard };
	for (const ERegionTrait Trait : Traits)
	{
		const TConstArrayView<FGlyphSegment> Glyph = TraitGlyph(Trait);
		TestTrue(*FString::Printf(TEXT("Trait %d has a glyph of several strokes"), static_cast<int32>(Trait)), Glyph.Num() >= 4);
		bool bInside = true;
		for (const FGlyphSegment& Segment : Glyph)
			for (const FVector2D& Point : { Segment.A, Segment.B })
				bInside &= Point.X >= 0. && Point.X <= 1. && Point.Y >= 0. && Point.Y <= 1.;
		TestTrue(*FString::Printf(TEXT("Trait %d stays in the unit square"), static_cast<int32>(Trait)), bInside);
		TestTrue(*FString::Printf(TEXT("Trait %d carries a word"), static_cast<int32>(Trait)), FCString::Strlen(TraitWord(Trait)) > 0);
	}
	for (int32 First = 0; First < 4; ++First)
		for (int32 Second = First + 1; Second < 4; ++Second)
		{
			const TConstArrayView<FGlyphSegment> A = TraitGlyph(Traits[First]), B = TraitGlyph(Traits[Second]);
			bool bSame = A.Num() == B.Num();
			for (int32 Index = 0; bSame && Index < A.Num(); ++Index)
				bSame = A[Index].A == B[Index].A && A[Index].B == B[Index].B;
			TestFalse(*FString::Printf(TEXT("Glyphs %d and %d differ in silhouette, so colour is secondary"), First, Second), bSame);
			TestTrue(TEXT("and their words differ"), FCString::Strcmp(TraitWord(Traits[First]), TraitWord(Traits[Second])) != 0);
		}
	TestEqual(TEXT("A region without a trait has no glyph"), TraitGlyph(ERegionTrait::None).Num(), 0);
	TestEqual(TEXT("and no word"), FString(TraitWord(ERegionTrait::None)), FString());
	TestEqual(TEXT("High ground reads HIGH GROUND"), FString(TraitWord(ERegionTrait::HighGround)), FString(TEXT("HIGH GROUND")));
	TestTrue(TEXT("The chain-break glyph has strokes"), ChainBreakGlyph().Num() >= 4);
	return true;
}

bool FMapHatchTest::RunTest(const FString&)
{
	const TArray<FVector2D> Square = { { 0., 0. }, { 1000., 0. }, { 1000., 1000. }, { 0., 1000. } };
	int32 Count = 0;
	bool bOnLines = true, bInside = true;
	ForEachHatch(Square, 100.f, [&](const FVector2D& A, const FVector2D& B) {
		++Count;
		bOnLines &= FMath::IsNearlyEqual(A.X + A.Y, B.X + B.Y, .01) && FMath::IsNearlyZero(FMath::Fmod(A.X + A.Y, 100.), .01);
		bInside &= PlacementPolicy::ContainsPoint(Square, (A + B) * .5);
	});
	TestEqual(TEXT("A 1000 square hatches with a line at every multiple of the spacing strictly inside its x + y range"), Count, 19);
	TestTrue(TEXT("Every hatch line is diagonal and on a multiple of the spacing"), bOnLines);
	TestTrue(TEXT("Every segment lies inside the polygon"), bInside);
	// A U with its notch open to the top: lines through the notch split in two segments, never one across it.
	const TArray<FVector2D> U = { { 0., 0. }, { 900., 0. }, { 900., 900. }, { 600., 900. }, { 600., 300. }, { 300., 300. },
		{ 300., 900. }, { 0., 900. } };
	bool bAllInside = true;
	int32 Split = 0;
	ForEachHatch(U, 100.f, [&](const FVector2D& A, const FVector2D& B) {
		bAllInside &= PlacementPolicy::ContainsPoint(U, (A + B) * .5);
		Split += FMath::IsNearlyEqual(A.X + A.Y, 1000., .01);
	});
	TestTrue(TEXT("A concave polygon's hatch never crosses its notch"), bAllInside);
	TestEqual(TEXT("A line through both arms is cut into two segments"), Split, 2);
	int32 Degenerate = 0;
	ForEachHatch(TArray<FVector2D>{ { 0., 0. }, { 10., 10. } }, 5.f, [&](const FVector2D&, const FVector2D&) { ++Degenerate; });
	ForEachHatch(Square, 0.f, [&](const FVector2D&, const FVector2D&) { ++Degenerate; });
	ForEachHatch(Square, .001f, [&](const FVector2D&, const FVector2D&) { ++Degenerate; });
	TestEqual(TEXT("Fewer than three vertices, no spacing, or a runaway line count draw nothing"), Degenerate, 0);
	return true;
}

bool FMapTextTest::RunTest(const FString&)
{
	TStringBuilder<128> Row;
	AppendCutFeedText(Row, TEXT("Fusion Works"), 1);
	TestEqual(TEXT("One Drill Rig"), FString(Row.ToView()), FString(TEXT("Supply cut: Fusion Works cut off \u00B7 1 Drill Rig offline")));
	Row.Reset();
	AppendCutFeedText(Row, TEXT("Fusion Works"), 3);
	TestEqual(TEXT("Several Drill Rigs pluralise"), FString(Row.ToView()), FString(TEXT("Supply cut: Fusion Works cut off \u00B7 3 Drill Rigs offline")));
	Row.Reset();
	AppendCutFeedText(Row, TEXT("Skyhook"), 0);
	TestEqual(TEXT("A region without a rig names only itself"), FString(Row.ToView()), FString(TEXT("Supply cut: Skyhook cut off")));
	Row.Reset();
	AppendOfflineDepositLabel(Row, false, 1200);
	TestEqual(TEXT("The offline deposit label replaces the rate"), FString(Row.ToView()), FString(TEXT("POWER 1200 \u00B7 OFFLINE")));
	Row.Reset();
	AppendOfflineDepositLabel(Row, true, 1500);
	TestEqual(TEXT("A rich deposit says so"), FString(Row.ToView()), FString(TEXT("RICH 1500 \u00B7 OFFLINE")));
	return true;
}
#endif
