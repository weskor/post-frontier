#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/HoldPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldClockTest, "CoopRTS.Rules.Hold.CommitAndQuiet",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldSelectionTest, "CoopRTS.Rules.Hold.ResponderSelection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldAlarmLeashTest, "CoopRTS.Rules.Hold.AlarmAndLeash",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldPolygonTest, "CoopRTS.Rules.Hold.PolygonAndClamp",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldSegmentTest, "CoopRTS.Rules.Hold.ConcavePaths",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldPostsTest, "CoopRTS.Rules.Hold.PostsAndOverflow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldThreatTest, "CoopRTS.Rules.Hold.StickyThreat",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldClippedPostsTest, "CoopRTS.Rules.Hold.ClippedPostOverflow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
const FVector2D Square[] = { { 0., 0. }, { 120., 0. }, { 120., 120. }, { 0., 120. } };
// The vertex centroid is in the open notch, not in the polygon.
const FVector2D Concave[] = {
	{ 0., 0. }, { 120., 0. }, { 120., 120. }, { 80., 120. },
	{ 80., 40. }, { 40., 40. }, { 40., 120. }, { 0., 120. }
};
}

bool FHoldClockTest::RunTest(const FString& Parameters)
{
	HoldPolicy::FClock Clock;
	TestFalse(TEXT("Selection without an alarm stays at the post"), HoldPolicy::UpdateClock(Clock, 90., false, true));
	TestFalse(TEXT("Unselected holder does not respond to an alarm"), HoldPolicy::UpdateClock(Clock, 99., true, false));
	TestTrue(TEXT("Selected holder begins responding"), HoldPolicy::UpdateClock(Clock, 100., true, true));
	TestEqual(TEXT("Commitment starts at selection time"), Clock.Started, 100.);
	TestTrue(TEXT("Deselection during an alarm does not release a responder"), HoldPolicy::UpdateClock(Clock, 100.5, true, false));
	TestTrue(TEXT("Quiet begins while committed"), HoldPolicy::UpdateClock(Clock, 101., false, false));
	TestEqual(TEXT("First quiet observation starts its timer"), Clock.QuietSince, 101.);
	TestTrue(TEXT("Six quiet seconds do not shorten the eight-second commitment"), HoldPolicy::UpdateClock(Clock, 107., false, false));
	TestTrue(TEXT("Just short of commitment stays responding"), HoldPolicy::UpdateClock(Clock, 107.999, false, true));
	TestFalse(TEXT("Both deadlines met returns exactly at eight seconds"), HoldPolicy::UpdateClock(Clock, 108., false, true));
	TestFalse(TEXT("Selection cannot restart a responder in a quiet region"), HoldPolicy::UpdateClock(Clock, 110., false, true));

	TestTrue(TEXT("A new alarm permits a fresh commitment"), HoldPolicy::UpdateClock(Clock, 200., true, true));
	TestTrue(TEXT("Repeated selection retains responder"), HoldPolicy::UpdateClock(Clock, 207., true, true));
	TestEqual(TEXT("Repeated selection does not restart commitment"), Clock.Started, 200.);
	TestTrue(TEXT("Alarm beyond commitment keeps responder"), HoldPolicy::UpdateClock(Clock, 220., true, false));
	TestTrue(TEXT("Late quiet starts a full quiet period"), HoldPolicy::UpdateClock(Clock, 221., false, false));
	TestTrue(TEXT("Just short of quiet deadline stays responding"), HoldPolicy::UpdateClock(Clock, 226.999, false, false));
	TestFalse(TEXT("Returns exactly six seconds after late quiet"), HoldPolicy::UpdateClock(Clock, 227., false, false));

	HoldPolicy::UpdateClock(Clock, 300., true, true);
	HoldPolicy::UpdateClock(Clock, 309., false, false);
	TestTrue(TEXT("Renewed alarm interrupts the quiet countdown"), HoldPolicy::UpdateClock(Clock, 314., true, false));
	TestEqual(TEXT("Renewed alarm does not restart commitment"), Clock.Started, 300.);
	TestEqual(TEXT("Active alarm clears quiet state"), Clock.QuietSince, -1.);
	HoldPolicy::UpdateClock(Clock, 320., false, false);
	TestTrue(TEXT("Previous quiet deadline cannot release renewed responder"), HoldPolicy::UpdateClock(Clock, 325.999, false, false));
	TestFalse(TEXT("Renewed quiet receives all six seconds"), HoldPolicy::UpdateClock(Clock, 326., false, false));
	return true;
}

