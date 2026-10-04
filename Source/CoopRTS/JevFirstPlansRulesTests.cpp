#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/JevExecution.h"
#include "Rules/JevPlanner.h"

// JEV's first plans (battle.md "Opening"): a producer-backed force that has no unit yet is planned at its squad
// size, and its commitment runs on the clock it is decided on.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevFirstPlansTest, "CoopRTS.Rules.Jev.FirstPlans",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
JevPlanner::FWorld WorldSummary()
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
}

bool FJevFirstPlansTest::RunTest(const FString&)
{
	using namespace JevPlanner;
	const FWorld World = WorldSummary();
	const float Speeds[] = { 100.f };
	FForce Empty;
	Empty.Source = Empty.Home = 0;
	Empty.SquadSize = 6;
	Empty.ClassSpeeds = Speeds;

	TestEqual(TEXT("An empty producer-backed force is planned at its squad size"), Strength(Empty), 6);
	const FCandidates Candidates = Propose(World, Empty);
	TestTrue(TEXT("An empty producer-backed force gets a legal proposal"), Candidates.Count > 0);
	for (int32 Index = 0; Index < Candidates.Count; ++Index)
		TestEqual(TEXT("Its proposals carry the squad's size band"), Candidates.Values[Index].Plan.SizeBand, SizeBand(6));
	FForce Unfilled = Empty;
	Unfilled.SquadSize = 0;
	TestEqual(TEXT("An empty force nothing refills still gets no proposal"), Propose(World, Unfilled).Count, 0);

	FForce Partial = Empty;
	Partial.UnitCount = 2;
	TestEqual(TEXT("Living units replace the expected squad"), Strength(Partial), 2);
	TestEqual(TEXT("A partly filled force plans at what it has"), Choose(Propose(World, Partial))->Plan.SizeBand, SizeBand(2));

	// Commitment: planned on the frozen clock, so the 25 s run from that clock, which is 0:00 when planning ends.
	const float Clock = 120.f;
	FPlan First;
	TestTrue(TEXT("The empty force decides"), Decide(World, Empty, Clock, nullptr, First));
	TestEqual(TEXT("Its commitment starts at the deciding clock"), First.CommittedUntil, Clock + CommitmentSeconds);
	TestEqual(TEXT("A full commitment remains at that clock"), Remaining(First, Clock), CommitmentSeconds);
	First.bUnissued = true;
	FPlan Again;
	TestTrue(TEXT("The same clock decides again"), Decide(World, Empty, Clock, &First, Again));
	TestTrue(TEXT("The commitment holds the plan unchanged"),
		Again.Verb == First.Verb && Again.Target == First.Target && Again.CommittedUntil == First.CommittedUntil);
	TestFalse(TEXT("Holding it is not a new commitment"), JevExecution::NewCommitment(&First, Again));
	const JevExecution::FOrderChange Opening = JevExecution::OrderChange(Again, &First, true);
	TestTrue(TEXT("The plan is issued once when the force first receives an order"), Opening.bChanged && !Opening.bFresh);
	FPlan Expired;
	TestTrue(TEXT("After the commitment the force decides afresh"), Decide(World, Empty, Clock + CommitmentSeconds, &First, Expired));
	TestEqual(TEXT("The next commitment runs 25 s from its own clock"), Expired.CommittedUntil, Clock + 2.f * CommitmentSeconds);

	TestFalse(TEXT("A force with neither units nor an unfielded producer is not planned"), JevExecution::IsPlanned(0, false));
	TestTrue(TEXT("A producer's force that never fielded a unit is planned before its first"), JevExecution::IsPlanned(0, true));
	TestTrue(TEXT("A force with units is planned"), JevExecution::IsPlanned(1, false));
	return true;
}
#endif
