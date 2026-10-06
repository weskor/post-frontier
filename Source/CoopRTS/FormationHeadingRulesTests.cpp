#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ArmyGroupPolicy.h"
#include "Rules/HoldPolicy.h"

namespace
{
using namespace ArmyGroupPolicy;
using FIndices = TArray<int32, TInlineAllocator<8>>;

const FFormation Produced{ true, 6, false };
constexpr float Degree = UE_PI / 180.f;

FVector At(double X, double Y)
{
	return FVector(X, Y, 100.);
}

TArray<FMarchUnit, TInlineAllocator<8>> Marchers(std::initializer_list<FVector2D> Positions, std::initializer_list<int32> Ranks = {})
{
	TArray<FMarchUnit, TInlineAllocator<8>> Units;
	int32 Slot = 0;
	const int32* Rank = Ranks.begin();
	for (const FVector2D& Position : Positions)
		Units.Add({ Position, Rank != Ranks.end() ? *Rank++ : 1, Slot++ });
	return Units;
}

// Six units scattered around (1000, -2000), their rigid slots 0..5.
TArray<FMarchUnit, TInlineAllocator<8>> Scattered()
{
	return Marchers({ { 900., -2100. }, { 1100., -2100. }, { 900., -2000. }, { 1100., -2000. }, { 900., -1900. }, { 1100., -1900. } });
}

// Reference for AssignSlots: every permutation, the cheapest that keeps the rows, the first of equal cost.
struct FReference
{
	TArray<FVector2D> Positions, Slots;
	TArray<int32> Ranks;
	FVector2D Forward;
	double Best = TNumericLimits<double>::Max();
	FIndices Chosen, Current;
	uint32 Used = 0;