bool FHoldSelectionTest::RunTest(const FString& Parameters)
{
	HoldPolicy::FCandidate Exact[] = { { 9., 100 }, { 1., 25 }, { 16., 100 } };
	HoldPolicy::SelectResponders(Exact, 100);
	TestTrue(TEXT("Nearest small force is selected first"), Exact[1].bSelected);
	TestTrue(TEXT("Next nearest reaches the exact 1.25 ratio"), Exact[0].bSelected);
	TestFalse(TEXT("Exact ratio leaves the remaining force at its post"), Exact[2].bSelected);

	HoldPolicy::FCandidate Below[] = { { 1., 124 }, { 4., 1 }, { 9., 100 } };
	HoldPolicy::SelectResponders(Below, 100);
	TestTrue(TEXT("One Power short adds another holder"), Below[1].bSelected);
	TestFalse(TEXT("Reaching the threshold stops selection"), Below[2].bSelected);

	HoldPolicy::FCandidate Fractional[] = { { 1., 126 }, { 4., 1 }, { 9., 100 } };
	HoldPolicy::SelectResponders(Fractional, 101);
	TestTrue(TEXT("Fractional required Power must not round down below the response ratio"), Fractional[1].bSelected);
	TestFalse(TEXT("First whole-Power total exceeding a fractional threshold is sufficient"), Fractional[2].bSelected);

	HoldPolicy::FCandidate Ties[] = { { 1., 0 }, { 4., 125 }, { 4., 125 }, { 0., -1 } };
	HoldPolicy::SelectResponders(Ties, 100);
	TestFalse(TEXT("Zero-Power holder cannot cover an alarm"), Ties[0].bSelected);
	TestTrue(TEXT("Equal-distance positive holders choose the first"), Ties[1].bSelected);
	TestFalse(TEXT("Tie does not select an unnecessary holder"), Ties[2].bSelected);
	TestFalse(TEXT("Negative Power does not enter the response"), Ties[3].bSelected);

	HoldPolicy::FCandidate Growth[] = { { 100., 125, true }, { 4., 125 }, { 1., 125 } };
	HoldPolicy::SelectResponders(Growth, 100);
	TestTrue(TEXT("Existing distant responder is preserved"), Growth[0].bSelected);
	TestFalse(TEXT("Sufficient existing Power keeps nearer holders idle"), Growth[2].bSelected);
	HoldPolicy::SelectResponders(Growth, 200);
	TestTrue(TEXT("Threat growth preserves the original response"), Growth[0].bSelected);
	TestTrue(TEXT("Threat growth adds the nearest remaining holder"), Growth[2].bSelected);
	TestFalse(TEXT("Growth stops once combined Power is enough"), Growth[1].bSelected);
	Growth[2].bResponding = true;
	HoldPolicy::SelectResponders(Growth, 1);
	TestTrue(TEXT("Threat shrink cannot deselect an already responding reinforcement"), Growth[2].bSelected);
	HoldPolicy::SelectResponders(Growth, 1000);
	for (const HoldPolicy::FCandidate& Candidate : Growth)
		TestTrue(TEXT("Overwhelming threat selects all available positive Power"), Candidate.bSelected);
	HoldPolicy::SelectResponders(Growth, 0);
	TestTrue(TEXT("No threat still preserves committed responders"), Growth[0].bSelected && Growth[2].bSelected);
	TestFalse(TEXT("Selection from the previous tick does not preserve an idle holder"), Growth[1].bSelected);

	HoldPolicy::FCandidate Large[] = {
		{ 10., TNumericLimits<int32>::Max(), true },
		{ 20., TNumericLimits<int32>::Max(), true }, { 0., 1 }
	};
	HoldPolicy::SelectResponders(Large, TNumericLimits<int32>::Max());
	TestFalse(TEXT("Summed Power does not overflow and recruit an unnecessary holder"), Large[2].bSelected);
	HoldPolicy::SelectResponders(TArrayView<HoldPolicy::FCandidate>(), 100);
	return true;
}

