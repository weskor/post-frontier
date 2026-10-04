#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/HqHoldPolicy.h"
#include "Rules/JevPlanner.h"

// Pure rule tests: no world, no actors. The hold's progress, pause, decay and revival, the HQ's immunity
// while a node stands, the plating window, the once-per-side emergency claim and the lifecycle.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHqHoldProgressTest, "CoopRTS.Rules.HqHold.Progress",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHqHoldPresenceTest, "CoopRTS.Rules.HqHold.Presence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHqHoldRevivalTest, "CoopRTS.Rules.HqHold.Revival",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHqHoldCompletionTest, "CoopRTS.Rules.HqHold.Completion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHqHoldGuardTest, "CoopRTS.Rules.HqHold.Guard",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHqHoldJevPlanningTest, "CoopRTS.Rules.HqHold.JevPlanning",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHqHoldJevDefenceTest, "CoopRTS.Rules.HqHold.JevDefence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
using namespace HqHoldPolicy;

constexpr FPresence AttackersOnly{ 2, 0 };
constexpr FPresence DefendersToo{ 2, 1 };
constexpr FPresence DefendersOnly{ 0, 3 };
constexpr FPresence Nobody{ 0, 0 };

FStep Offline(const FHold& Hold, const FPresence& Presence, float Seconds)
{
	return Advance(EPhase::Offline, Hold, Presence, Seconds);
}
}

bool FHqHoldProgressTest::RunTest(const FString& Parameters)
{
	// 1/75 of the hold per second: 15 s of attackers is a fifth.
	const FStep Step = Offline({}, AttackersOnly, 15.f);
	TestEqual(TEXT("Attackers alone add their seconds"), Step.Hold.Progress, 15.f);
	TestEqual(TEXT("A fifth of the hold"), Fraction(Step.Hold), .2f);
	TestEqual(TEXT("State is holding"), static_cast<int32>(Step.State), static_cast<int32>(EHoldState::Holding));
	TestTrue(TEXT("Progress existed"), Step.Hold.bStarted);
	TestEqual(TEXT("Timer text shows whole seconds"), WholeSeconds(Offline({ 38.9f, true }, DefendersToo, 0.f).Hold), 38);
	// Small steps add up exactly like one big one.
	FHold Hold;
	for (int32 Tick = 0; Tick < 30; ++Tick)
		Hold = Offline(Hold, AttackersOnly, .5f).Hold;
	TestEqual(TEXT("Thirty half-second steps equal fifteen seconds"), Hold.Progress, 15.f);
	TestEqual(TEXT("An online HQ has no hold"), static_cast<int32>(Advance(EPhase::Online, {}, AttackersOnly, 10.f).State),
		static_cast<int32>(EHoldState::None));
	TestEqual(TEXT("An online HQ does not advance"), Advance(EPhase::Online, {}, AttackersOnly, 10.f).Hold.Progress, 0.f);
	return true;
}

bool FHqHoldPresenceTest::RunTest(const FString& Parameters)
{
	const FHold Held{ 30.f, true };
	const FStep Paused = Offline(Held, DefendersToo, 10.f);
	TestEqual(TEXT("Any defender pauses even against attackers"), Paused.Hold.Progress, 30.f);
	TestEqual(TEXT("State is paused"), static_cast<int32>(Paused.State), static_cast<int32>(EHoldState::Paused));
	TestEqual(TEXT("Defenders alone also pause"), Offline(Held, DefendersOnly, 10.f).Hold.Progress, 30.f);
	const FStep Decaying = Offline(Held, Nobody, 10.f);
	TestEqual(TEXT("Nobody present decays at the growth rate"), Decaying.Hold.Progress, 20.f);
	TestEqual(TEXT("State is decaying"), static_cast<int32>(Decaying.State), static_cast<int32>(EHoldState::Decaying));
	TestEqual(TEXT("Growth then decay returns to the start"),
		Offline(Offline({}, AttackersOnly, 12.f).Hold, Nobody, 12.f).Hold.Progress, 0.f);
	return true;
}

