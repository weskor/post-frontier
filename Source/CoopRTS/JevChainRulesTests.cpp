#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/JevPlanner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevChainConnectedTest, "CoopRTS.Rules.JevChain.Connected",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevChainReconnectTest, "CoopRTS.Rules.JevChain.Reconnect",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevChainHoldTest, "CoopRTS.Rules.JevChain.Hold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevChainRigRegionTest, "CoopRTS.Rules.JevChain.RigRegion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
constexpr uint64 Bit(int32 Index) { return uint64(1) << Index; }

// A line 0 - 1 - 2 - 3 with JEV's main at 0 and the humans' main far away.
JevPlanner::FWorld ChainLine()
{
	JevPlanner::FWorld World;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		World.Regions[Index].bExists = true;
		World.Regions[Index].Position = FVector(Index * 1000.f, 0.f, 0.f);
		World.Regions[Index].Neighbours = (Index > 0 ? Bit(Index - 1) : 0) | (Index < 3 ? Bit(Index + 1) : 0);
	}
	World.Regions[0].Controller = 5;
	World.Regions[0].bMain = true;
	World.Home = 0;
	return World;
}

// The chosen plan, or one with no target when nothing is proposed.
JevPlanner::FPlan BestPlan(const JevPlanner::FWorld& World, const JevPlanner::FForce& Force)
{
	const JevPlanner::FCandidates Candidates = JevPlanner::Propose(World, Force);
	const JevPlanner::FCandidate* Best = JevPlanner::Choose(Candidates);
	JevPlanner::FPlan Plan;
	Plan.Target = INDEX_NONE;
	if (Best)
		Plan = Best->Plan;
	return Plan;
}

JevPlanner::FForce ForceAt(const JevPlanner::FWorld& World, int32 Region, TConstArrayView<float> Speeds)
{
	JevPlanner::FForce Force;
	Force.Source = Region;
	Force.Home = 0;
	Force.UnitCount = 6;
	Force.Position = World.Regions[Region].Position;
	Force.ClassSpeeds = Speeds;
	return Force;
}
}

bool FJevChainConnectedTest::RunTest(const FString&)
{
	JevPlanner::FWorld World = ChainLine();
	World.Regions[2].Controller = World.Regions[3].Controller = 5;
	World.Regions[1].Controller = 0;
	TestEqual(TEXT("A hostile region between breaks the chain"), JevPlanner::ConnectedRegions(World), Bit(0));
	World.Regions[1].Controller = 5;
	TestEqual(TEXT("Control all the way keeps every region connected"), JevPlanner::ConnectedRegions(World), Bit(0) | Bit(1) | Bit(2) | Bit(3));
	World.Regions[1].Controller = INDEX_NONE;
	TestEqual(TEXT("A neutral region between breaks the chain too"), JevPlanner::ConnectedRegions(World), Bit(0));
	World.Regions[0].Controller = INDEX_NONE;
	TestEqual(TEXT("The main counts as controlled while it exists"), JevPlanner::ConnectedRegions(World), Bit(0));
	World.Home = INDEX_NONE;
	TestEqual(TEXT("Without a main nothing is known to be connected"), JevPlanner::ConnectedRegions(World), uint64(0));
	return true;
}