	bool Feasible() const
	{
		for (int32 A = 0; A < Current.Num(); ++A)
			for (int32 B = 0; B < Current.Num(); ++B)
				if (Ranks[A] < Ranks[B]
					&& FVector2D::DotProduct(Slots[Current[A]], Forward) < FVector2D::DotProduct(Slots[Current[B]], Forward) - 1.)
					return false;
		return true;
	}
	void Run(int32 Unit, double Cost)
	{
		if (Unit == Positions.Num())
		{
			if (Cost < Best - 1.e-6 && Feasible())
			{
				Best = Cost;
				Chosen = Current;
			}
			return;
		}
		for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
			if (!(Used & (1u << Slot)))
			{
				Current[Unit] = Slot;
				Used |= 1u << Slot;
				Run(Unit + 1, Cost + FVector2D::Distance(Positions[Unit], Slots[Slot]));
				Used &= ~(1u << Slot);
			}
	}
};

double CostOf(TConstArrayView<FVector2D> Positions, TConstArrayView<FVector2D> Slots, const FIndices& SlotOfUnit)
{
	double Cost = 0.;
	for (int32 Unit = 0; Unit < Positions.Num(); ++Unit)
		Cost += FVector2D::Distance(Positions[Unit], Slots[SlotOfUnit[Unit]]);
	return Cost;
}

// A small deterministic generator for the property checks.
struct FLcg
{
	uint32 State = 12345u;
	double Next(double Range)
	{
		State = State * 1664525u + 1013904223u;
		return (static_cast<double>(State >> 8) / 16777216. - .5) * 2. * Range;
	}
};

double LeastGap(const FLegPlan& Plan)
{
	double Least = TNumericLimits<double>::Max();
	for (int32 A = 0; A < Plan.Targets.Num(); ++A)
		for (int32 B = A + 1; B < Plan.Targets.Num(); ++B)
			Least = FMath::Min(Least, FVector::Dist2D(Plan.Targets[A], Plan.Targets[B]));
	return Least;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingChooseTest, "CoopRTS.Rules.ArmyGroup.Heading.Choose",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationHeadingChooseTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Without a previous plan the desired heading is taken"), ChooseHeading(false, 1.f, 2.f, 100.), 2.f);
	TestEqual(TEXT("A small turn keeps the previous heading even long after it"), ChooseHeading(true, 0.f, 20.f * Degree, 100.), 0.f);
	TestEqual(TEXT("A turn exactly at the threshold is still small"), ChooseHeading(true, 0.f, HeadingTurnThreshold, 100.), 0.f);
	TestEqual(TEXT("A large turn inside the cooldown keeps the previous heading"),
		ChooseHeading(true, 0.f, 90.f * Degree, HeadingCooldownSeconds - .1), 0.f);
	TestEqual(TEXT("A large turn after the cooldown is taken"),
		ChooseHeading(true, 0.f, 90.f * Degree, HeadingCooldownSeconds), 90.f * Degree);
	TestEqual(TEXT("A turn across the angle wrap is measured the short way"),
		ChooseHeading(true, 170.f * Degree, -170.f * Degree, 100.), 170.f * Degree);
	TestEqual(TEXT("A large turn across the wrap is taken after the cooldown"),
		ChooseHeading(true, 100.f * Degree, -100.f * Degree, 100.), -100.f * Degree);

	TestTrue(TEXT("An intermediate leg of a long march is a column"), ChooseLegShape(true, true, ColumnMinDistance, false) == ELegShape::Column);
	TestTrue(TEXT("The leg into the target region is a box"), ChooseLegShape(true, false, 9000.f, false) == ELegShape::Box);
	TestTrue(TEXT("A short intermediate leg is a box"), ChooseLegShape(true, true, ColumnMinDistance - 1.f, false) == ELegShape::Box);
	TestTrue(TEXT("A force that is not marching (retreat, withdrawal) is a box"), ChooseLegShape(false, true, 9000.f, true) == ELegShape::Box);
	TestTrue(TEXT("A force in column stays in it inside the entry distance"), ChooseLegShape(true, true, ColumnExitDistance, true) == ELegShape::Column);
	TestTrue(TEXT("A force in column leaves it below the exit distance"), ChooseLegShape(true, true, ColumnExitDistance - 1.f, true) == ELegShape::Box);
	TestTrue(TEXT("A force in a box does not enter a column inside the entry distance"), ChooseLegShape(true, true, ColumnExitDistance, false) == ELegShape::Box);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingMemoryTest, "CoopRTS.Rules.ArmyGroup.Heading.Memory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationHeadingMemoryTest::RunTest(const FString& Parameters)
{
	const FVector2D Goal(5000., 0.);
	FLegMemory Memory;
	FLegChoice Choice = ChooseLeg(Memory, Goal, 0.f, true, true, 4000.f, 100.);
	TestTrue(TEXT("The first plan takes the desired heading and a column"), Choice.Yaw == 0.f && Choice.Shape == ELegShape::Column && Memory.bPlanned);
	TestEqual(TEXT("The first plan starts the cooldown"), Memory.TurnedAt, 100.);

	// Re-plans of the same leg within the cooldown: a large turn is held, and re-planning does not restart the clock.
	for (const double Now : { 101., 102., 103., 103.9 })
	{
		Choice = ChooseLeg(Memory, Goal + FVector2D(10., 0.), 90.f * Degree, true, true, 4000.f, Now);
		TestTrue(TEXT("A large turn inside the cooldown keeps the heading"), Choice.Yaw == 0.f);
	}
	TestEqual(TEXT("Held plans leave the last turn time alone"), Memory.TurnedAt, 100.);
	Choice = ChooseLeg(Memory, Goal, 90.f * Degree, true, true, 4000.f, 104.);
	TestEqual(TEXT("The large turn is taken once the cooldown since the last turn has run"), Choice.Yaw, 90.f * Degree);
	TestEqual(TEXT("A turn restarts the cooldown"), Memory.TurnedAt, 104.);
	Choice = ChooseLeg(Memory, Goal, 100.f * Degree, true, true, 4000.f, 104.5);
	TestEqual(TEXT("A small turn after it changes nothing"), Choice.Yaw, 90.f * Degree);
	TestEqual(TEXT("and does not restart the clock"), Memory.TurnedAt, 104.);

	// A redirect (a new goal) is a new leg: taken at once, even inside the cooldown and for a small turn.
	Choice = ChooseLeg(Memory, Goal + FVector2D(0., 3000.), 80.f * Degree, true, true, 4000.f, 104.6);
	TestEqual(TEXT("A redirect takes its heading fresh inside the cooldown"), Choice.Yaw, 80.f * Degree);
	TestEqual(TEXT("and restarts the cooldown"), Memory.TurnedAt, 104.6);

	// The shape has its own hysteresis band, and a redirect forgets that the force was in column.
	FLegMemory InColumn;
	ChooseLeg(InColumn, Goal, 0.f, true, true, 4000.f, 0.);
	TestTrue(TEXT("A re-plan of the same leg at 1300 cm stays a column"), ChooseLeg(InColumn, Goal, 0.f, true, true, 1300.f, 1.).Shape == ELegShape::Column);
	TestTrue(TEXT("At 1100 cm it leaves the column"), ChooseLeg(InColumn, Goal, 0.f, true, true, 1100.f, 2.).Shape == ELegShape::Box);
	FLegMemory Redirected;
	ChooseLeg(Redirected, Goal, 0.f, true, true, 4000.f, 0.);
	TestTrue(TEXT("A redirect at 1300 cm is judged by the entry distance"),
		ChooseLeg(Redirected, Goal + FVector2D(0., 3000.), 0.f, true, true, 1300.f, 1.).Shape == ELegShape::Box);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingJitterTest, "CoopRTS.Rules.ArmyGroup.Heading.Jitter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationHeadingJitterTest::RunTest(const FString& Parameters)
{
	bool bVaries = false, bBounded = true, bRepeats = true, bSeeds = false;
	for (int32 Index = 0; Index < 64; ++Index)
	{
		const float Jitter = SlotJitter(7, Index);
		bBounded &= FMath::Abs(Jitter) <= JitterRadius;
		bRepeats &= Jitter == SlotJitter(7, Index);
		bVaries |= Jitter != SlotJitter(7, 0);
		bSeeds |= Jitter != SlotJitter(8, Index);
	}
	TestTrue(TEXT("Jitter stays within its radius"), bBounded);
	TestTrue(TEXT("Jitter is the same on every call"), bRepeats);
	TestTrue(TEXT("Jitter differs between slots, so a column is not a ruled line"), bVaries);
	TestTrue(TEXT("Jitter differs between forces"), bSeeds);

	// In a plan the jitter is lateral only and has its mean removed: the file keeps exactly its spacing and its centroid.
	const FLegPlan Plan = PlanLeg(Produced, {}, At(0., 0.), Marchers({ { 0., 0. }, { 10., 0. }, { 20., 0. }, { 30., 0. }, { 40., 0. }, { 50., 0. } }),
		ELegShape::Column, 0.f, 7);
	FVector Mean = FVector::ZeroVector;
	double Widest = 0.;
	TArray<double> Along;
	for (const FVector& Target : Plan.Targets)
	{
		Mean += Target / Plan.Targets.Num();
		Widest = FMath::Max(Widest, FMath::Abs(Target.Y));
		Along.Add(Target.X);
	}
	Along.Sort();
	TestTrue(TEXT("A column's jitter leaves its mean at the centre"), FVector::Dist2D(Mean, At(0., 0.)) < .01);
	TestTrue(TEXT("A column's lateral jitter stays within twice the radius"), Widest <= 2. * JitterRadius);
	for (int32 Index = 1; Index < Along.Num(); ++Index)
		TestTrue(TEXT("Jitter leaves the spacing along the file exactly"), FMath::Abs(Along[Index] - Along[Index - 1] - ColumnSpacing) < .01);
	const FLegPlan Other = PlanLeg(Produced, {}, At(0., 0.), Marchers({ { 0., 0. }, { 10., 0. }, { 20., 0. }, { 30., 0. }, { 40., 0. }, { 50., 0. } }),
		ELegShape::Column, 0.f, 8);
	bool bDiffers = false;
	for (int32 Index = 0; Index < Plan.Targets.Num(); ++Index)
		bDiffers |= FMath::Abs(Plan.Targets[Index].Y - Other.Targets[Index].Y) > .01;
	TestTrue(TEXT("Two forces' columns wobble differently"), bDiffers);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingColumnTest, "CoopRTS.Rules.ArmyGroup.Heading.Column",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationHeadingColumnTest::RunTest(const FString& Parameters)
{
	const FVector Centre = At(1000., -2000.);
	const auto Units = Scattered();
	// The heading is +Y.
	const FLegPlan Plan = PlanLeg(Produced, {}, Centre, Units, ELegShape::Column, UE_HALF_PI, 3);
	TestEqual(TEXT("One destination per unit"), Plan.Targets.Num(), 6);
	FVector Mean = FVector::ZeroVector;
	TArray<double> Along;
	double Lateral = 0.;
	for (const FVector& Target : Plan.Targets)
	{
		Mean += Target / 6.;
		Along.Add(Target.Y);
		Lateral = FMath::Max(Lateral, FMath::Abs(Target.X - Centre.X));
	}
	TestTrue(TEXT("A full column is centred on the force"), FVector::Dist2D(Mean, Centre) < .01);
	TestTrue(TEXT("The column runs along the heading: lateral spread is only the jitter"), Lateral <= 2. * JitterRadius);
	Along.Sort();
	for (int32 Index = 1; Index < Along.Num(); ++Index)
		TestTrue(TEXT("Neighbours in the file stand one spacing apart"), FMath::Abs(Along[Index] - Along[Index - 1] - ColumnSpacing) < .01);
	TestTrue(TEXT("No two column slots are closer than a capsule diameter plus clearance"), LeastGap(Plan) >= ColumnFloorSpacing);

	// A heading along -X turns the file around.
	const FLegPlan West = PlanLeg(Produced, {}, Centre, Units, ELegShape::Column, UE_PI, 3);
	double WestSpan = 0., WestLateral = 0.;
	for (const FVector& Target : West.Targets)
	{
		WestSpan = FMath::Max(WestSpan, FMath::Abs(Target.X - Centre.X));
		WestLateral = FMath::Max(WestLateral, FMath::Abs(Target.Y - Centre.Y));
	}
	TestTrue(TEXT("A heading of 180 degrees lays the file along X"), WestSpan > 190. && WestLateral <= 2. * JitterRadius);

	// Two survivors in slots 4 and 5: the file is centred on the survivors' rigid centroid, as the box would be.
	const FVector Rigid = (FormationOffset(Produced, 4) + FormationOffset(Produced, 5)) / 2.;
	TArray<FMarchUnit, TInlineAllocator<8>> Two;
	Two.Add({ FVector2D(900., -2100.), 1, 4 });
	Two.Add({ FVector2D(1100., -2100.), 1, 5 });
	const FLegPlan Depleted = PlanLeg(Produced, {}, Centre, Two, ELegShape::Column, UE_HALF_PI, 3);
	TestTrue(TEXT("A depleted column keeps the occupied slots' centroid"),
		FVector::Dist2D((Depleted.Targets[0] + Depleted.Targets[1]) / 2., Centre + Rigid) < .01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingTailTest, "CoopRTS.Rules.ArmyGroup.Heading.Tail",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationHeadingTailTest::RunTest(const FString& Parameters)
{
	const TArray<FVector2D> File = { FVector2D(0., 40.), FVector2D(0., -40.) };
	TestTrue(TEXT("A column heading along +Y has its tail behind the rearmost slot"),
		FVector2D::Distance(ColumnTail(File, UE_HALF_PI), FVector2D(0., -40. - ColumnSpacing)) < .01);
	TestTrue(TEXT("A column heading along -Y has its tail on the other side"),
		FVector2D::Distance(ColumnTail(File, -UE_HALF_PI), FVector2D(0., 40. + ColumnSpacing)) < .01);
	TestTrue(TEXT("An empty file has no tail"), ColumnTail(TArray<FVector2D>(), 0.f).IsZero());
	const TArray<FVector2D> One = { FVector2D(100., 100.) };
	TestTrue(TEXT("A file of one leaves one spacing behind it"),
		FVector2D::Distance(ColumnTail(One, 0.f), FVector2D(100. - ColumnSpacing, 100.)) < .01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingBoxTest, "CoopRTS.Rules.ArmyGroup.Heading.Box",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationHeadingBoxTest::RunTest(const FString& Parameters)
{
	const FVector Centre = At(1000., -2000.);
	const auto Units = Scattered();
	const FLegPlan Plan = PlanLeg(Produced, {}, Centre, Units, ELegShape::Box, .7f, 3);
	// The box is the force's own layout, only reassigned: the set of points is the six rigid slots.
	bool bAll = true;
	for (int32 Slot = 0; Slot < 6; ++Slot)
	{
		bool bFound = false;
		for (const FVector& Target : Plan.Targets)
			bFound |= FVector::Dist2D(Target, Centre + FormationOffset(Produced, Slot)) < .01;
		bAll &= bFound;
	}
	TestTrue(TEXT("A box assigns exactly the six rigid slots, whatever the heading"), bAll);

	// Beside a border the box is the fitted one, inside the region.
	const TArray<FVector2D> Region = { FVector2D(0., -5000.), FVector2D(10000., -5000.), FVector2D(10000., 5000.), FVector2D(0., 5000.) };
	const FVector Post = At(56., 0.);
	const FLegPlan Fitted = PlanLeg(Produced, Region, Post, Units, ELegShape::Box, 0.f, 3);
	const FFit Direct = FitForce(Produced, Region, Post);
	bool bSameSet = true, bInside = true;
	for (int32 Slot = 0; Slot < 6; ++Slot)
	{
		bool bFound = false;
		for (const FVector& Target : Fitted.Targets)
			bFound |= FVector::Dist2D(Target, FittedSlot(Produced, Direct, Region, Slot)) < .01;
		bSameSet &= bFound;
	}
	for (const FVector& Target : Fitted.Targets)
		bInside &= HoldPolicy::Contains(Region, FVector2D(Target));
	TestTrue(TEXT("A box beside a border uses the round-1 fit"), bSameSet && bInside && !Fitted.Fit.bClamped);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingFitTest, "CoopRTS.Rules.ArmyGroup.Heading.ColumnFit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationHeadingFitTest::RunTest(const FString& Parameters)
{
	const auto Units = Scattered();
	const auto Inside = [](const TArray<FVector2D>& Polygon, const FLegPlan& Plan, double Margin) {
		for (const FVector& Target : Plan.Targets)
		{
			const FVector2D Flat(Target);
			if (!HoldPolicy::Contains(Polygon, Flat) || FVector2D::Distance(Flat, HoldPolicy::ClosestBoundary(Polygon, Flat)) < Margin)
				return false;
		}
		return true;
	};
	// A column in a neck: the file is fitted too, so every slot is inside with its margin.
	const TArray<FVector2D> Neck = { FVector2D(0., 0.), FVector2D(300., 0.), FVector2D(300., 6000.), FVector2D(0., 6000.) };
	const FLegPlan Column = PlanLeg(Produced, Neck, At(60., 3000.), Units, ELegShape::Column, UE_HALF_PI, 3);
	TestTrue(TEXT("A column in a 3 m neck shifts to the middle and keeps its margin"),
		Column.Shape == ELegShape::Column && Inside(Neck, Column, FitMargin - .1) && !Column.Fit.bClamped);

	// A corridor along X: a column along Y does not fit and turns to lie along it, at its full spacing.
	const TArray<FVector2D> Corridor = { FVector2D(0., 0.), FVector2D(6000., 0.), FVector2D(6000., 200.), FVector2D(0., 200.) };
	const FLegPlan Turned = PlanLeg(Produced, Corridor, At(3000., 100.), Units, ELegShape::Column, UE_HALF_PI, 3);
	TestTrue(TEXT("A column that cannot fit a 2 m corridor turns to lie along it"),
		Turned.Shape == ELegShape::Column && Inside(Corridor, Turned, 0.) && Turned.Fit.Yaw != 0.f && LeastGap(Turned) >= ColumnFloorSpacing);

	// A cell 4.2 m long: the fit could only squeeze the file to 64 cm spacing, so the box is planned instead.
	const TArray<FVector2D> Short = { FVector2D(0., 0.), FVector2D(300., 0.), FVector2D(300., 420.), FVector2D(0., 420.) };
	const FLegPlan Squeezed = PlanLeg(Produced, Short, At(150., 210.), Units, ELegShape::Column, UE_HALF_PI, 3);
	TestTrue(TEXT("A file the fit would squeeze below the floor spacing becomes the box"), Squeezed.Shape == ELegShape::Box);
	TestTrue(TEXT("and the box keeps every pair apart"), LeastGap(Squeezed) >= ColumnFloorSpacing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingAssignTest, "CoopRTS.Rules.ArmyGroup.Heading.Assign",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationHeadingAssignTest::RunTest(const FString& Parameters)
{
	FIndices Result;
	// Two units standing on each other's side take the near slots: no crossing.
	AssignSlots(TArray<FVector2D>{ FVector2D(90., 0.), FVector2D(10., 0.) }, TArray<int32>{ 1, 1 },
		TArray<FVector2D>{ FVector2D(0., 0.), FVector2D(100., 0.) }, FVector2D(1., 0.), Result);
	TestTrue(TEXT("Swapped units take the slots next to them"), Result.Num() == 2 && Result[0] == 1 && Result[1] == 0);

	// Equal costs: the first unit takes the first slot.
	AssignSlots(TArray<FVector2D>{ FVector2D(50., 0.), FVector2D(50., 0.) }, TArray<int32>{ 1, 1 },
		TArray<FVector2D>{ FVector2D(0., 0.), FVector2D(100., 0.) }, FVector2D(1., 0.), Result);
	TestTrue(TEXT("A tie goes to the lexicographically first assignment"), Result.Num() == 2 && Result[0] == 0 && Result[1] == 1);

	// The sum of distances, not of squared distances: here the plain sum is least for pairing 0-2-1 and the sum of
	// squares for 2-1-0.
	AssignSlots(TArray<FVector2D>{ FVector2D(8., 7.), FVector2D(8., 19.), FVector2D(16., 16.) }, TArray<int32>{ 1, 1, 1 },
		TArray<FVector2D>{ FVector2D(13., 1.), FVector2D(15., 10.), FVector2D(0., 1.) }, FVector2D(1., 0.), Result);
	TestTrue(TEXT("The least total distance, not squared distance, decides"),
		Result.Num() == 3 && Result[0] == 0 && Result[1] == 2 && Result[2] == 1);

	AssignSlots(TArray<FVector2D>(), TArray<int32>(), TArray<FVector2D>(), FVector2D(1., 0.), Result);
	TestEqual(TEXT("No units assign nothing"), Result.Num(), 0);

	// More units than the exact search allows fall back to the identity assignment.
	TArray<FVector2D> Positions, Slots;
	TArray<int32> Ranks;
	for (int32 Index = 0; Index < MaxAssigned + 1; ++Index)
	{
		Positions.Add(FVector2D(Index * 10., 0.));
		Slots.Add(FVector2D((MaxAssigned - Index) * 10., 0.));
		Ranks.Add(1);
	}
	AssignSlots(Positions, Ranks, Slots, FVector2D(1., 0.), Result);
	bool bIdentity = Result.Num() == Positions.Num();
	for (int32 Index = 0; bIdentity && Index < Result.Num(); ++Index)
		bIdentity &= Result[Index] == Index;
	TestTrue(TEXT("Too many units keep their own index"), bIdentity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingAssignPropertyTest, "CoopRTS.Rules.ArmyGroup.Heading.AssignProperty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationHeadingAssignPropertyTest::RunTest(const FString& Parameters)
{
	// The search matches an exhaustive reference on random cases, with and without class rows.
	FIndices Result;
	FLcg Random;
	for (int32 Case = 0; Case < 120; ++Case)
	{
		FReference Reference;
		const int32 Count = 1 + Case % 6;
		const bool bRows = Case % 2 == 1;
		Reference.Forward = FVector2D(FMath::Cos(Random.Next(3.)), FMath::Sin(Random.Next(3.)));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Reference.Positions.Add(FVector2D(Random.Next(500.), Random.Next(500.)));
			Reference.Slots.Add(FVector2D(Random.Next(500.), Random.Next(500.)));
			Reference.Ranks.Add(bRows ? static_cast<int32>(FMath::Abs(Random.Next(1.5))) : 1);
		}
		Reference.Current.Init(0, Count);
		Reference.Run(0, 0.);
		AssignSlots(Reference.Positions, Reference.Ranks, Reference.Slots, Reference.Forward, Result);
		if (Result.Num() != Count || FMath::Abs(CostOf(Reference.Positions, Reference.Slots, Result) - Reference.Best) > 1.e-3)
		{
			AddError(FString::Printf(TEXT("Case %d (%d units, rows %d): cost %.3f, exhaustive %.3f"), Case, Count, bRows,
				Result.Num() == Count ? CostOf(Reference.Positions, Reference.Slots, Result) : -1., Reference.Best));
			return true;
		}
		bool bSame = true;
		for (int32 Index = 0; Index < Count; ++Index)
			bSame &= Result[Index] == Reference.Chosen[Index];
		if (!bSame)
		{
			AddError(FString::Printf(TEXT("Case %d: a different assignment of the same cost than the lexicographic first"), Case));
			return true;
		}
		// Without rows the plain-distance optimum never crosses: no pair does better by trading slots.
		for (int32 A = 0; !bRows && A < Count; ++A)
			for (int32 B = A + 1; B < Count; ++B)
				if (FVector2D::Distance(Reference.Positions[A], Reference.Slots[Result[B]]) + FVector2D::Distance(Reference.Positions[B], Reference.Slots[Result[A]])
					< FVector2D::Distance(Reference.Positions[A], Reference.Slots[Result[A]]) + FVector2D::Distance(Reference.Positions[B], Reference.Slots[Result[B]]) - 1.e-6)
				{
					AddError(FString::Printf(TEXT("Case %d: units %d and %d would do better trading slots"), Case, A, B));
					return true;
				}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingRowsTest, "CoopRTS.Rules.ArmyGroup.Heading.Rows",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFormationHeadingRowsTest::RunTest(const FString& Parameters)
{
	// Artillery, ranged and melee standing in the wrong order for a march along +X: the rows put melee first.
	const FVector Centre = At(0., 0.);
	const auto Units = Marchers({ { 200., 0. }, { 100., 0. }, { 0., 0. }, { -100., 0. }, { -200., 0. }, { -300., 0. } }, { 2, 2, 1, 1, 0, 0 });
	const FLegPlan Column = PlanLeg(Produced, {}, Centre, Units, ELegShape::Column, 0.f, 3);
	for (int32 Back = 0; Back < 2; ++Back)
		for (int32 Front = 4; Front < 6; ++Front)
			TestTrue(TEXT("In a column the melee units are ahead of the artillery"), Column.Targets[Front].X > Column.Targets[Back].X);
	for (int32 Middle = 2; Middle < 4; ++Middle)
	{
		TestTrue(TEXT("The ranged units are behind the melee ones"), Column.Targets[Middle].X < Column.Targets[4].X);
		TestTrue(TEXT("The ranged units are ahead of the artillery"), Column.Targets[Middle].X > Column.Targets[0].X);
	}

	// The box keeps the rows too: its front row (largest X along the heading) is melee.
	const FLegPlan Box = PlanLeg(Produced, {}, Centre, Units, ELegShape::Box, 0.f, 3);
	const double FrontMelee = FMath::Min(Box.Targets[4].X, Box.Targets[5].X);
	const double BackArtillery = FMath::Max(Box.Targets[0].X, Box.Targets[1].X);
	TestTrue(TEXT("In a box the melee row is not behind the artillery row"), FrontMelee >= BackArtillery - 1.);

	// Equal classes reduce to the nearest-slot assignment.
	TArray<FMarchUnit, TInlineAllocator<8>> Pair;
	Pair.Add({ FVector2D(120., -50.), 1, 0 });
	Pair.Add({ FVector2D(-110., 50.), 1, 4 });
	const FLegPlan Near = PlanLeg(Produced, {}, Centre, Pair, ELegShape::Box, 0.f, 3);
	TestTrue(TEXT("Units of one class take the slot nearest to them"),
		FVector::Dist2D(Near.Targets[0], Centre + FormationOffset(Produced, 0)) < .01
			&& FVector::Dist2D(Near.Targets[1], Centre + FormationOffset(Produced, 4)) < .01);
	return true;
}
#endif