bool FHqHoldRevivalTest::RunTest(const FString& Parameters)
{
	const FStep Revived = Offline({ 5.f, true }, Nobody, 5.f);
	TestTrue(TEXT("Decay to zero after progress existed brings the HQ back"), Revived.bRevived);
	TestEqual(TEXT("It is online"), static_cast<int32>(Revived.Phase), static_cast<int32>(EPhase::Online));
	TestEqual(TEXT("It starts a fresh hold"), Revived.Hold.Progress, 0.f);
	TestFalse(TEXT("The fresh hold has not started"), Revived.Hold.bStarted);
	TestTrue(TEXT("Overshooting zero revives too"), Offline({ 5.f, true }, Nobody, 9.f).bRevived);
	TestFalse(TEXT("Defenders keep it offline at any progress"), Offline({ 5.f, true }, DefendersOnly, 60.f).bRevived);
	// An HQ nobody has attacked since it went offline stays offline: zero progress never existed.
	const FStep Idle = Offline({}, Nobody, 30.f);
	TestFalse(TEXT("Zero progress that never existed does not revive"), Idle.bRevived);
	TestEqual(TEXT("It stays offline"), static_cast<int32>(Idle.Phase), static_cast<int32>(EPhase::Offline));
	// Attackers present at zero progress with no time passing still start nothing.
	TestFalse(TEXT("A zero-length step starts nothing"), Offline({}, AttackersOnly, 0.f).Hold.bStarted);
	// The fresh hold after a revival needs a new attack to count as started.
	TestEqual(TEXT("RestoredHealth is a quarter of the maximum"), RestoredHealth(900), 225);
	TestEqual(TEXT("A tiny maximum still restores one point"), RestoredHealth(1), 1);
	return true;
}

bool FHqHoldCompletionTest::RunTest(const FString& Parameters)
{
	FStep Step;
	Step.Phase = EPhase::Offline;
	int32 Seconds = 0;
	while (Step.Phase == EPhase::Offline && Seconds < 200)
	{
		Step = Offline(Step.Hold, AttackersOnly, 1.f);
		++Seconds;
	}
	TestEqual(TEXT("Seventy-five seconds of uninterrupted attackers complete the hold"), Seconds, 75);
	TestTrue(TEXT("The completion is reported once"), Step.bCompleted);
	TestEqual(TEXT("The HQ is lost"), static_cast<int32>(Step.Phase), static_cast<int32>(EPhase::Lost));
	TestEqual(TEXT("Progress is full"), Fraction(Step.Hold), 1.f);
	const FStep After = Advance(EPhase::Lost, Step.Hold, AttackersOnly, 10.f);
	TestFalse(TEXT("A lost HQ completes nothing again"), After.bCompleted);
	TestEqual(TEXT("A lost HQ stays lost"), static_cast<int32>(After.Phase), static_cast<int32>(EPhase::Lost));
	// An interruption costs time: a defender for 10 s pauses, so the hold needs 10 more seconds of calendar time.
	FHold Hold = Offline({}, AttackersOnly, 40.f).Hold;
	Hold = Offline(Hold, DefendersToo, 10.f).Hold;
	TestFalse(TEXT("Paused progress does not complete"), Offline(Hold, AttackersOnly, 34.f).bCompleted);
	TestTrue(TEXT("It completes after the remaining 35 s"), Offline(Hold, AttackersOnly, 35.f).bCompleted);
	return true;
}

bool FHqHoldGuardTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("An online HQ is immune while either node stands"), HqImmune(EPhase::Online, 2) && HqImmune(EPhase::Online, 1));
	TestFalse(TEXT("Both nodes down makes it damageable"), HqImmune(EPhase::Online, 0));
	TestFalse(TEXT("An offline HQ has no immunity to show"), HqImmune(EPhase::Offline, 2));
	TestTrue(TEXT("Plating holds before v1.2"), NodePlated(0.f) && NodePlated(239.9f));
	TestFalse(TEXT("Plating ends at v1.2 (240 s)"), NodePlated(240.f));
	TestTrue(TEXT("Plating covers the planning phase"), NodePlated(-5.f));
	TestEqual(TEXT("Plating takes 90% of the damage"), PlatingIncomingMultiplier, .1f);
	TestEqual(TEXT("Each HQ has two nodes of 1000 HP"), NodesPerHq * NodeHealth, 2000);
	bool bHumans = false;
	bool bJev = false;
	TestTrue(TEXT("The first offline transition of a side claims its emergency force"), ClaimEmergency(bHumans));
	TestFalse(TEXT("A second offline transition of the same side does not"), ClaimEmergency(bHumans));
	TestTrue(TEXT("The other side's claim is independent"), ClaimEmergency(bJev));
	TestFalse(TEXT("And is once as well"), ClaimEmergency(bJev));
	return true;
}