bool FJevChainReconnectTest::RunTest(const FString&)
{
	using namespace JevPlanner;
	const float Speeds[] = { 400.f };
	// Region 1 (humans') cuts JEV's Drill Rig in region 2 off from the main. Region 3 holds a
	// far richer deposit, but only region 2 touches it.
	FWorld World = ChainLine();
	World.Regions[1].Controller = 0;
	World.Regions[2].Controller = 5;
	World.Regions[2].IncomeValue = 6;
	World.Regions[3].DepositValue = 12;
	const FForce Force = ForceAt(World, 2, Speeds);
	TestEqual(TEXT("The Drill Rig behind the cut is disconnected"), ConnectedRegions(World), Bit(0));
	FPlan Best = BestPlan(World, Force);
	TestTrue(TEXT("A reconnecting capture beats a richer isolated deposit"),
		Best.Target == 1 && Best.Verb == EVerb::Attack);
	// Taking the cut region restores the income.
	World.Regions[1].Controller = 5;
	TestEqual(TEXT("Taking the cut region reconnects the Drill Rig"), ConnectedRegions(World), Bit(0) | Bit(1) | Bit(2));
	Best = BestPlan(World, Force);
	TestTrue(TEXT("Once connected, the deposit next to the chain is worth taking"), Best.Target == 3);
	// The same picture without the main known keeps the old valuation: the rich deposit wins.
	World.Regions[1].Controller = 0;
	World.Home = INDEX_NONE;
	Best = BestPlan(World, Force);
	TestTrue(TEXT("Without a main the chain term is off and the richer deposit wins"), Best.Target == 3);
	// A Move & Hold on a neutral cut region restores income the same way.
	World.Home = 0;
	World.Regions[1].Controller = INDEX_NONE;
	Best = BestPlan(World, Force);
	TestTrue(TEXT("A neutral region that restores connected income beats the isolated deposit"),
		Best.Target == 1 && Best.Verb == EVerb::MoveAndHold);
	return true;
}

bool FJevChainHoldTest::RunTest(const FString&)
{
	using namespace JevPlanner;
	const float Speeds[] = { 400.f };
	// The force stands in the main (0). Hostiles press leaf region 1 and region 2, which is the
	// only link to region 3's Drill Rig.
	FWorld World = ChainLine();
	World.Regions[0].Neighbours = Bit(1) | Bit(2);
	World.Regions[1].Neighbours = Bit(0);
	World.Regions[2].Neighbours = Bit(0) | Bit(3);
	World.Regions[1].Controller = World.Regions[2].Controller = World.Regions[3].Controller = 5;
	World.Regions[3].IncomeValue = 5;
	World.Regions[1].Hostiles = World.Regions[2].Hostiles = 2;
	const FForce Force = ForceAt(World, 0, Speeds);
	FPlan Best = BestPlan(World, Force);
	TestTrue(TEXT("The hold that keeps connected income outranks an equal leaf"), Best.Target == 2);
	World.Home = INDEX_NONE;
	Best = BestPlan(World, Force);
	TestTrue(TEXT("Without the chain term equal threats tie to the lower region"), Best.Target == 1);
	World.Home = 0;
	World.Regions[3].IncomeValue = 0;
	Best = BestPlan(World, Force);
	TestTrue(TEXT("A cut vertex with no income behind it is worth no more than a leaf"), Best.Target == 1);
	return true;
}

bool FJevChainRigRegionTest::RunTest(const FString&)
{
	using namespace JevPlanner;
	const float Speeds[] = { 400.f };
	// Main 0 touches 1 and 2; region 3 touches only 1.
	const FVector Positions[] = { FVector(0.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f), FVector(0.f, 1000.f, 0.f), FVector(2000.f, 0.f, 0.f) };
	const uint64 Links[] = { Bit(1) | Bit(2), Bit(0) | Bit(3), Bit(0), Bit(1) };
	FWorld World;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		World.Regions[Index].bExists = true;
		World.Regions[Index].Position = Positions[Index];
		World.Regions[Index].Neighbours = Links[Index];
	}
	World.Regions[0].Controller = 5;
	World.Regions[0].bMain = true;
	World.Home = 0;
	// The humans hold region 1, but JEV's own Drill Rig still stands there.
	World.Regions[1].Controller = 0;
	World.Regions[1].IncomeValue = 6;
	World.Regions[2].DepositValue = 4;
	World.Regions[3].DepositValue = 12;
	const FForce Force = ForceAt(World, 0, Speeds);
	FPlan Best = BestPlan(World, Force);
	TestTrue(TEXT("Retaking the region that holds JEV's own Drill Rig beats a free deposit and a richer isolated one"),
		Best.Target == 1 && Best.Verb == EVerb::Attack);
	World.Regions[1].IncomeValue = 0;
	Best = BestPlan(World, Force);
	TestEqual(TEXT("With no rig of its own there the same region is worth less than the connected deposit"), Best.Target, 2);
	return true;
}
#endif
