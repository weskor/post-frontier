#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/JevExecution.h"
#include "Rules/JevPlanner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevStandingPlanTest, "CoopRTS.Rules.Jev.StandingPlan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevStandingOrderChangeTest, "CoopRTS.Rules.JevExecution.StandingOrderChange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevWaveObjectiveTest, "CoopRTS.Rules.Jev.WaveObjective",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
// JEV's main 0 at the west end, the humans' main 3 at the east end, a line of regions between.
JevPlanner::FWorld LineWorld()
{
	JevPlanner::FWorld World;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		World.Regions[Index].bExists = true;
		World.Regions[Index].Position = FVector(Index * 1000.f, 0.f, 0.f);
		World.Regions[Index].Neighbours = (Index > 0 ? uint64(1) << (Index - 1) : 0)
			| (Index < 3 ? uint64(1) << (Index + 1) : 0);
	}
	World.Regions[0].Controller = 5;
	World.Regions[0].bMain = true;
	World.Regions[3].Controller = 0;
	World.Regions[3].bMain = true;
	World.Home = 0;
	World.EnemyHome = 3;
	return World;
}

// A wave's Attack on the humans' main whose 25 s window has long expired.
JevPlanner::FPlan WavePlan(float Now)
{
	JevPlanner::FPlan Plan;
	Plan.Verb = JevPlanner::EVerb::Attack;
	Plan.Source = 0;
	Plan.Target = 3;
	Plan.EtaSeconds = 56.f;
	Plan.CommittedUntil = Now - 1000.f;
	Plan.bRequiresUnownedTarget = true;
	Plan.bStanding = true;
	return Plan;
}
}

bool FJevStandingPlanTest::RunTest(const FString&)
{
	using namespace JevPlanner;
	const float Speeds[] = { 100.f };
	const float Now = 2000.f;
	FWorld World = LineWorld();
	FForce Force;
	Force.Source = Force.Home = 0;
	Force.UnitCount = 6;
	Force.bCanRefill = false;
	Force.ClassSpeeds = Speeds;
	const FPlan Standing = WavePlan(Now);
	FPlan Out;
	TestTrue(TEXT("An expired standing Attack is kept"), Decide(World, Force, Now, &Standing, Out));
	TestTrue(TEXT("It stays the same Attack on the same target, with its old deadline"),
		Out.Verb == EVerb::Attack && Out.Target == 3 && Out.bStanding && Out.CommittedUntil == Standing.CommittedUntil);

	FPlan Ordinary = Standing;
	Ordinary.bStanding = false;
	Decide(World, Force, Now, &Ordinary, Out);
	TestFalse(TEXT("An ordinary plan past its window is replaced by a fresh choice"), Out.bStanding);
	TestTrue(TEXT("The fresh choice starts a new window"), Out.CommittedUntil == Now + CommitmentSeconds);

	World.Regions[0].bAttacked = true;
	World.Regions[0].Hostiles = 3;
	Decide(World, Force, Now, &Standing, Out);
	TestTrue(TEXT("A standing Attack is kept while its own source region is attacked"), Out.bStanding && Out.Verb == EVerb::Attack);
	World.Regions[0].bAttacked = false;
	World.Regions[0].Hostiles = 0;

	FForce Retreating = Force;
	Retreating.bRetreating = true;
	Decide(World, Retreating, Now, &Standing, Out);
	TestFalse(TEXT("The force's own Retreat ends the standing order"), Out.bStanding);

	FForce Wiped = Force;
	Wiped.UnitCount = 0;
	Wiped.SquadSize = 6;
	Decide(World, Wiped, Now, &Standing, Out);
	TestFalse(TEXT("A force with no units left does not carry the standing order into its refill"), Out.bStanding);

	FWorld Captured = World;
	Captured.Regions[3].Controller = 5;
	Decide(Captured, Force, Now, &Standing, Out);
	TestFalse(TEXT("A captured target ends the standing order"), Out.bStanding);

	FPlan Raid = Standing;
	Raid.Target = 2;
	World.Regions[2].Controller = 5;
	Decide(World, Force, Now, &Raid, Out);
	TestFalse(TEXT("A raid whose target JEV already holds is complete"), Out.bStanding);
	World.Regions[2].Controller = INDEX_NONE;

	FPlan Defence = Standing;
	Defence.Target = 0;
	Defence.bRequiresUnownedTarget = false;
	Decide(World, Force, Now, &Defence, Out);
	TestFalse(TEXT("An emergency wave's defence of a quiet home region is complete"), Out.bStanding);
	World.Regions[0].Hostiles = 2;
	TestTrue(TEXT("While hostiles stand in the home region the defence continues"),
		Decide(World, Force, Now, &Defence, Out) && Out.bStanding && Out.Target == 0);
	return true;
}