bool FHoldAlarmLeashTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Living hostile intrusion raises an alarm before dealing damage"), HoldPolicy::IsAlarmSource(true, true, true, false));
	TestTrue(TEXT("Living hostile outside raises an alarm while damaging inside"), HoldPolicy::IsAlarmSource(true, true, false, true));
	TestTrue(TEXT("An intruder damaging inside remains an alarm source"), HoldPolicy::IsAlarmSource(true, true, true, true));
	TestFalse(TEXT("Outside hostile not damaging inside is not an alarm source"), HoldPolicy::IsAlarmSource(true, true, false, false));
	TestFalse(TEXT("Dead intruder is not an alarm source"), HoldPolicy::IsAlarmSource(false, true, true, false));
	TestFalse(TEXT("Dead outside attacker is not an alarm source"), HoldPolicy::IsAlarmSource(false, true, false, true));
	TestFalse(TEXT("Friendly unit inside does not alarm holders"), HoldPolicy::IsAlarmSource(true, false, true, false));
	TestFalse(TEXT("Friendly damage does not create a hostile alarm source"), HoldPolicy::IsAlarmSource(true, false, false, true));
	TestTrue(TEXT("Live in-range damage remains current just before expiry"), HoldPolicy::IsDamageCurrent(9.999, 10., true, true));
	TestFalse(TEXT("Damage stops being current exactly at expiry"), HoldPolicy::IsDamageCurrent(10., 10., true, true));
	TestFalse(TEXT("Expired damage cannot extend an alarm"), HoldPolicy::IsDamageCurrent(10.001, 10., true, true));
	TestFalse(TEXT("Victim death ends otherwise current damage"), HoldPolicy::IsDamageCurrent(9., 10., false, true));
	TestFalse(TEXT("Moving out of weapon range ends otherwise current damage"), HoldPolicy::IsDamageCurrent(9., 10., true, false));
	TestTrue(TEXT("Inside threat does not need to be firing or within edge weapon range"), HoldPolicy::WithinLeash(true, false, 1000., 10.));
	TestFalse(TEXT("Outside intrusion alone cannot be pursued"), HoldPolicy::WithinLeash(false, false, 0., 100.));
	TestTrue(TEXT("Outside damaging attacker is permitted just within range"), HoldPolicy::WithinLeash(false, true, 99.999, 100.));
	TestTrue(TEXT("Weapon-range edge is inclusive"), HoldPolicy::WithinLeash(false, true, 100., 100.));
	TestFalse(TEXT("Outside damaging attacker beyond range exits the leash"), HoldPolicy::WithinLeash(false, true, 100.001, 100.));
	TestFalse(TEXT("Ceasing damage ends outside retaliation"), HoldPolicy::WithinLeash(false, false, 50., 100.));
	TestTrue(TEXT("Zero-range weapon can reach the border itself"), HoldPolicy::WithinLeash(false, true, 0., 0.));
	TestFalse(TEXT("Zero-range weapon cannot reach beyond the border"), HoldPolicy::WithinLeash(false, true, .001, 0.));
	return true;
}

