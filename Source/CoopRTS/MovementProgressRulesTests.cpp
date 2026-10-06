#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/MovementProgressPolicy.h"

// Pure rule tests: no world, no actors.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementProgressSamplingTest, "CoopRTS.Rules.MovementProgress.Sampling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementProgressEscalationTest, "CoopRTS.Rules.MovementProgress.Escalation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementProgressResetTest, "CoopRTS.Rules.MovementProgress.NewGoal",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementProgressArrivalTest, "CoopRTS.Rules.MovementProgress.FittedArrival",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementProgressOrderTest, "CoopRTS.Rules.MovementProgress.RepeatOrders",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementProgressExemptTest, "CoopRTS.Rules.MovementProgress.Exempt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
using namespace MovementProgressPolicy;

// The force tick: a quarter second.
constexpr float Tick = .25f;

// One unit walking toward Goal, advanced a force tick at a time.
struct FWalker
{
	FUnitProgress Progress;
	FVector Position = FVector(-2000.f, 0.f, 0.f);
	FVector Goal = FVector(2000.f, 0.f, 0.f);
	float Now = 10.f;
	// Cm per second; 0 is a unit pinned in place.
	float Speed = 300.f;
	bool bHasGoal = true;

	EAction Step()
	{
		Now += Tick;
		Position.X += Speed * Tick;
		FSample Sample;
		Sample.Now = Now;
		Sample.Position = Position;
		Sample.Goal = Goal;
		Sample.bHasGoal = bHasGoal;
		return Update(Progress, Sample);
	}
	// Steps for Seconds and counts each action.
	void Run(float Seconds, int32& Repaths, int32& Settles)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - Tick / 2.f; Elapsed += Tick)
		{
			const EAction Action = Step();
			Repaths += Action == EAction::Repath;
			Settles += Action == EAction::Settle;
		}
	}
};
}

bool FMovementProgressSamplingTest::RunTest(const FString& Parameters)
{
	FWalker Walker;
	int32 Repaths = 0, Settles = 0;
	Walker.Run(10.f, Repaths, Settles);
	TestTrue(TEXT("A walking unit is tracked toward its goal"), Walker.Progress.bTracking);
	TestEqual(TEXT("A walking unit never accumulates idle time"), Walker.Progress.IdleSeconds, 0.f);
	TestTrue(TEXT("A walking unit is never re-pathed or settled"), Repaths == 0 && Settles == 0 && !Walker.Progress.bSettled);

	// 24 cm per 1.5 s window is just under the bar; 26 cm is just over it.
	FWalker Crawler;
	Crawler.Speed = 24.f / SampleSeconds;
	Crawler.Run(SampleSeconds + Tick, Repaths, Settles);
	TestEqual(TEXT("A window under MinProgress counts as idle for the window's length"), Crawler.Progress.IdleSeconds, SampleSeconds);
	FWalker Steady;
	Steady.Speed = 26.f / SampleSeconds;
	Steady.Run(SampleSeconds + Tick, Repaths, Settles);
	TestEqual(TEXT("A window at MinProgress or more is not idle"), Steady.Progress.IdleSeconds, 0.f);

	// Six ticks open and fill the first 1.25 s; the window ends on the seventh.
	FWalker Pinned;
	Pinned.Speed = 0.f;
	Pinned.Run(6.f * Tick, Repaths, Settles);
	TestEqual(TEXT("No window ends before SampleSeconds have passed"), Pinned.Progress.IdleSeconds, 0.f);
	Pinned.Run(Tick, Repaths, Settles);
	TestEqual(TEXT("The window ends on the tick that completes SampleSeconds"), Pinned.Progress.IdleSeconds, SampleSeconds);

	// Progress after idling clears the idle time.
	Pinned.Speed = 300.f;
	Pinned.Run(SampleSeconds, Repaths, Settles);
	TestEqual(TEXT("A window with progress clears the idle time"), Pinned.Progress.IdleSeconds, 0.f);

	// A detour around an obstacle may take the unit away from its goal; moving is progress.
	FWalker Detour;
	Detour.Goal = FVector(-4000.f, 0.f, 0.f);
	Detour.Run(3.f * SampleSeconds, Repaths, Settles);
	TestEqual(TEXT("A unit walking away from its goal is not idle"), Detour.Progress.IdleSeconds, 0.f);
	return true;
}