bool FJevStandingOrderChangeTest::RunTest(const FString&)
{
	const JevPlanner::FPlan Standing = WavePlan(2000.f);
	TestFalse(TEXT("A standing Attack the force is still executing is not reissued"),
		JevExecution::OrderChange(Standing, &Standing, false).bChanged);
	TestTrue(TEXT("A standing Attack whose order was lost is reissued without a new window"),
		JevExecution::OrderChange(Standing, &Standing, true).bChanged);
	JevPlanner::FPlan Ordinary = Standing;
	Ordinary.bStanding = false;
	TestFalse(TEXT("An ordinary decision that did not change keeps a differing order"),
		JevExecution::OrderChange(Ordinary, &Ordinary, true).bChanged);
	TestFalse(TEXT("Keeping a standing plan is not a new commitment"), JevExecution::NewCommitment(&Standing, Standing));
	return true;
}

bool FJevWaveObjectiveTest::RunTest(const FString&)
{
	using namespace JevPlanner;
	const float Speeds[] = { 100.f };
	FWorld World = LineWorld();
	// The humans' two Failover Nodes stand in regions 1 and 2, outside their main.
	FTarget Nodes[2];
	Nodes[0].Identity = 12;
	Nodes[0].Region = 1;
	Nodes[1].Identity = 11;
	Nodes[1].Region = 2;
	for (FTarget& Node : Nodes)
		Node.bAlive = Node.bNode = true;
	World.Targets = Nodes;
	World.bHostileNodesStand = true;
	FForce Force;
	Force.Source = Force.Home = 0;
	Force.UnitCount = 6;
	Force.ClassSpeeds = Speeds;

	JevExecution::FObjective Objective = JevExecution::WaveObjective(World, Force, 3);
	TestTrue(TEXT("A wave sent at the guarded main goes to the nearest standing node"), Objective.Region == 1 && Objective.Identity == 12);
	Objective = JevExecution::WaveObjective(World, Force, 2);
	TestTrue(TEXT("A wave sent anywhere but the main keeps its target"), Objective.Region == 2 && Objective.Identity == 0);
	Nodes[0].bAlive = false;
	Objective = JevExecution::WaveObjective(World, Force, 3);
	TestTrue(TEXT("When the nearest node has fallen the next one is the objective"), Objective.Region == 2 && Objective.Identity == 11);
	Nodes[1].bAlive = false;
	World.bHostileNodesStand = false;
	Objective = JevExecution::WaveObjective(World, Force, 3);
	TestTrue(TEXT("With every node down the wave goes for the main"), Objective.Region == 3 && Objective.Identity == 0);

	// A plan that attacks a node lasts while the node lives, even in a region JEV holds.
	Nodes[0].bAlive = Nodes[1].bAlive = true;
	World.bHostileNodesStand = true;
	World.Regions[1].Controller = 5;
	FPlan NodePlan;
	NodePlan.Verb = EVerb::Attack;
	NodePlan.Source = 0;
	NodePlan.Target = 1;
	NodePlan.TargetIdentity = 12;
	NodePlan.bStanding = true;
	TestTrue(TEXT("A standing attack on a standing node holds although JEV holds the node's region"), StandingHolds(World, Force, NodePlan));
	Nodes[0].bAlive = false;
	TestFalse(TEXT("It ends when the node falls"), StandingHolds(World, Force, NodePlan));
	Nodes[0].bAlive = true;

	bool bOffered = false;
	const FCandidates Candidates = Propose(World, Force);
	for (int32 Index = 0; Index < Candidates.Count; ++Index)
		bOffered |= Candidates.Values[Index].Plan.TargetIdentity == 12;
	TestTrue(TEXT("The planner offers a hostile node standing in a region JEV holds"), bOffered);
	return true;
}

#endif