bool FHoldPolygonTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Interior point is contained"), HoldPolicy::Contains(Square, { 60., 60. }));
	TestTrue(TEXT("Polygon edge is included"), HoldPolicy::Contains(Square, { 120., 60. }));
	TestTrue(TEXT("Polygon vertex is included"), HoldPolicy::Contains(Square, { 0., 0. }));
	TestFalse(TEXT("Point just beyond an edge is outside"), HoldPolicy::Contains(Square, { 120.001, 60. }));
	TestFalse(TEXT("Empty polygon has no inside"), HoldPolicy::Contains(TConstArrayView<FVector2D>(), { 0., 0. }));
	const FVector2D Line[] = { { 0., 0. }, { 120., 0. } };
	TestFalse(TEXT("Two vertices do not enclose a region"), HoldPolicy::Contains(Line, { 60., 0. }));
	TestEqual(TEXT("Nearest boundary projects onto an edge rather than a vertex"), HoldPolicy::ClosestBoundary(Square, { 150., 60. }), FVector2D(120., 60.));
	TestEqual(TEXT("Inside clamp preserves the requested destination"), HoldPolicy::ClampInside(Square, { 60., 60. }), FVector2D(60., 60.));
	TestEqual(TEXT("Inclusive boundary destination needs no displacement"), HoldPolicy::ClampInside(Square, { 120., 60. }), FVector2D(120., 60.));
	const FVector2D FarCorner = HoldPolicy::ClampInside(Square, { 150., 150. });
	TestTrue(TEXT("Outside corner clamps into the polygon"), HoldPolicy::Contains(Square, FarCorner));
	TestTrue(TEXT("Corner clamp nudges into the actual interior"), FarCorner.X < 120. && FarCorner.Y < 120.);

	for (const bool bReverse : { false, true })
	{
		TArray<FVector2D> Polygon;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Concave); ++Index)
			Polygon.Add(Concave[bReverse ? UE_ARRAY_COUNT(Concave) - 1 - Index : Index]);
		TestTrue(TEXT("Concave arm remains inside for either winding"), HoldPolicy::Contains(Polygon, { 20., 100. }));
		TestFalse(TEXT("Open notch is outside for either winding"), HoldPolicy::Contains(Polygon, { 60., 100. }));
		TestTrue(TEXT("Concave boundary is inclusive"), HoldPolicy::Contains(Polygon, { 40., 100. }));
		const FVector2D Clamped = HoldPolicy::ClampInside(Polygon, { 60., 100. });
		TestTrue(TEXT("Concave clamp result satisfies the shared containment policy"), HoldPolicy::Contains(Polygon, Clamped));
		TestTrue(TEXT("Nudge goes into an arm, not towards the outside centroid"), Clamped.X < 40. || Clamped.X > 80.);
		TestTrue(TEXT("Concave clamp stays at the nearest boundary rather than another part of the region"),
			(Clamped - FVector2D(60., 100.)).Size() <= 20.1);
	}
	return true;
}

bool FHoldSegmentTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Interior straight path is permitted"), HoldPolicy::SegmentInside(Square, { 10., 10. }, { 100., 100. }));
	TestTrue(TEXT("Boundary path is permitted"), HoldPolicy::SegmentInside(Square, { 0., 0. }, { 120., 0. }));
	TestTrue(TEXT("Stationary inside point is permitted"), HoldPolicy::SegmentInside(Square, { 60., 60. }, { 60., 60. }));
	TestFalse(TEXT("Outside endpoint is rejected"), HoldPolicy::SegmentInside(Square, { 60., 60. }, { 130., 60. }));
	TestFalse(TEXT("Outside starting point is rejected"), HoldPolicy::SegmentInside(Square, { 130., 60. }, { 60., 60. }));
	TestFalse(TEXT("Inside endpoints do not permit crossing a concave exterior"), HoldPolicy::SegmentInside(Concave, { 20., 100. }, { 100., 100. }));
	TestFalse(TEXT("Reverse traversal also rejects the notch"), HoldPolicy::SegmentInside(Concave, { 100., 100. }, { 20., 100. }));
	TestTrue(TEXT("Path below the notch remains inside"), HoldPolicy::SegmentInside(Concave, { 20., 20. }, { 100., 20. }));
	TestTrue(TEXT("Path along the concave boundary remains inside"), HoldPolicy::SegmentInside(Concave, { 20., 40. }, { 100., 40. }));
	TestFalse(TEXT("Vertex endpoints cannot bridge the outside notch"), HoldPolicy::SegmentInside(Concave, { 40., 120. }, { 80., 120. }));
	TestFalse(TEXT("Interior sampling detects crossing even when the whole-path midpoint is inside"),
		HoldPolicy::SegmentInside(Concave, { 20., 60. }, { 110., 0. }));
	return true;
}

