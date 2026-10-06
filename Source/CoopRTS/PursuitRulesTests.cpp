#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/PursuitPolicy.h"

// Pure rule tests: no world, no actors. Values are arbitrary; assertions are invariants.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPursuitTransitionsTest, "CoopRTS.Rules.Combat.PursuitTransitions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPursuitTransitionsTest::RunTest(const FString& Parameters)
{
	const FVector Origin = FVector::ZeroVector;
	constexpr float ArrivalAllowance = 34.f + 35.f;
	for (const float Range : { 175.f, 560.f, 1150.f })
	{
		const auto Decide = [&](float Distance, bool bPursuing, bool bChanged, const FVector& LastGoal, bool bRetryReady = true) {
			return PursuitPolicy::Evaluate(Origin, FVector(Distance, 0.f, 0.f), Range, ArrivalAllowance,
				Origin, 2000.f, 0.f, bPursuing, bChanged, LastGoal, bRetryReady);
		};
		const FPursuitDecision Firing = Decide(Range, false, false, Origin);
		TestTrue(TEXT("Exact weapon boundary fires"), Firing.bInRange);
		TestFalse(TEXT("In-range unit does not issue pursuit"), Firing.bIssueMove);
		TestTrue(TEXT("An idle in-range unit holds still"), Firing.bStop);
		for (const float Distance : { Range + 1.f, Range + 25.f, Range + 129.f })
		{
			const FPursuitDecision Leaving = Decide(Distance, false, false, Origin);
			TestTrue(TEXT("Leaving firing range always starts pursuit"), Leaving.bIssueMove);
			TestFalse(TEXT("A unit out of range does not stop"), Leaving.bStop);
			TestTrue(TEXT("Pursuit endpoint is inside weapon range"),
				FVector::Dist2D(Leaving.Goal, FVector(Distance, 0.f, 0.f)) < Range);
			TestTrue(TEXT("Endpoint reserves the arrival allowance inside weapon range"),
				FVector::Dist2D(Leaving.Goal, FVector(Distance, 0.f, 0.f)) + ArrivalAllowance <= Range + .0001f);
			TestTrue(TEXT("Endpoint stands inside the stop band"),
				FVector::Dist2D(Leaving.Goal, FVector(Distance, 0.f, 0.f)) <= Range * PursuitPolicy::StopBandFraction + .0001f);
			TestFalse(TEXT("Stationary target does not reissue an accepted move"),
				Decide(Distance, true, false, Leaving.Goal).bIssueMove);
			TestTrue(TEXT("A finished path outside range must resume even with an unchanged endpoint"),
				Decide(Distance, false, false, Leaving.Goal).bIssueMove);
			TestFalse(TEXT("Idle stationary pursuit waits for retry cooldown"),
				Decide(Distance, false, false, Leaving.Goal, false).bIssueMove);
			TestTrue(TEXT("Target switch moves immediately even during retry cooldown"),
				Decide(Distance + 10.f, false, true, Leaving.Goal, false).bIssueMove);
			TestFalse(TEXT("Failed path to a stationary target is also rate limited"),
				Decide(Distance, false, false, Origin, false).bIssueMove);
			TestTrue(TEXT("Switch to a nearby target bypasses prior goal hysteresis"),
				Decide(Distance + 10.f, true, true, Leaving.Goal).bIssueMove);
			TestFalse(TEXT("Target drift exactly at 130 cm does not reissue"),
				Decide(Distance + 130.f, true, false, Leaving.Goal).bIssueMove);
			TestTrue(TEXT("Target drift above 130 cm reissues"),
				Decide(Distance + 131.f, true, false, Leaving.Goal).bIssueMove);
		}
	}
	const FPursuitDecision LargerCapsule = PursuitPolicy::Evaluate(Origin, FVector(200.f, 0.f, 0.f),
		175.f, 80.f + 35.f, Origin, 2000.f, 0.f, false, false, Origin, true);
	TestEqual(TEXT("Larger capsule reserves its own full allowance"), LargerCapsule.Goal.X, 140.);
	const FPursuitDecision Clamped = PursuitPolicy::Evaluate(Origin, FVector(1000.f, 0.f, 0.f),
		175.f, 69.f, Origin, 500.f, 7.f, false, false, Origin, true);
	TestTrue(TEXT("Pursuit goal stays inside order leash"), FMath::IsNearlyEqual(Clamped.Goal.Size2D(), 500.));
	TestEqual(TEXT("Pursuit goal keeps order height"), Clamped.Goal.Z, 7.);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPursuitStopBandTest, "CoopRTS.Rules.Combat.PursuitStopBand",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPursuitStopBandTest::RunTest(const FString& Parameters)
{
	// Against a structure a unit walking in fires from the edge of its range but keeps walking to the
	// stop band; one that is idle in range stays put. A crowd nudge across the range edge therefore
	// cannot start a pursuit that the stop band would end a tick later.
	const FVector Origin = FVector::ZeroVector;
	const CombatRangePolicy::FRangeTarget Workshop = CombatRangePolicy::Box(Origin, FVector2D(145., 145.), 0.f, 34.f);
	for (const float Range : { 175.f, 300.f, 560.f, 1150.f })
	{
		const float Band = Range * PursuitPolicy::StopBandFraction;
		const auto Decide = [&](double Edge, bool bPursuing) {
			const FVector Unit(145. + 34. + Edge, 0., 0.);
			return PursuitPolicy::Evaluate(Unit, Workshop, Range, PursuitPolicy::ArrivalTolerance, Unit, 5000.f, 0.f, bPursuing, false, Origin, true);
		};
		const FPursuitDecision Entering = Decide(Range - 1., true);
		TestTrue(TEXT("A walking unit fires as soon as it is in range"), Entering.bInRange);
		TestFalse(TEXT("A walking unit above the stop band keeps walking"), Entering.bStop);
		TestFalse(TEXT("A walking unit in range issues no new move"), Entering.bIssueMove);
		TestTrue(TEXT("A walking unit inside the stop band stops"), Decide(Band, true).bStop);
		TestTrue(TEXT("An idle unit anywhere in range holds still"), Decide(Range - 1., false).bStop);
		TestFalse(TEXT("An idle unit in range never issues a move"), Decide(Range - 1., false).bIssueMove);
		TestFalse(TEXT("An idle unit just out of range is not stopped"), Decide(Range + 1., false).bStop);
		// Against a unit the approach is the balanced one: stop the moment it is in range.
		const FPursuitDecision VsUnit = PursuitPolicy::Evaluate(FVector(Range - 1., 0., 0.), CombatRangePolicy::FRangeTarget(Origin), Range,
			PursuitPolicy::ArrivalAllowance(CombatRangePolicy::FRangeTarget(Origin), 34.f), Origin, 5000.f, 0.f, true, false, Origin, true);
		TestTrue(TEXT("A walking unit stops as soon as a unit target is in range"), VsUnit.bInRange && VsUnit.bStop);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPursuitOpenStructureTest, "CoopRTS.Rules.Combat.PursuitOpenStructure",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPursuitOpenStructureTest::RunTest(const FString& Parameters)
{
	// An HQ or Failover Node (150 cm half size) can be walked into: range runs to its edge and members spread along
	// it, but the approach is the balanced one against a unit: stop the moment it is in range, stand at 0.82 x range.
	const FVector Origin = FVector::ZeroVector;
	const FVector Far(1500.f, 0.f, 0.f);
	const CombatRangePolicy::FRangeTarget Node = CombatRangePolicy::Box(Origin, FVector2D(150., 150.), 0.f, 34.f, false);
	const float Allowance = PursuitPolicy::ArrivalAllowance(Node, 34.f);
	TestEqual(TEXT("A walk-in structure reserves the capsule like a unit target"), Allowance, 34.f + PursuitPolicy::ArrivalTolerance);
	const FVector InRange(150.f + 34.f + 500.f, 0.f, 0.f);
	const FPursuitDecision Entering = PursuitPolicy::Evaluate(InRange, Node, 550.f, Allowance, InRange, 5000.f, 0.f, true, false, Origin, true);
	TestTrue(TEXT("A walking unit in range of an open structure stops at once"), Entering.bInRange && Entering.bStop);
	const FVector Goal = PursuitPolicy::ApproachGoal(Far, Node, 550.f, Allowance, Far, 5000.f, 0.f, 0);
	TestEqual(TEXT("Its goal stands 0.82 x range from the edge, capped by the arrival allowance"),
		CombatRangePolicy::EdgeDistance(FVector2D(Goal), Node), static_cast<double>(FMath::Min(550.f * PursuitPolicy::UnitStandoffFraction, 550.f - Allowance)), 1e-6);
	const TArray<FVector2D> Taken = { FVector2D(Goal) };
	const FVector Next = PursuitPolicy::ApproachGoal(Far, Node, 550.f, Allowance, Far, 5000.f, 0.f, 0, Taken);
	TestTrue(TEXT("Members spread along an open structure"), FVector::Dist2D(Next, Goal) >= PursuitPolicy::SlotSpacing - .001f);
	TestEqual(TEXT("at the same standoff"), CombatRangePolicy::EdgeDistance(FVector2D(Next), Node), CombatRangePolicy::EdgeDistance(FVector2D(Goal), Node), 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPursuitStructureApproachTest, "CoopRTS.Rules.Combat.PursuitStructureApproach",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPursuitStructureApproachTest::RunTest(const FString& Parameters)
{
	constexpr float Capsule = 34.f;
	const FVector Origin = FVector::ZeroVector;
	// A melee unit reaches a Workshop (half size 145) only along its edge: the old centre distance
	// of 175 lies inside the building's navigation cutout.
	const CombatRangePolicy::FRangeTarget Workshop = CombatRangePolicy::Box(Origin, FVector2D(145., 145.), 0.f, Capsule);
	const FVector Far(1000.f, 0.f, 0.f);
	const FPursuitDecision Approach = PursuitPolicy::Evaluate(Far, Workshop, 175.f, PursuitPolicy::ArrivalTolerance, Far, 5000.f, 0.f,
		false, false, Far, true);
	TestFalse(TEXT("A melee unit far from a Workshop is out of range"), Approach.bInRange);
	TestTrue(TEXT("It starts to approach"), Approach.bIssueMove);
	TestEqual(TEXT("The goal stands at the arrival-reserved standoff from the Workshop edge"),
		CombatRangePolicy::EdgeDistance(FVector2D(Approach.Goal), Workshop), static_cast<double>(PursuitPolicy::Standoff(Workshop, 175.f, PursuitPolicy::ArrivalTolerance)));
	TestTrue(TEXT("That goal is outside the footprint plus the capsule, on the unit's side"), Approach.Goal.X >= 145. + Capsule);
	const FVector AtCutout(145.f + Capsule + 60.f, 0.f, 0.f);
	TestTrue(TEXT("A melee unit 60 cm off the Workshop's cutout is in range"),
		PursuitPolicy::Evaluate(AtCutout, Workshop, 175.f, PursuitPolicy::ArrivalTolerance, AtCutout, 5000.f, 0.f, false, false, AtCutout, true).bInRange);
	// A unit circling the structure at the same standoff keeps its move: only distance matters.
	const FVector Circled(0.f, 145.f + Capsule + 140.f, 0.f);
	const FPursuitDecision Held = PursuitPolicy::Evaluate(Far, Workshop, 175.f + 300.f, PursuitPolicy::ArrivalTolerance, Far, 5000.f, 0.f,
		true, false, PursuitPolicy::ApproachGoal(Circled, Workshop, 475.f, PursuitPolicy::ArrivalTolerance, Far, 5000.f, 0.f, 0), true);
	TestFalse(TEXT("A goal on another side of the structure at the same standoff is kept"), Held.bIssueMove);

	// Fallback goals walk toward the target, ending at its edge, and the leash clips each.
	double Previous = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < PursuitPolicy::MaxApproachGoals; ++Index)
	{
		const FVector Goal = PursuitPolicy::ApproachGoal(Far, Workshop, 560.f, PursuitPolicy::ArrivalTolerance, Far, 5000.f, 0.f, Index);
		const double Edge = CombatRangePolicy::EdgeDistance(FVector2D(Goal), Workshop);
		TestTrue(TEXT("Each fallback goal is closer to the target than the previous"), Edge < Previous);
		TestTrue(TEXT("Every fallback goal is within weapon range"), Edge <= 560. - PursuitPolicy::ArrivalTolerance + .001);
		Previous = Edge;
	}
	TestTrue(TEXT("The last fallback goal stands at the target's edge"), Previous < .001);
	const FVector Clipped = PursuitPolicy::ApproachGoal(Far, Workshop, 560.f, PursuitPolicy::ArrivalTolerance, Far, 200.f, 9.f, 0);
	TestTrue(TEXT("Approach goals stop at the order leash"), FMath::IsNearlyEqual(FVector::Dist2D(Clipped, Far), 200., .01));
	TestEqual(TEXT("Approach goals keep the order height"), Clipped.Z, 9.);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPursuitSpreadTest, "CoopRTS.Rules.Combat.PursuitSpread",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPursuitSpreadTest::RunTest(const FString& Parameters)
{
	constexpr float Capsule = 34.f;
	const FVector Origin = FVector::ZeroVector;
	const FVector Far(1000.f, 0.f, 0.f);
	const CombatRangePolicy::FRangeTarget Workshop = CombatRangePolicy::Box(Origin, FVector2D(145., 145.), 0.f, Capsule);
	// Members of a force take distinct places along the edge, all at the same standoff.
	for (const float HalfSize : { 95.f, 145.f, 150.f })
	{
		const CombatRangePolicy::FRangeTarget Structure = CombatRangePolicy::Box(Origin, FVector2D(HalfSize, HalfSize), 0.f, Capsule);
		const double Wanted = PursuitPolicy::Standoff(Structure, 175.f, PursuitPolicy::ArrivalTolerance);
		TArray<FVector2D> Taken;
		for (int32 Member = 0; Member < 6; ++Member)
		{
			const FVector Goal = PursuitPolicy::ApproachGoal(Far, Structure, 175.f, PursuitPolicy::ArrivalTolerance, Far, 5000.f, 0.f, 0, Taken);
			TestEqual(TEXT("A shifted goal keeps the standoff"), CombatRangePolicy::EdgeDistance(FVector2D(Goal), Structure), Wanted, 1e-6);
			for (const FVector2D& Other : Taken)
				TestTrue(TEXT("Each member's goal is a slot spacing clear of the others'"),
					FVector2D::Distance(FVector2D(Goal), Other) >= PursuitPolicy::SlotSpacing - .001f);
			Taken.Add(FVector2D(Goal));
		}
	}
	// The slide goes to the side of the order's anchor, so the force stays centred on it.
	const FVector Natural = PursuitPolicy::ApproachGoal(Far, Workshop, 175.f, PursuitPolicy::ArrivalTolerance, Far, 5000.f, 0.f, 0);
	const TArray<FVector2D> NaturalTaken = { FVector2D(Natural) };
	for (const float AnchorSide : { -400.f, 400.f })
	{
		const FVector Anchor(1000.f, AnchorSide, 0.f);
		const FVector Next = PursuitPolicy::ApproachGoal(Far, Workshop, 175.f, PursuitPolicy::ArrivalTolerance, Anchor, 5000.f, 0.f, 0, NaturalTaken);
		TestTrue(TEXT("The second member's goal is on the anchor's side of the first"), (Next.Y - Natural.Y) * AnchorSide > 0.);
	}
	const FVector Alone = PursuitPolicy::ApproachGoal(Far, Workshop, 175.f, PursuitPolicy::ArrivalTolerance, Far, 5000.f, 0.f, 0);
	TArray<FVector2D> Blocked;
	for (int32 Step = 0; Step < 2 * PursuitPolicy::MaxSlotShifts; ++Step)
	{
		const FVector Goal = PursuitPolicy::ApproachGoal(Far, Workshop, 175.f, PursuitPolicy::ArrivalTolerance, Far, 5000.f, 0.f, 0, Blocked);
		Blocked.Add(FVector2D(Goal));
	}
	// A unit target is approached head on whoever stands there already.
	const CombatRangePolicy::FRangeTarget Enemy{ FVector(Origin) };
	const FVector Head = PursuitPolicy::ApproachGoal(Far, Enemy, 550.f, 69.f, Far, 5000.f, 0.f, 0);
	const TArray<FVector2D> Crowd = { FVector2D(Head) };
	TestEqual(TEXT("Members chasing a unit do not spread"), PursuitPolicy::ApproachGoal(Far, Enemy, 550.f, 69.f, Far, 5000.f, 0.f, 0, Crowd), Head);
	TestEqual(TEXT("With every shift taken the goal falls back to the nearest point"),
		PursuitPolicy::ApproachGoal(Far, Workshop, 175.f, PursuitPolicy::ArrivalTolerance, Far, 5000.f, 0.f, 0, Blocked), Alone);
	return true;
}

#endif