bool FMovementProgressEscalationTest::RunTest(const FString& Parameters)
{
	FWalker Walker;
	Walker.Speed = 0.f;
	int32 Repaths = 0, Settles = 0;
	// The first tick opens the window, so idle windows end at 1.5, 3, 4.5 and 6 s after it.
	Walker.Step();
	Walker.Run(SampleSeconds, Repaths, Settles);
	TestEqual(TEXT("The close-enough radius widens with the first idle window"), CloseEnoughRadius(Walker.Progress.IdleSeconds),
		SampleSeconds * RadiusGrowth);
	TestTrue(TEXT("One idle window neither re-paths nor settles"), Repaths == 0 && Settles == 0);
	Walker.Run(SampleSeconds, Repaths, Settles);
	TestEqual(TEXT("The unit is re-pathed once at RepathIdleSeconds"), Repaths, 1);
	TestEqual(TEXT("A re-path is not a settle"), Settles, 0);
	Walker.Run(SampleSeconds, Repaths, Settles);
	TestTrue(TEXT("Idle time past the re-path neither re-paths again nor settles yet"), Repaths == 1 && Settles == 0);
	TestFalse(TEXT("The unit is not settled before SettleIdleSeconds"), Walker.Progress.bSettled);
	Walker.Run(SampleSeconds, Repaths, Settles);
	TestEqual(TEXT("The unit settles once at SettleIdleSeconds"), Settles, 1);
	TestTrue(TEXT("A settled unit is marked settled"), Walker.Progress.bSettled);
	Walker.Run(10.f, Repaths, Settles);
	TestTrue(TEXT("A settled unit is never re-pathed or settled again"), Repaths == 1 && Settles == 1);

	// Progress before the bound restarts the escalation, and the re-path is spent once per goal.
	FWalker Wobbler;
	int32 WobbleRepaths = 0, WobbleSettles = 0;
	Wobbler.Speed = 0.f;
	Wobbler.Step();
	Wobbler.Run(3.f * SampleSeconds, WobbleRepaths, WobbleSettles);
	TestEqual(TEXT("Idle time accumulates over consecutive idle windows"), Wobbler.Progress.IdleSeconds, 3.f * SampleSeconds);
	Wobbler.Speed = 300.f;
	Wobbler.Run(SampleSeconds, WobbleRepaths, WobbleSettles);
	Wobbler.Speed = 0.f;
	Wobbler.Run(SettleIdleSeconds - SampleSeconds, WobbleRepaths, WobbleSettles);
	TestTrue(TEXT("Progress in between restarts the idle count: the unit is not settled"),
		WobbleSettles == 0 && !Wobbler.Progress.bSettled);
	TestEqual(TEXT("The re-path stays spent after progress"), WobbleRepaths, 1);
	return true;
}