namespace
{
// JEV's main 0 touches the two regions 1 and 2, equally far; 3 is the players' main.
struct FJevFixture
{
	FJevFixture()
	{
		for (int32 Index = 0; Index < 4; ++Index)
			World.Regions[Index].bExists = true;
		World.Regions[0].Neighbours = (uint64(1) << 1) | (uint64(1) << 2);
		World.Regions[1].Neighbours = (uint64(1) << 0) | (uint64(1) << 3);
		World.Regions[2].Neighbours = (uint64(1) << 0) | (uint64(1) << 3);
		World.Regions[3].Neighbours = (uint64(1) << 1) | (uint64(1) << 2);
		World.Regions[1].Position = FVector(1000.f, 1000.f, 0.f);
		World.Regions[2].Position = FVector(1000.f, -1000.f, 0.f);
		World.Regions[3].Position = FVector(2000.f, 0.f, 0.f);
		World.Regions[0].Controller = 5;
		World.Regions[0].bMain = true;
		World.Regions[3].Controller = 0;
		World.Regions[3].bMain = true;
		World.Regions[1].Controller = World.Regions[2].Controller = 0;
		World.Home = 0;
		World.EnemyHome = 3;
		Force.Source = Force.Home = 0;
		Force.UnitCount = 5;
		Force.ClassSpeeds = Speeds;
	}
	FJevFixture(const FJevFixture&) = delete;
	FJevFixture& operator=(const FJevFixture&) = delete;

	JevPlanner::FPlan Pick() const
	{
		const JevPlanner::FCandidates All = JevPlanner::Propose(World, Force);
		const JevPlanner::FCandidate* Best = JevPlanner::Choose(All);
		return Best ? Best->Plan : JevPlanner::FPlan();
	}

	JevPlanner::FWorld World;
	JevPlanner::FForce Force;
	float Speeds[1] = { 200.f };
};
}

bool FHqHoldJevPlanningTest::RunTest(const FString& Parameters)
{
	using namespace JevPlanner;
	FJevFixture Fixture;
	FWorld& World = Fixture.World;
	// A hostile Failover Node outranks another structure at the same distance.
	FTarget Targets[] = { { 11, 1, true, false }, { 22, 2, true, true } };
	World.Targets = Targets;
	FPlan Chosen = Fixture.Pick();
	TestTrue(TEXT("A hostile node is attacked before an equal structure"), Chosen.Target == 2 && Chosen.TargetIdentity == 22);
	Targets[1].bNode = false;
	TestEqual(TEXT("Without the node bonus the lower index wins the tie"), Fixture.Pick().Target, 1);
	World.Targets = {};

	// The nodes come before the HQ: an advantaged JEV does not push into the main while one stands.
	World.bAdvantage = true;
	TestEqual(TEXT("An unthreatened JEV with the advantage pushes the main"), Fixture.Pick().Target, 3);
	World.bHostileNodesStand = true;
	TestTrue(TEXT("While a hostile node stands it does not"), Fixture.Pick().Target != 3);
	// An offline hostile HQ makes its main the objective even without the advantage.
	World.bAdvantage = false;
	World.bHostileNodesStand = false;
	World.bHostileHqOffline = true;
	TestEqual(TEXT("An offline hostile HQ makes its main the objective"), Fixture.Pick().Target, 3);
	return true;
}

bool FHqHoldJevDefenceTest::RunTest(const FString& Parameters)
{
	using namespace JevPlanner;
	FJevFixture Fixture;
	FWorld& World = Fixture.World;
	World.Regions[1].Hostiles = World.Regions[2].Hostiles = 3;
	World.Regions[1].Controller = World.Regions[2].Controller = 5;
	TestEqual(TEXT("Equal threatened regions tie on the lower index"), Fixture.Pick().Target, 1);
	World.Regions[2].OwnNodes = 1;
	const FPlan Defence = Fixture.Pick();
	TestTrue(TEXT("The region with its own node is defended first"), Defence.Target == 2 && Defence.Verb == EVerb::MoveAndHold);
	// Even where it does not hold the region, JEV fights for the node.
	World.Regions[1].Controller = World.Regions[2].Controller = 0;
	const FPlan Contest = Fixture.Pick();
	TestTrue(TEXT("A node in a hostile-held region is contested with an Attack"),
		Contest.Target == 2 && Contest.Verb == EVerb::Attack);
	return true;
}

#endif
