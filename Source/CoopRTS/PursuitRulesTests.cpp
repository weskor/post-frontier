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
		const FPursuitDecision Firing = Decide(Range, true, false, Origin);
		TestTrue(TEXT("Exact weapon boundary fires"), Firing.bInRange);
		TestFalse(TEXT("In-range unit does not issue pursuit"), Firing.bIssueMove);
		// All bands cross the former hysteresis window; for 1150 the old window
		// ends before weapon range, but first out-of-range movement still matters.
		for (const float Distance : { Range + 1.f, Range + 25.f, FMath::Max(Range + 1.f, .82f * Range + 129.f) })
		{
			const FPursuitDecision Leaving = Decide(Distance, false, false, Origin);
			TestTrue(TEXT("Leaving firing range always starts pursuit"), Leaving.bIssueMove);
			TestTrue(TEXT("Pursuit endpoint is inside weapon range"),
				FVector::Dist2D(Leaving.Goal, FVector(Distance, 0.f, 0.f)) < Range);
			TestTrue(TEXT("Endpoint reserves full capsule plus move tolerance inside weapon range"),
				FVector::Dist2D(Leaving.Goal, FVector(Distance, 0.f, 0.f)) + ArrivalAllowance <= Range + .0001f);
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
		175.f, ArrivalAllowance, Origin, 500.f, 7.f, false, false, Origin, true);
	TestTrue(TEXT("Pursuit goal stays inside order leash"), FMath::IsNearlyEqual(Clamped.Goal.Size2D(), 500.));
	TestEqual(TEXT("Pursuit goal keeps order height"), Clamped.Goal.Z, 7.);
	return true;
}

#endif
