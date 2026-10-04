#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/JevThreatPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevThreatScheduleTest, "CoopRTS.Rules.JevThreat.Schedule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevThreatBudgetTest, "CoopRTS.Rules.JevThreat.Budget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevThreatPairsTest, "CoopRTS.Rules.JevThreat.Pairs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevThreatNeckTest, "CoopRTS.Rules.JevThreat.Neck",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevThreatChoiceTest, "CoopRTS.Rules.JevThreat.Choice",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevThreatTargetsTest, "CoopRTS.Rules.JevThreat.Targets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevThreatUnitsTest, "CoopRTS.Rules.JevThreat.Units",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevThreatMemoTest, "CoopRTS.Rules.JevThreat.Memo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
using JevThreat::FPair;

// Human main 0, JEV main 9. From 0: regions 1 and 2 are 1 hop, 3, 4 and 5 are 2. Region 1 is on every shortest path to 3,
// 4 and 6 (3 and 4 give 6 two equal paths, so neither is a neck), 2 on the only shortest path to 5 and 5 on the only one
// to 7. Regions 6, 7 and 8 lie nearer JEV's main.
//
//   0 - 1 - 3 - 6 - 8 - 9
//   |    \_ 4 _/    |
//   2 ------ 5 - 7 -/
JevPlanner::FWorld Graph()
{
	const TArray<TArray<int32>> Links = { { 1, 2 }, { 0, 3, 4 }, { 0, 5 }, { 1, 6 }, { 1, 6 }, { 2, 7 }, { 3, 4, 8 }, { 5, 8 },
		{ 6, 7, 9 }, { 8 } };
	JevPlanner::FWorld World;
	for (int32 Index = 0; Index < Links.Num(); ++Index)
	{
		World.Regions[Index].bExists = true;
		World.Regions[Index].Position = FVector(Index * 1000.f, 0.f, 0.f);
		for (const int32 Neighbour : Links[Index])
			World.Regions[Index].Neighbours |= uint64(1) << Neighbour;
	}
	World.Regions[0].bMain = World.Regions[9].bMain = true;
	World.Home = 9;
	World.EnemyHome = 0;
	World.Team = 5;
	World.Regions[0].Controller = 0;
	World.Regions[9].Controller = 5;
	return World;
}

void Control(JevPlanner::FWorld& World, int32 Region, int32 Team)
{
	World.Regions[Region].Controller = Team;
}

// Necks of Graph(): 1 (shortest path to 3, 4 and 6), 2 (to 5) and 5 (to 7).
const FPair Authored[] = { { 1, 2 }, { 1, 5 }, { 2, 5 } };

}

bool FJevThreatScheduleTest::RunTest(const FString&)
{
	using namespace JevThreat;
	TestEqual(TEXT("The threat rides on v2.0, at 360 s"), LaunchTime(), 360.f);
	TestEqual(TEXT("Its plans publish 30 s ahead"), PublishTime(), 330.f);
	TestEqual(TEXT("Waiting steps to nothing before the lead"), static_cast<int32>(NextStep(EStage::Waiting, 329.9f)), static_cast<int32>(EStep::None));
	TestEqual(TEXT("then publishes from 330 s"), static_cast<int32>(NextStep(EStage::Waiting, 330.f)), static_cast<int32>(EStep::Publish));
	TestEqual(TEXT("A clock past the launch still publishes first, so no plan is skipped"),
		static_cast<int32>(NextStep(EStage::Waiting, 400.f)), static_cast<int32>(EStep::Publish));
	TestEqual(TEXT("Published waits for the launch"), static_cast<int32>(NextStep(EStage::Published, 359.9f)), static_cast<int32>(EStep::None));
	TestEqual(TEXT("and launches at 360 s"), static_cast<int32>(NextStep(EStage::Published, 360.f)), static_cast<int32>(EStep::Launch));
	for (const float Seconds : { 0.f, 330.f, 360.f, 1200.f })
		TestEqual(TEXT("A threat that has launched or been skipped never steps again"),
			static_cast<int32>(NextStep(EStage::Done, Seconds)), static_cast<int32>(EStep::None));
	return true;
}