bool FHoldPostsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Empty post reserves its first slot"), HoldPolicy::ChoosePostSlot(TConstArrayView<int32>()), 0);
	const int32 FirstSlotHole[] = { 1, 2, 3 };
	TestEqual(TEXT("Vacated first slot is reused while other holders keep their slots"), HoldPolicy::ChoosePostSlot(FirstSlotHole), 0);
	const int32 MiddleSlotHole[] = { 0, 3, 1 };
	TestEqual(TEXT("Interior hole avoids duplicating a higher frozen slot"), HoldPolicy::ChoosePostSlot(MiddleSlotHole), 2);
	const int32 DuplicatedSlots[] = { 0, 0, 1, 1, 3, INDEX_NONE, -2 };
	TestEqual(TEXT("Duplicate and negative slot entries do not hide the lowest free slot"), HoldPolicy::ChoosePostSlot(DuplicatedSlots), 2);
	const int32 FullSlots[] = { 2, 0, 1 };
	TestEqual(TEXT("Fully occupied slots extend to the next unused index"), HoldPolicy::ChoosePostSlot(FullSlots), 3);
	const FVector Posts[] = { { 0., 0., 0. }, { 5., 0., 0. }, { 10., 0., 0. }, { 20., 0., 0. } };
	const FVector Assets[] = { { 0., 0., 0. }, { 2., 0., 0. } };
	const FVector Borders[] = { { 18., 0., 0. }, { 20., 0., 0. } };
	const TConstArrayView<FVector> Empty;
	int32 Occupancy[] = { 0, 0, 0, 0 };
	TestEqual(TEXT("Equal occupancy favours placement between assets and borders"), HoldPolicy::ChoosePost(Posts, Occupancy, Assets, Borders), 2);
	Occupancy[0] = Occupancy[1] = Occupancy[2] = 1;
	TestEqual(TEXT("Empty post wins over better positioned occupied posts"), HoldPolicy::ChoosePost(Posts, Occupancy, Assets, Borders), 3);
	Occupancy[3] = 1;
	TestEqual(TEXT("Full posts are shared instead of rejecting orders"), HoldPolicy::ChoosePost(Posts, Occupancy, Assets, Borders), 2);
	TestEqual(TEXT("Assets-only fallback uses their centroid"), HoldPolicy::ChoosePost(Posts, Occupancy, Assets, Empty), 0);
	TestEqual(TEXT("Borders-only fallback uses their centroid"), HoldPolicy::ChoosePost(Posts, Occupancy, Empty, Borders), 3);
	TestEqual(TEXT("No assets or borders falls back to the posts centroid"), HoldPolicy::ChoosePost(Posts, Occupancy, Empty, Empty), 2);
	TestEqual(TEXT("No defend posts has no assignment"), HoldPolicy::ChoosePost(Empty, TConstArrayView<int32>(), Assets, Borders), INDEX_NONE);
	const FVector TiedPosts[] = { { 0., 0., 0. }, { 10., 0., 0. } };
	TestEqual(TEXT("Equal occupancy and placement retain input order"), HoldPolicy::ChoosePost(TiedPosts, TConstArrayView<int32>(), Empty, Empty), 0);

	for (int32& Count : Occupancy)
		Count = 0;
	for (int32 Holder = 0; Holder < 19; ++Holder)
	{
		const int32 Post = HoldPolicy::ChoosePost(Posts, Occupancy, Assets, Borders);
		TestTrue(TEXT("Every overflow holder receives an existing post"), Post >= 0 && Post < UE_ARRAY_COUNT(Posts));
		if (Post >= 0 && Post < UE_ARRAY_COUNT(Posts))
			++Occupancy[Post];
	}
	for (const int32 Count : Occupancy)
		TestTrue(TEXT("Repeated assignment balances least occupancy before placement"), Count == 4 || Count == 5);

	TestEqual(TEXT("First group stays at the selected post"), HoldPolicy::SharedPostOffset(0), FVector::ZeroVector);
	for (int32 Index = 0; Index < 61; ++Index)
	{
		const FVector Offset = HoldPolicy::SharedPostOffset(Index);
		TestEqual(TEXT("Post sharing does not alter terrain height"), Offset.Z, 0.);
		for (int32 Earlier = 0; Earlier < Index; ++Earlier)
			TestTrue(TEXT("Overflow groups receive distinct, group-sized spacing"),
				FVector::DistSquared(Offset, HoldPolicy::SharedPostOffset(Earlier)) >= FMath::Square(750.));
	}
	return true;
}