bool FMovementProgressResetTest::RunTest(const FString& Parameters)
{
	int32 Repaths = 0, Settles = 0;
	FWalker Settled;
	Settled.Speed = 0.f;
	Settled.Step();
	Settled.Run(SettleIdleSeconds + SampleSeconds, Repaths, Settles);
	TestTrue(TEXT("The unit settles for its goal"), Settled.Progress.bSettled);

	FWalker Jitter = Settled;
	Jitter.Goal += FVector(GoalChangeTolerance * .5f, 0.f, 0.f);
	Jitter.Step();
	TestTrue(TEXT("A goal that moved less than the tolerance is the same goal"), Jitter.Progress.bSettled);

	FWalker Moved = Settled;
	Moved.Goal += FVector(0.f, GoalChangeTolerance * 2.f, 0.f);
	Moved.Step();
	TestTrue(TEXT("A new goal is tracked afresh"), Moved.Progress.bTracking);
	TestTrue(TEXT("A new goal clears the settled state"), !Moved.Progress.bSettled && !Moved.Progress.bRepathed);
	TestEqual(TEXT("A new goal clears the idle time"), Moved.Progress.IdleSeconds, 0.f);
	TestEqual(TEXT("A new goal opens its window at the current position"), Moved.Progress.WindowStart, Moved.Position);

	FWalker Lost = Settled;
	Lost.bHasGoal = false;
	TestEqual(TEXT("A unit without a goal takes no action"), Lost.Step(), EAction::None);
	TestTrue(TEXT("A unit without a goal is not tracked or settled"), !Lost.Progress.bTracking && !Lost.Progress.bSettled);

	FWalker Arrived = Settled;
	Arrived.Position = Arrived.Goal + FVector(AtGoalRadius * .5f, 0.f, 0.f);
	Arrived.Speed = 0.f;
	Arrived.Step();
	TestTrue(TEXT("A unit at its goal is not tracked or settled"), !Arrived.Progress.bTracking && !Arrived.Progress.bSettled);
	return true;
}

bool FMovementProgressExemptTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("No idle time has no radius"), CloseEnoughRadius(0.f), 0.f);
	TestEqual(TEXT("The radius grows with idle time"), CloseEnoughRadius(2.f), 2.f * RadiusGrowth);
	TestEqual(TEXT("The radius is capped"), CloseEnoughRadius(1000.f), MaxRadius);
	TestTrue(TEXT("The cap is reached before the unit is settled"), CloseEnoughRadius(SettleIdleSeconds) == MaxRadius);

	FUnitProgress Progress;
	Progress.Goal = FVector::ZeroVector;
	const FVector Near(100.f, 0.f, 0.f), Far(1000.f, 0.f, 0.f);
	TestFalse(TEXT("An untracked unit is never exempt"), IsExempt(Progress, FVector::ZeroVector));
	Progress.bTracking = true;
	TestFalse(TEXT("A unit that has not idled is not exempt, so ordinary arrival is unchanged"), IsExempt(Progress, Near));
	Progress.IdleSeconds = 2.f;
	TestTrue(TEXT("An idle unit inside its grown radius is exempt"), IsExempt(Progress, Near));
	TestFalse(TEXT("An idle unit outside its grown radius is not exempt"), IsExempt(Progress, Far));
	Progress.bSettled = true;
	TestTrue(TEXT("A settled unit is exempt wherever it stands"), IsExempt(Progress, Far));
	return true;
}

bool FMovementProgressArrivalTest::RunTest(const FString& Parameters)
{
	// Two members on fitted slots 200 cm apart; the fitted radius is the slot distance from the fitted centre.
	const FVector SlotA(-100.f, 0.f, 0.f), SlotB(100.f, 0.f, 0.f);
	constexpr float Radius = 100.f;
	const auto Judge = [&](const FVector& PositionB, bool bIdleB) {
		const FArrivalMember Members[] = { { SlotA, SlotA, true }, { PositionB, SlotB, bIdleB } };
		return JudgeArrival(Members, Radius);
	};
	TestFalse(TEXT("No members are not gathered"), JudgeArrival({}, Radius).bGathered);
	TestTrue(TEXT("Members on their fitted slots are gathered"), Judge(SlotB, false).bGathered);
	TestTrue(TEXT("Members within 170 cm of their slots are gathered"), Judge(SlotB + FVector(0.f, 150.f, 0.f), false).bGathered);
	TestFalse(TEXT("A member still walking 400 cm short pulls the mean off the slots"),
		Judge(SlotB + FVector(0.f, 400.f, 0.f), false).bGathered);
	TestTrue(TEXT("A fallback member idle 400 cm from its slot stands where it was left"),
		Judge(SlotB + FVector(0.f, 400.f, 0.f), true).bGathered);
	TestTrue(TEXT("The mean then expects it where it stands"),
		Judge(SlotB + FVector(0.f, 400.f, 0.f), true).ExpectedMean.Equals(FVector(0.f, 200.f, 0.f)));
	TestFalse(TEXT("An idle member beyond the fallback radius plus tolerance is not settled"),
		Judge(SlotB + FVector(0.f, 500.f, 0.f), true).bGathered);
	// Slack through the fitted radius: a walking member 250 cm off its slot still counts when it is within the
	// radius plus tolerance of the mean and the mean test holds.
	TestTrue(TEXT("A trailing member within the fitted radius plus tolerance of the mean is gathered"),
		Judge(SlotB + FVector(0.f, 250.f, 0.f), false).bGathered);
	// A fitted layout is judged against its own slots, not the rigid layout: shifting every slot shifts the verdict.
	const FArrivalMember Shifted[] = { { SlotA, SlotA + FVector(0.f, 600.f, 0.f), false }, { SlotB, SlotB + FVector(0.f, 600.f, 0.f), false } };
	TestFalse(TEXT("Members 600 cm from fitted slots they are still walking to are not gathered"),
		JudgeArrival(Shifted, Radius).bGathered);
	return true;
}