bool FJevThreatBudgetTest::RunTest(const FString&)
{
	using namespace JevThreat;
	TestEqual(TEXT("No commander or one sends one force"), TargetCount(0) + TargetCount(1), 2);
	TestEqual(TEXT("Two commanders send two"), TargetCount(2), 2);
	TestEqual(TEXT("and more than two still send two"), TargetCount(4), 2);
	TestEqual(TEXT("Solo: half the 400 v2.0 budget"), ForceBudget(1), 200);
	TestEqual(TEXT("Two commanders: half of 400 x 1.3"), ForceBudget(2), 260);
	TestEqual(TEXT("Three commanders: half of 400 x 1.6"), ForceBudget(3), 320);
	TestEqual(TEXT("Two cut forces together carry exactly one more v2.0 wave budget"), 2 * ForceBudget(2),
		JevRelease::WaveBudget(TriggerRelease, 2));
	TestEqual(TEXT("A budget below zero commanders reads as one commander"), ForceBudget(0), ForceBudget(1));
	return true;
}

bool FJevThreatPairsTest::RunTest(const FString&)
{
	using namespace JevThreat;
	TestEqual(TEXT("Pair 0"), PairOfTag(TEXT("SplitBrain.0")), 0);
	TestEqual(TEXT("Pair 7 is the last"), PairOfTag(TEXT("SplitBrain.7")), 7);
	TestEqual(TEXT("A leading zero names the same pair"), PairOfTag(TEXT("SplitBrain.03")), 3);
	for (const TCHAR* Bad : { TEXT("SplitBrain.8"), TEXT("SplitBrain."), TEXT("SplitBrain.x"), TEXT("SplitBrain.-1"), TEXT("SplitBrain.100"),
			 TEXT("splitbrain.1"), TEXT("Region02_West Cut"), TEXT("") })
		TestEqual(*FString::Printf(TEXT("'%s' names no pair"), Bad), PairOfTag(Bad), static_cast<int32>(INDEX_NONE));

	const TArray<FPair> Built = BuildPairs({ { 8, 1 }, { 3, 0 }, { 3, 1 }, { 1, 0 }, { 5, 4 }, { 6, 4 } });
	TestEqual(TEXT("Three tags, three pairs"), Built.Num(), 3);
	TestTrue(TEXT("Pairs come in tag order with their regions in the order the tags were read"),
		Built.Num() == 3 && Built[0].A == 3 && Built[0].B == 1 && Built[1].A == 8 && Built[1].B == 3 && Built[2].A == 5 && Built[2].B == 6);
	TestEqual(TEXT("A tag held once names no pair"), BuildPairs({ { 3, 0 } }).Num(), 0);
	TestEqual(TEXT("A tag held three times names no pair"), BuildPairs({ { 3, 0 }, { 4, 0 }, { 5, 0 } }).Num(), 0);
	TestEqual(TEXT("A tag held twice by one region names no pair"), BuildPairs({ { 3, 0 }, { 3, 0 } }).Num(), 0);
	TestEqual(TEXT("Regions without a tag are ignored"), BuildPairs({ { 3, INDEX_NONE }, { 4, INDEX_NONE } }).Num(), 0);
	TestEqual(TEXT("An out-of-range tag index is ignored"), BuildPairs({ { 3, MaxPairs }, { 4, MaxPairs } }).Num(), 0);
	return true;
}