bool FHoldClippedPostsTest::RunTest(const FString& Parameters)
{
	const FVector2D Region[] = { { -400., -400. }, { 400., -400. }, { 400., 400. }, { -400., 400. } };
	const FVector Post(0., 0., 73.);
	TArray<FVector> Placed;
	for (int32 Slot = 0; Slot < 19; ++Slot)
	{
		const FVector Location = HoldPolicy::ChoosePostLocation(Region, Post, Slot, Placed);
		TestTrue(TEXT("Every shared placement stays in the region"), HoldPolicy::Contains(Region, FVector2D(Location)));
		TestEqual(TEXT("Shared placement preserves terrain height"), Location.Z, Post.Z);
		for (const FVector& Earlier : Placed)
			TestTrue(FString::Printf(TEXT("Shared slot %d at %s remains distinct from %s"), Slot,
						 *Location.ToString(), *Earlier.ToString()),
				FVector::DistSquared2D(Location, Earlier) > 1.);
		Placed.Add(Location);
	}
	const FVector2D Reversed[] = { { -400., -400. }, { -400., 400. }, { 400., 400. }, { 400., -400. } };
	for (const TConstArrayView<FVector2D> Shape : { MakeArrayView(Region), MakeArrayView(Reversed) })
	{
		Placed.Reset();
		const FVector BorderPost(400., 400., 73.);
		for (int32 Slot = 0; Slot < 19; ++Slot)
		{
			const FVector Location = HoldPolicy::ChoosePostLocation(Shape, BorderPost, Slot, Placed);
			TestTrue(TEXT("Border-post sharing stays inside either polygon winding"), HoldPolicy::Contains(Shape, FVector2D(Location)));
			for (const FVector& Earlier : Placed)
				TestTrue(TEXT("Border-post sharing keeps every assigned point distinct"),
					Location.X != Earlier.X || Location.Y != Earlier.Y);
			Placed.Add(Location);
		}
	}
	Placed.Reset();
	const FVector NotchPost(80., 40., 73.);
	for (int32 Slot = 0; Slot < 19; ++Slot)
	{
		const FVector Location = HoldPolicy::ChoosePostLocation(Concave, NotchPost, Slot, Placed);
		TestTrue(TEXT("Concave-post sharing never places a holder in the notch"), HoldPolicy::Contains(Concave, FVector2D(Location)));
		for (const FVector& Earlier : Placed)
			TestTrue(TEXT("Concave border clipping preserves distinct assigned points"),
				Location.X != Earlier.X || Location.Y != Earlier.Y);
		Placed.Add(Location);
	}
	return true;
}

bool FHoldThreatTest::RunTest(const FString& Parameters)
{
	const FVector Positions[] = { { 100., 0., 0. }, { 10., 0., 0. }, { -10., 0., 0. } };
	bool Permitted[] = { true, true, true };
	TestEqual(TEXT("Existing permitted target stays sticky despite closer threats"), HoldPolicy::ChooseThreat(Positions, Permitted, FVector::ZeroVector, 0), 0);
	TestEqual(TEXT("No target chooses nearest, with stable distance ties"), HoldPolicy::ChooseThreat(Positions, Permitted, FVector::ZeroVector, INDEX_NONE), 1);
	Permitted[0] = false;
	TestEqual(TEXT("Death or leash exit replaces the old target with nearest permitted threat"), HoldPolicy::ChooseThreat(Positions, Permitted, FVector::ZeroVector, 0), 1);
	Permitted[1] = false;
	TestEqual(TEXT("Ineligible nearer threat is skipped"), HoldPolicy::ChooseThreat(Positions, Permitted, FVector::ZeroVector, 1), 2);
	TestEqual(TEXT("Out-of-range current index reselects normally"), HoldPolicy::ChooseThreat(Positions, Permitted, FVector::ZeroVector, 10), 2);
	Permitted[2] = false;
	TestEqual(TEXT("No permitted threats clears targeting"), HoldPolicy::ChooseThreat(Positions, Permitted, FVector::ZeroVector, 2), INDEX_NONE);
	TestEqual(TEXT("Empty threat set clears targeting"), HoldPolicy::ChooseThreat(TConstArrayView<FVector>(), TConstArrayView<bool>(), FVector::ZeroVector, 0), INDEX_NONE);
	return true;
}
#endif
