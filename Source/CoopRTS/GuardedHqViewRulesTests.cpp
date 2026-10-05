#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/AnnouncerPolicy.h"
#include "Rules/GuardedHqView.h"

// Row wording and node-mark placement of the guarded-HQ readability pass (ui.md surface 9), world-free.

namespace
{
FString Title(const TCHAR* Id, int32 Number)
{
	const AnnouncerPolicy::FDefinition* Definition = AnnouncerPolicy::Find(FName(Id));
	TStringBuilder<128> Out;
	GuardedHqView::AppendFeedTitle(Out, Definition ? FStringView(Definition->Text) : FStringView(), Id, Number);
	return FString(Out.ToView());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedHqFeedTextTest, "CoopRTS.Rules.GuardedHqView.FeedText",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGuardedHqFeedTextTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A node loss carries the nodes left"), Title(TEXT("own_node_lost"), 1), FString(TEXT("Hardline Failover Node lost \u00B7 1 left")));
	TestEqual(TEXT("The last node reads 0 left"), Title(TEXT("enemy_node_lost"), 0), FString(TEXT("The Lattice Failover Node lost \u00B7 0 left")));
	TestEqual(TEXT("A revival carries the restored percent"), Title(TEXT("own_hq_online"), 25), FString(TEXT("Hardline HQ back online at 25% HP")));
	TestEqual(TEXT("The emergency row keeps the announcer's sentence"), Title(TEXT("own_emergency"), 0),
		FString(TEXT("Hardline HQ offline. Emergency forces deployed.")));
	TestEqual(TEXT("An ordinary row is untouched by the number"), Title(TEXT("region_lost"), 7), FString(TEXT("Region lost.")));
	TestEqual(TEXT("A damage row keeps its sentence, tier or not"), Title(TEXT("own_hq_critical"), 2),
		FString(TEXT("Hardline critical. Twenty-five percent remaining.")));

	using GuardedHqView::EFeedKind;
	TestTrue(TEXT("Both sides' node losses classify as node losses"),
		GuardedHqView::Classify(TEXT("own_node_lost")) == EFeedKind::NodeLost && GuardedHqView::Classify(TEXT("enemy_node_lost")) == EFeedKind::NodeLost);
	TestTrue(TEXT("Both sides' emergencies classify as emergencies, the only rows with the badge"),
		GuardedHqView::Classify(TEXT("own_emergency")) == EFeedKind::Emergency && GuardedHqView::Classify(TEXT("enemy_emergency")) == EFeedKind::Emergency);
	TestTrue(TEXT("HQ offline is not the emergency row"), GuardedHqView::Classify(TEXT("own_hq_offline")) == EFeedKind::Plain);
	TestEqual(TEXT("The badge text"), FString(GuardedHqView::EmergencyBadge), FString(TEXT("EMERGENCY")));

	// Every announcer definition that classifies as a guarded-HQ row ends its sentence with a full stop for the number to replace.
	for (const AnnouncerPolicy::FDefinition& Definition : AnnouncerPolicy::Definitions())
		if (GuardedHqView::Classify(Definition.Id) == EFeedKind::NodeLost || GuardedHqView::Classify(Definition.Id) == EFeedKind::BackOnline)
			TestTrue(*FString::Printf(TEXT("%s ends in a full stop"), Definition.Id), FStringView(Definition.Text).EndsWith(TEXT(".")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedHqNodeMarksTest, "CoopRTS.Rules.GuardedHqView.NodeMarks",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGuardedHqNodeMarksTest::RunTest(const FString& Parameters)
{
	using namespace GuardedHqView;
	const FVector2D Origin(100., 40.);
	constexpr double Size = 210.;
	const FVector2D Centre = Origin + FVector2D(Size, Size) * .5;
	const double Drop = MinimapNodeDrop, Pitch = MinimapNodePitch, Radius = MinimapNodeRadius;

	const FNodeMarks Mid = PlaceNodeMarks(Centre, Origin, Size, 2, {});
	TestEqual(TEXT("Two nodes, two marks"), Mid.Count, 2);
	TestEqual(TEXT("Marks sit below the HQ"), Mid.Centre[0].Y, Centre.Y + Drop);
	TestEqual(TEXT("Both on one row"), Mid.Centre[0].Y, Mid.Centre[1].Y);
	TestEqual(TEXT("Centred on the HQ"), (Mid.Centre[0].X + Mid.Centre[1].X) * .5, Centre.X);
	TestEqual(TEXT("One pitch apart"), Mid.Centre[1].X - Mid.Centre[0].X, Pitch);

	const FNodeMarks One = PlaceNodeMarks(Centre, Origin, Size, 1, {});
	TestEqual(TEXT("A single node gets one mark"), One.Count, 1);
	TestEqual(TEXT("A single node sits straight below the HQ"), One.Centre[0].X, Centre.X);
	TestEqual(TEXT("No nodes, no marks"), PlaceNodeMarks(Centre, Origin, Size, 0, {}).Count, 0);
	TestEqual(TEXT("Never more marks than nodes a side has"), PlaceNodeMarks(Centre, Origin, Size, 9, {}).Count, HqHoldPolicy::NodesPerHq);

	const FVector2D Bottom(Centre.X, Origin.Y + Size - 6.);
	TestEqual(TEXT("An HQ on the lower edge puts its row above"), PlaceNodeMarks(Bottom, Origin, Size, 2, {}).Centre[0].Y, Bottom.Y - Drop);

	const FNodeMarks Shifted = PlaceNodeMarks(FVector2D(Origin.X + 3., Centre.Y), Origin, Size, 2, {});
	TestEqual(TEXT("A row at the left edge shifts to just inside the square"), Shifted.Centre[0].X - Radius, Origin.X);
	TestEqual(TEXT("The shifted row keeps its pitch"), Shifted.Centre[1].X - Shifted.Centre[0].X, Pitch);
	const FNodeMarks ShiftedRight = PlaceNodeMarks(FVector2D(Origin.X + Size - 3., Centre.Y), Origin, Size, 2, {});
	TestEqual(TEXT("A row at the right edge shifts to just inside the square"), ShiftedRight.Centre[1].X + Radius, Origin.X + Size);

	// Deposit markers (radius MinimapDepositRadius) sit in the main: the row keeps clear of them, flipping above when
	// that clears more, and stays below when they are no nearer than the clearance or both sides are equally blocked.
	const double Reach = Radius + MinimapDepositRadius + MinimapMarkGap;
	auto Hits = [&](const FNodeMarks& Marks, const FVector2D& Deposit) {
		int32 Count = 0;
		for (int32 Index = 0; Index < Marks.Count; ++Index)
			Count += FVector2D::Distance(Marks.Centre[Index], Deposit) < Reach;
		return Count;
	};
	const FVector2D BelowLeft = Centre + FVector2D(-Pitch * .5 + 1., Drop - 2.);
	TestEqual(TEXT("The row blocked below flips above"), PlaceNodeMarks(Centre, Origin, Size, 2, MakeArrayView(&BelowLeft, 1)).Centre[0].Y, Centre.Y - Drop);
	const FVector2D BelowFar = Centre + FVector2D(0., Drop + Reach + 4.);
	TestEqual(TEXT("A deposit no nearer than the clearance leaves the row below"), PlaceNodeMarks(Centre, Origin, Size, 2, MakeArrayView(&BelowFar, 1)).Centre[0].Y, Centre.Y + Drop);
	const FVector2D Mixed[] = { BelowLeft, Centre + FVector2D(-Pitch * .5, -Drop), Centre + FVector2D(Pitch * .5, -Drop) };
	const FNodeMarks Blocked = PlaceNodeMarks(Centre, Origin, Size, 2, Mixed);
	TestEqual(TEXT("Blocked on both sides, the row with fewer marks on deposits wins"), Blocked.Centre[0].Y, Centre.Y + Drop);
	TestEqual(TEXT("... and only one of its marks sits on a deposit"), Hits(Blocked, Mixed[0]) + Hits(Blocked, Mixed[1]) + Hits(Blocked, Mixed[2]), 1);
	const FVector2D AboveBottom = Bottom + FVector2D(-Pitch * .5, -Drop);
	TestEqual(TEXT("A side the square cannot hold is never chosen, blocked or not"),
		PlaceNodeMarks(Bottom, Origin, Size, 2, MakeArrayView(&AboveBottom, 1)).Centre[0].Y, Bottom.Y - Drop);

	// Over the whole square, at both minimap sizes, no mark meets the HQ square, its offline X, a Fortify ring or a JEV
	// badge box (all centred on the HQ, the largest reaching MinimapRingRadius), and every mark stays inside the square.
	FString Failure;
	for (const double Edge : { 144., 210. })
		for (int32 Column = 0; Column <= 20; ++Column)
			for (int32 Row = 0; Row <= 20; ++Row)
			{
				const FVector2D Hq = Origin + FVector2D(Column, Row) * (Edge / 20.);
				const FNodeMarks Marks = PlaceNodeMarks(Hq, Origin, Edge, 2, {});
				for (int32 Index = 0; Index < Marks.Count && Failure.IsEmpty(); ++Index)
				{
					const FVector2D Mark = Marks.Centre[Index];
					const bool bClear = FMath::Abs(Mark.Y - Hq.Y) - Radius >= MinimapRingRadius;
					const bool bInside = Mark.X - Radius >= Origin.X - UE_KINDA_SMALL_NUMBER && Mark.X + Radius <= Origin.X + Edge + UE_KINDA_SMALL_NUMBER
						&& Mark.Y - Radius >= Origin.Y - UE_KINDA_SMALL_NUMBER && Mark.Y + Radius <= Origin.Y + Edge + UE_KINDA_SMALL_NUMBER;
					if (!bClear || !bInside)
						Failure = FString::Printf(TEXT("edge %.0f HQ %s mark %d at %s (clear %d, inside %d)"), Edge, *Hq.ToString(), Index, *Mark.ToString(), bClear, bInside);
				}
			}
	TestTrue(*FString::Printf(TEXT("Every mark clears the HQ's ring and stays inside the square: %s"), *Failure), Failure.IsEmpty());
	return true;
}

#endif