bool FJevThreatNeckTest::RunTest(const FString&)
{
	using namespace JevThreat;
	const JevPlanner::FWorld World = Graph();
	TestTrue(TEXT("Region 1 is on every shortest path to 3, 4 and 6"), IsSupplyNeck(World, 1));
	TestTrue(TEXT("Region 2 is on the only shortest path to 5"), IsSupplyNeck(World, 2));
	TestTrue(TEXT("Region 5 is on the only shortest path to 7"), IsSupplyNeck(World, 5));
	TestFalse(TEXT("Region 3 has an equal alternative through 4"), IsSupplyNeck(World, 3));
	TestFalse(TEXT("so does region 4"), IsSupplyNeck(World, 4));
	TestFalse(TEXT("A region nearer JEV's main is not the humans' neck"), IsSupplyNeck(World, 6) || IsSupplyNeck(World, 7) || IsSupplyNeck(World, 8));
	TestFalse(TEXT("The humans' main is no neck"), IsSupplyNeck(World, 0));
	TestFalse(TEXT("JEV's main is no neck"), IsSupplyNeck(World, 9));
	TestFalse(TEXT("A region the map does not have is no neck"), IsSupplyNeck(World, 12));
	TestFalse(TEXT("An index outside the world is no neck"), IsSupplyNeck(World, ForceOrders::MaxRegions));

	JevPlanner::FWorld Shortcut = Graph();
	Shortcut.Regions[0].Neighbours |= uint64(1) << 5;
	Shortcut.Regions[5].Neighbours |= uint64(1) << 0;
	TestFalse(TEXT("A direct link from the main to 5 takes region 2 off the only short path"), IsSupplyNeck(Shortcut, 2));
	TestTrue(TEXT("Adjacency reads the neighbour masks"), Adjacent(World, 1, 3) && Adjacent(World, 3, 1) && !Adjacent(World, 1, 2));
	TestFalse(TEXT("A region is not adjacent to one the map lacks"), Adjacent(World, 1, 12));
	return true;
}

bool FJevThreatChoiceTest::RunTest(const FString&)
{
	using namespace JevThreat;
	TestEqual(TEXT("No authored pair skips"), static_cast<int32>(ChoosePair(Graph(), {}).Skip), static_cast<int32>(ESkip::NoPairs));
	TestTrue(TEXT("with a logged reason"), FString(SkipReason(ESkip::NoPairs)).Contains(TEXT("no neck pair")));

	JevPlanner::FWorld World = Graph();
	Control(World, 1, 0);
	Control(World, 5, 0);
	FChoice Choice = ChoosePair(World, Authored);
	TestTrue(TEXT("The pair both of whose regions the humans hold wins over nearer and earlier pairs"),
		Choice.Skip == ESkip::None && Choice.Pair.A == 1 && Choice.Pair.B == 5 && Choice.Held == 2 && !Choice.bFallback);

	Control(World, 5, -1);
	Control(World, 2, 0);
	Choice = ChoosePair(World, Authored);
	TestTrue(TEXT("Holding 1 and 2 picks that pair, though the human main reaches them first"),
		Choice.Pair.A == 1 && Choice.Pair.B == 2 && Choice.Held == 2);

	Control(World, 2, -1);
	Choice = ChoosePair(World, Authored);
	TestTrue(TEXT("With one region held the pair containing it wins; of two such pairs the nearer is a fallback"),
		Choice.Pair.A == 1 && Choice.Pair.B == 2 && Choice.Held == 1 && Choice.bFallback);

	Control(World, 1, -1);
	Choice = ChoosePair(World, Authored);
	TestTrue(TEXT("Holding nothing, the pair fewest hops from the human main wins: 1 and 2 (1 + 1) before 1 and 5 (1 + 2)"),
		Choice.Pair.A == 1 && Choice.Pair.B == 2 && Choice.Held == 0 && Choice.bFallback);
	const FPair Far[] = { { 1, 5 }, { 1, 2 } };
	Choice = ChoosePair(World, Far);
	TestTrue(TEXT("whatever the authored order"), Choice.Pair.A == 1 && Choice.Pair.B == 2);
	const FPair Tied[] = { { 1, 2 }, { 2, 1 } };
	TestTrue(TEXT("Equal pairs keep authored order"), ChoosePair(World, Tied).Pair.A == 1);

	Control(World, 1, 5);
	Choice = ChoosePair(World, Authored);
	TestTrue(TEXT("A region JEV controls rules its pairs out; pair 2 and 5 is adjacent, so the rest are gone"),
		Choice.Skip == ESkip::NoEligiblePair);
	TestTrue(TEXT("with a logged reason"), FString(SkipReason(ESkip::NoEligiblePair)).Contains(TEXT("JEV controls")));

	const FPair Bad[] = { { 2, 5 }, { 3, 4 }, { 0, 1 }, { 2, 2 }, { 1, 12 }, { 6, 1 } };
	Control(World, 1, -1);
	TestEqual(TEXT("Adjacent, non-neck, main, repeated, missing and wrong-side pairs are never taken"),
		static_cast<int32>(ChoosePair(World, Bad).Skip), static_cast<int32>(ESkip::NoEligiblePair));
	return true;
}