bool FMovementProgressOrderTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("A new waypoint is a new waypoint whatever its phase"),
		ClassifyOrder(false, true) == EOrderKind::NewWaypoint && ClassifyOrder(false, false) == EOrderKind::NewWaypoint);
	TestTrue(TEXT("The applied waypoint with the same phase is a repeat"), ClassifyOrder(true, true) == EOrderKind::Repeat);
	TestTrue(TEXT("The applied waypoint with another phase is a phase switch"), ClassifyOrder(true, false) == EOrderKind::PhaseSwitch);

	FOrderClocks Clocks;
	const float Start = 100.f;
	Ordered(EOrderKind::NewWaypoint, Clocks, Start);
	TestFalse(TEXT("An identical order right after is held back"), MayOrder(EOrderKind::Repeat, Clocks, Start + .25f));
	TestTrue(TEXT("An advance to a new waypoint right after a repeat goes out in the same tick"),
		MayOrder(EOrderKind::NewWaypoint, Clocks, Start + .25f));
	Ordered(EOrderKind::Repeat, Clocks, Start + RepeatOrderSeconds);
	TestTrue(TEXT("The new waypoint is not held back by a repeat issued the tick before"),
		MayOrder(EOrderKind::NewWaypoint, Clocks, Start + RepeatOrderSeconds));
	TestFalse(TEXT("Another repeat waits out a fresh interval"), MayOrder(EOrderKind::Repeat, Clocks, Start + RepeatOrderSeconds + 1.f));
	TestTrue(TEXT("A repeat is allowed once the interval has passed"),
		MayOrder(EOrderKind::Repeat, Clocks, Start + 2.f * RepeatOrderSeconds));

	// Arrival: the first phase switch is immediate even right after the waypoint was ordered.
	FOrderClocks Arrival;
	Ordered(EOrderKind::NewWaypoint, Arrival, Start);
	TestTrue(TEXT("The first phase switch on a waypoint is never held back"), MayOrder(EOrderKind::PhaseSwitch, Arrival, Start + .25f));
	Ordered(EOrderKind::PhaseSwitch, Arrival, Start + .25f);
	TestFalse(TEXT("A second switch within the interval waits, so disagreeing callers cannot alternate"),
		MayOrder(EOrderKind::PhaseSwitch, Arrival, Start + .5f));
	Ordered(EOrderKind::Repeat, Arrival, Start + 1.f);
	TestFalse(TEXT("A repeat in between does not release the switch clock"), MayOrder(EOrderKind::PhaseSwitch, Arrival, Start + 1.5f));
	TestTrue(TEXT("A switch is allowed again after the interval"),
		MayOrder(EOrderKind::PhaseSwitch, Arrival, Start + .25f + RepeatOrderSeconds));
	return true;
}
#endif
