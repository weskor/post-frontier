#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ArmyGroupPolicy.h"
#include "Rules/HoldPolicy.h"

namespace
{
using namespace ArmyGroupPolicy;

TArray<FVector2D> Box(double MinX, double MinY, double MaxX, double MaxY)
{
	return { FVector2D(MinX, MinY), FVector2D(MaxX, MinY), FVector2D(MaxX, MaxY), FVector2D(MinX, MaxY) };
}

FVector At(double X, double Y)
{
	return FVector(X, Y, 100.);
}

TArray<FVector> Slots(const FFormation& Formation, const FFit& Fit, TConstArrayView<FVector2D> Polygon)
{
	TArray<FVector> Points;
	for (int32 Slot = 0; Slot < SlotCount(Formation); ++Slot)
		Points.Add(FittedSlot(Formation, Fit, Polygon, Slot));
	return Points;
}

double Clearance(TConstArrayView<FVector2D> Polygon, const FVector& Point)
{
	return HoldPolicy::Contains(Polygon, FVector2D(Point))
		? FVector2D::Distance(FVector2D(Point), HoldPolicy::ClosestBoundary(Polygon, FVector2D(Point)))
		: -1.;
}

double MinimumClearance(TConstArrayView<FVector2D> Polygon, TConstArrayView<FVector> Points)
{
	double Least = TNumericLimits<double>::Max();
	for (const FVector& Point : Points)
		Least = FMath::Min(Least, Clearance(Polygon, Point));
	return Least;
}

double ClosestPair(TConstArrayView<FVector> Points)
{
	double Least = TNumericLimits<double>::Max();
	for (int32 A = 0; A < Points.Num(); ++A)
		for (int32 B = A + 1; B < Points.Num(); ++B)
			Least = FMath::Min(Least, FVector::Dist2D(Points[A], Points[B]));
	return Least;
}

// Pairs of slots that land on one point (within a centimetre).
int32 SharedPoints(TConstArrayView<FVector> Points)
{
	int32 Shared = 0;
	for (int32 A = 0; A < Points.Num(); ++A)
		for (int32 B = A + 1; B < Points.Num(); ++B)
			Shared += FVector::Dist2D(Points[A], Points[B]) < 1.;
	return Shared;
}

// The per-slot clamp the hold code used before the fit: every slot on its own.
TArray<FVector> ClampedOneByOne(const FFormation& Formation, TConstArrayView<FVector2D> Polygon, const FVector& Post)
{
	TArray<FVector> Points;
	for (int32 Slot = 0; Slot < SlotCount(Formation); ++Slot)
	{
		const FVector2D Inside = HoldPolicy::ClampInside(Polygon, FVector2D(Post + FormationOffset(Formation, Slot)));
		Points.Add(FVector(Inside.X, Inside.Y, Post.Z));
	}
	return Points;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationFitOpenGroundTest, "CoopRTS.Rules.ArmyGroup.Fit.OpenGround",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationFitOpenGroundTest::RunTest(const FString& Parameters)
{
	const TArray<FVector2D> Region = Box(0., 0., 10000., 10000.);
	for (const FFormation& Formation : { FFormation{ true, 6, false }, FFormation{ true, 3, false }, FFormation{ false, 6, true } })
	{
		const FFit Fit = FitForce(Formation, Region, At(5000., 5000.));
		TestTrue(TEXT("A formation well inside its region is not changed"),
			!Fit.bClamped && Fit.Scale == 1.f && Fit.Yaw == 0.f && Fit.Centre == At(5000., 5000.));
		for (int32 Slot = 0; Slot < SlotCount(Formation); ++Slot)
			TestEqual(TEXT("An unfitted slot is exactly centre plus offset"),
				FittedSlot(Formation, Fit, Region, Slot), At(5000., 5000.) + FormationOffset(Formation, Slot));
	}
	TestEqual(TEXT("A produced force has one slot per capacity"), SlotCount({ true, 5, false }), 5);
	TestEqual(TEXT("An unset capacity falls back to the six-slot layout"), SlotCount({ false, 0, false }), 6);

	const FFormation Produced{ true, 6, false };
	const TArray<FVector2D> NoPolygon;
	const TArray<FVector2D> Line = { FVector2D(0., 0.), FVector2D(100., 0.) };
	for (const TArray<FVector2D>* Degenerate : { &NoPolygon, &Line })
	{
		const FFit Fit = FitForce(Produced, *Degenerate, At(3., 4.));
		TestTrue(TEXT("Without region geometry the layout is left alone"),
			!Fit.bClamped && Fit.Scale == 1.f && Fit.Yaw == 0.f && Fit.Centre == At(3., 4.));
	}
	TestTrue(TEXT("No offsets fit as they are"), !FitFormation(Region, At(1., 1.), TArray<FVector>()).bClamped);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationFitBorderPostTest, "CoopRTS.Rules.ArmyGroup.Fit.BorderPosts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationFitBorderPostTest::RunTest(const FString& Parameters)
{
	// The v2-terrain cases: posts 56 cm and 130 cm from a border.
	const TArray<FVector2D> Region = Box(0., -5000., 10000., 5000.);
	for (const double Distance : { 56., 130. })
		for (const FFormation& Formation : { FFormation{ true, 6, false }, FFormation{ false, 6, false } })
		{
			const FVector Post = At(Distance, 0.);
			const FFit Fit = FitForce(Formation, Region, Post);
			const TArray<FVector> Points = Slots(Formation, Fit, Region);
			const FString Label = FString::Printf(TEXT("%s post %.0f cm from the border"),
				Formation.bProduced ? TEXT("Produced") : TEXT("Encounter"), Distance);
			TestFalse(*(Label + TEXT(" fits without clamping")), Fit.bClamped);
			TestTrue(*(Label + TEXT(" keeps every slot inside with its margin")),
				MinimumClearance(Region, Points) >= FitMargin - .1);
			TestTrue(*(Label + TEXT(" gives distinct slots")), ClosestPair(Points) >= 60. && SharedPoints(Points) == 0);
			TestTrue(*(Label + TEXT(" moves the centre inward by at most the shift allowance")),
				Fit.Centre.X >= Post.X && FVector::Dist2D(Fit.Centre, Post) <= MaxFitShift + .01);
			TestTrue(*(Label + TEXT(" keeps the spacing above the floor")), Fit.Scale >= MinFitScale && Fit.Scale <= 1.f);
		}

	// A produced force 56 cm from the border only needs to shift: the spacing stays.
	const FFit Shifted = FitForce({ true, 6, false }, Region, At(56., 0.));
	TestTrue(TEXT("A shift alone is enough when it stays within the allowance"), Shifted.Scale == 1.f && Shifted.Yaw == 0.f);
	TestEqual(TEXT("The shift is the smallest one that clears the margin"), Shifted.Centre.X, 150., .1);
	TestEqual(TEXT("The shift is along the border's normal"), Shifted.Centre.Y, 0., .1);

	// Corners: both axes shift together.
	for (const double Distance : { 20., 56., 130. })
	{
		const FFit Fit = FitForce({ true, 6, false }, Region, At(Distance, -5000. + Distance));
		const TArray<FVector> Points = Slots({ true, 6, false }, Fit, Region);
		TestTrue(TEXT("A produced force's corner post fits inside both borders with distinct slots"),
			!Fit.bClamped && MinimumClearance(Region, Points) >= FitMargin - .1 && SharedPoints(Points) == 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationFitStepsTest, "CoopRTS.Rules.ArmyGroup.Fit.Steps",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationFitStepsTest::RunTest(const FString& Parameters)
{
	const FFormation Produced{ true, 6, false };
	// The produced layout is 220 cm long (x) and 110 cm wide (y). Its slots need 150 cm of clear room each
	// side along x at full spacing: a 260 cm neck shrinks it first.
	const TArray<FVector2D> Neck = Box(0., 0., 260., 5000.);
	FFit Fit = FitForce(Produced, Neck, At(70., 2500.));
	TArray<FVector> Points = Slots(Produced, Fit, Neck);
	TestTrue(TEXT("A neck too narrow at full spacing shrinks the spacing, without turning"),
		!Fit.bClamped && Fit.Yaw == 0.f && Fit.Scale < 1.f && Fit.Scale >= MinFitScale);
	TestEqual(TEXT("The first spacing step that fits is taken"), Fit.Scale, .8f);
	TestTrue(TEXT("The shrunken slots are inside with their margin and apart"),
		MinimumClearance(Neck, Points) >= FitMargin - .1 && ClosestPair(Points) >= MinFitScale * 110. - .1);
	TestTrue(TEXT("The neck's off-centre post shifts just far enough"), FMath::Abs(Fit.Centre.X - 128.) < .1);

	// 200 cm wide: even the floor spacing needs 223 cm, so the formation turns to lie along the corridor.
	const TArray<FVector2D> Corridor = Box(0., 0., 200., 5000.);
	Fit = FitForce(Produced, Corridor, At(100., 2500.));
	Points = Slots(Produced, Fit, Corridor);
	TestTrue(TEXT("A corridor narrower than the floor spacing turns the formation"), !Fit.bClamped && Fit.Yaw != 0.f);
	// Turns are tried from the smallest up, each from full spacing down: 75 degrees needs the 0.7 step, and
	// no smaller turn fits at any spacing.
	TestEqual(TEXT("The smallest sufficient turn is taken"), static_cast<double>(Fit.Yaw), 5. * UE_DOUBLE_PI / 12., 1.e-4);
	TestEqual(TEXT("A turned formation uses the largest spacing that fits that turn"), Fit.Scale, .7f);
	TestTrue(TEXT("Turned slots are inside the corridor with their margin"), MinimumClearance(Corridor, Points) >= FitMargin - .1);
	const FFit Mirror = FitForce(Produced, Box(0., 0., 5000., 200.), At(2500., 100.));
	TestEqual(TEXT("A corridor along the other axis needs no turn"), Mirror.Yaw, 0.f);

	// Too small for anything: slots clamp one by one, at the floor spacing, and stay inside the polygon.
	const TArray<FVector2D> Cell = Box(0., 0., 100., 100.);
	Fit = FitForce(Produced, Cell, At(50., 50.));
	Points = Slots(Produced, Fit, Cell);
	TestTrue(TEXT("Nothing fits a 1 m cell, so the slots clamp at the floor spacing"),
		Fit.bClamped && Fit.Scale == MinFitScale && Fit.Yaw == 0.f && Fit.Centre == At(50., 50.));
	TestTrue(TEXT("Clamped slots never leave the polygon"), MinimumClearance(Cell, Points) >= 0.);

	// A centre outside the polygon is pulled in when the allowance reaches, and clamped when it does not.
	const TArray<FVector2D> Open = Box(0., 0., 10000., 10000.);
	Fit = FitForce(Produced, Open, At(-20., 5000.));
	TestTrue(TEXT("A centre just outside the region is brought inside"),
		!Fit.bClamped && MinimumClearance(Open, Slots(Produced, Fit, Open)) >= FitMargin - .1);
	Fit = FitForce(Produced, Open, At(-2000., 5000.));
	TestTrue(TEXT("A centre far outside the region clamps"), Fit.bClamped);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationFitShapeTest, "CoopRTS.Rules.ArmyGroup.Fit.Concave",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationFitShapeTest::RunTest(const FString& Parameters)
{
	// An L: the reflex corner at (4000, 4000) points into the region.
	const TArray<FVector2D> L = { FVector2D(0., 0.), FVector2D(8000., 0.), FVector2D(8000., 4000.),
		FVector2D(4000., 4000.), FVector2D(4000., 8000.), FVector2D(0., 8000.) };
	const FFormation Produced{ true, 6, false };
	for (const double Along : { 0., 30., 80., 130. })
	{
		// Posts hugging the reflex corner from inside the corner's two arms.
		for (const FVector& Post : { At(4000. - Along - 20., 4000. + Along + 20.), At(4000. + Along + 20., 4000. - Along - 20.),
				 At(3900., 4000. + Along + 20.) })
		{
			const FFit Fit = FitForce(Produced, L, Post);
			const TArray<FVector> Points = Slots(Produced, Fit, L);
			TestTrue(TEXT("Slots near a reflex corner stay inside the concave region with their margin"),
				MinimumClearance(L, Points) >= (Fit.bClamped ? 0. : FitMargin - .1) && SharedPoints(Points) == 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationFitSweepTest, "CoopRTS.Rules.ArmyGroup.Fit.Sweep",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationFitSweepTest::RunTest(const FString& Parameters)
{
	// Every post from the border to 400 cm in, along both edges of a corner: the invariants of a fit, the
	// determinism of the rules, and the holders that used to stand on one clamped point.
	const TArray<FVector2D> Region = Box(0., 0., 6000., 6000.);
	struct FKind
	{
		FFormation Formation;
		const TCHAR* Name;
		int32 Posts = 0, SharedBefore = 0, SharedAfter = 0, SharedRigid = 0, Clamped = 0, ClampedAwayFromCorner = 0;
	};
	FKind Kinds[] = { { { true, 6, false }, TEXT("produced 6") }, { { true, 4, false }, TEXT("produced 4") },
		{ { false, 6, false }, TEXT("encounter") } };
	for (FKind& Kind : Kinds)
		for (double X = 0.; X <= 400.; X += 20.)
			for (double Y = 0.; Y <= 400.; Y += 20.)
			{
				const FVector Post = At(X, Y);
				const FFit Fit = FitForce(Kind.Formation, Region, Post);
				const FFit Again = FitForce(Kind.Formation, Region, Post);
				const TArray<FVector> Points = Slots(Kind.Formation, Fit, Region);
				if (Fit.Centre != Again.Centre || Fit.Scale != Again.Scale || Fit.Yaw != Again.Yaw || Fit.bClamped != Again.bClamped)
				{
					AddError(FString::Printf(TEXT("Fit around %s is not deterministic"), *Post.ToCompactString()));
					return true;
				}
				if (!(Fit.Scale >= MinFitScale && Fit.Scale <= 1.f && MinimumClearance(Region, Points) >= 0.
						&& (Fit.bClamped || (MinimumClearance(Region, Points) >= FitMargin - .1
											   && FVector::Dist2D(Fit.Centre, Post) <= MaxFitShift + .01))))
				{
					AddError(FString::Printf(TEXT("Fit around %s breaks an invariant: scale %.2f yaw %.2f clamped %d"),
						*Post.ToCompactString(), Fit.Scale, Fit.Yaw, Fit.bClamped));
					return true;
				}
				++Kind.Posts;
				Kind.Clamped += Fit.bClamped;
				Kind.ClampedAwayFromCorner += Fit.bClamped && (X >= 20. || Y >= 20.);
				Kind.SharedBefore += SharedPoints(ClampedOneByOne(Kind.Formation, Region, Post));
				Kind.SharedAfter += SharedPoints(Points);
				Kind.SharedRigid += Fit.bClamped ? 0 : SharedPoints(Points);
			}
	for (const FKind& Kind : Kinds)
	{
		AddInfo(FString::Printf(TEXT("%s: %d posts within 400 cm of a corner; slot pairs sharing a point: %d with per-slot clamping, %d fitted; clamped fits %d (%d not at the very corner)"),
			Kind.Name, Kind.Posts, Kind.SharedBefore, Kind.SharedAfter, Kind.Clamped, Kind.ClampedAwayFromCorner));
		if (Kind.Formation.bProduced)
		{
			TestEqual(*FString::Printf(TEXT("%s: no rigidly fitted slots share a point"), Kind.Name), Kind.SharedRigid, 0);
			TestEqual(*FString::Printf(TEXT("%s: only a post right in the corner needs the per-slot clamp"), Kind.Name),
				Kind.ClampedAwayFromCorner, 0);
		}
	}
	return true;
}
#endif