bool FJevThreatTargetsTest::RunTest(const FString&)
{
	using namespace JevThreat;
	JevPlanner::FWorld World = Graph();
	const FPair Pair{ 1, 5 };
	FTargets Both = ChooseTargets(World, Pair, 2);
	TestTrue(TEXT("Two commanders: both regions of the pair"), Both.Count == 2 && Both.Region[0] == 1 && Both.Region[1] == 5);
	Both = ChooseTargets(World, Pair, 3);
	TestEqual(TEXT("Three commanders send two forces too"), Both.Count, 2);

	FTargets Alone = ChooseTargets(World, Pair, 1);
	TestTrue(TEXT("Alone with neither held: the one nearer the human main"), Alone.Count == 1 && Alone.Region[0] == 1);
	Control(World, 5, 0);
	Alone = ChooseTargets(World, Pair, 1);
	TestTrue(TEXT("Alone: the region the humans hold"), Alone.Count == 1 && Alone.Region[0] == 5);
	Control(World, 1, 0);
	Alone = ChooseTargets(World, Pair, 1);
	TestTrue(TEXT("Alone with both held: the one nearer the human main"), Alone.Count == 1 && Alone.Region[0] == 1);
	Alone = ChooseTargets(World, FPair{ 5, 1 }, 1);
	TestTrue(TEXT("whichever way round the pair is authored"), Alone.Count == 1 && Alone.Region[0] == 1);
	Control(World, 1, -1);
	Alone = ChooseTargets(World, FPair{ 2, 1 }, 1);
	TestTrue(TEXT("Equal distance goes to A"), Alone.Count == 1 && Alone.Region[0] == 2);
	Alone = ChooseTargets(World, FPair{ 1, 2 }, 0);
	TestTrue(TEXT("and no commander is solo"), Alone.Count == 1 && Alone.Region[0] == 1);
	return true;
}

bool FJevThreatUnitsTest::RunTest(const FString&)
{
	using namespace JevThreat;
	TestEqual(TEXT("A cut force is five Lancers"), ForceUnits, 5);
	TestEqual(TEXT("Two commanders' 260 buys ten Lancers at 24 Power; the force takes five"), UnitsFor(ForceBudget(2), 24), 5);
	TestEqual(TEXT("Solo's 200 buys eight; the force takes five"), UnitsFor(ForceBudget(1), 24), 5);
	TestEqual(TEXT("120 is exactly five"), UnitsFor(120, 24), 5);
	TestEqual(TEXT("119 buys four whole units"), UnitsFor(119, 24), 4);
	TestEqual(TEXT("A budget under one unit buys none"), UnitsFor(23, 24), 0);
	TestEqual(TEXT("A budget below zero buys none"), UnitsFor(-50, 24), 0);
	TestEqual(TEXT("A unit that costs nothing is never bought"), UnitsFor(260, 0), 0);
	TestEqual(TEXT("nor one with a negative cost"), UnitsFor(260, -24), 0);
	return true;
}

bool FJevThreatMemoTest::RunTest(const FString&)
{
	TStringBuilder<128> Memo;
	JevThreat::AppendMemo(Memo, 12, 6, TEXT("Uplink"), 64.2f);
	TestEqual(TEXT("The memo names the threat, the band, the region and the countdown"), FString(Memo.ToView()),
		FString(TEXT("Ticket #12 \u00B7 Attack: Split-Brain Cut sends ~6 units to Uplink \u00B7 ETA 1:05")));
	return true;
}
#endif
