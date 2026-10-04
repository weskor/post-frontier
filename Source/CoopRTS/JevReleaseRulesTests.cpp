#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/JevReleasePolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevReleaseScheduleTest, "CoopRTS.Rules.JevRelease.Schedule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevReleaseBudgetTest, "CoopRTS.Rules.JevRelease.Budget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevReleaseBehaviourTest, "CoopRTS.Rules.JevRelease.Behaviour",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevReleasePurchaseTest, "CoopRTS.Rules.JevRelease.Purchase",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevReleaseForcesTest, "CoopRTS.Rules.JevRelease.Forces",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevReleaseRaidTest, "CoopRTS.Rules.JevRelease.Raid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevReleaseFightsOnTest, "CoopRTS.Rules.JevRelease.FightsOn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
// Today's three units: Brawler, Rifle, Artillery.
TArray<JevRelease::FUnitOption> TodaysUnits()
{
	return { { 20, EArmorClass::Heavy, EDamageType::Kinetic },
		{ 24, EArmorClass::Light, EDamageType::Piercing },
		{ 40, EArmorClass::Light, EDamageType::Demolition } };
}

// Human main 4 at the far end of the line 0 - 1 - 2 - 3 - 4, team 5 main at 0.
JevPlanner::FWorld RaidLine()
{
	JevPlanner::FWorld World;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		World.Regions[Index].bExists = true;
		World.Regions[Index].Position = FVector(Index * 1000.f, 0.f, 0.f);
		World.Regions[Index].Neighbours = (Index > 0 ? uint64(1) << (Index - 1) : 0)
			| (Index < 4 ? uint64(1) << (Index + 1) : 0);
	}
	World.Regions[0].Controller = World.Regions[1].Controller = 5;
	World.Regions[3].Controller = World.Regions[4].Controller = 0;
	World.Home = 0;
	World.EnemyHome = 4;
	return World;
}
}

bool FJevReleaseScheduleTest::RunTest(const FString&)
{
	using namespace JevRelease;
	const float Expected[] = { 0.f, 120.f, 240.f, 360.f, 480.f, 600.f, 660.f, 720.f, 1200.f };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Expected) - 1; ++Index)
		TestEqual(*FString::Printf(TEXT("Release %d time"), Index), ReleaseTime(Index), Expected[Index]);
	TestEqual(TEXT("Overrun repeats every 60 s from 600 s"), ReleaseTime(15), Expected[8]);
	TestEqual(TEXT("Index -1 has no time"), ReleaseTime(-1), -1.f);
	TestEqual(TEXT("Before the clock starts no release applies"), IndexAt(-.1f), static_cast<int32>(INDEX_NONE));
	const float Seconds[] = { 0.f, 119.9f, 120.f, 239.9f, 240.f, 359.9f, 360.f, 479.9f, 480.f, 599.9f, 600.f, 659.9f, 660.f, 1200.f };
	const int32 Index[] = { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 15 };
	for (int32 At = 0; At < UE_ARRAY_COUNT(Seconds); ++At)
		TestEqual(*FString::Printf(TEXT("Release in force at %.1f s"), Seconds[At]), IndexAt(Seconds[At]), Index[At]);
	for (int32 Check = 0; Check < 30; ++Check)
		TestEqual(TEXT("A release is in force exactly from its own time"), IndexAt(ReleaseTime(Check)), Check);
	TestFalse(TEXT("v1.1 is hidden 30.1 s ahead"), TimelineVisible(1, 89.9f));
	TestTrue(TEXT("v1.1 shows 30 s ahead"), TimelineVisible(1, 90.f));
	TestTrue(TEXT("A release still shows at its own time"), TimelineVisible(1, 120.f));
	TestFalse(TEXT("An overrun release is hidden until 30 s ahead"), TimelineVisible(7, 689.f));
	TestTrue(TEXT("An overrun release shows 30 s ahead"), TimelineVisible(7, 690.f));
	TestFalse(TEXT("No release means nothing to show"), TimelineVisible(INDEX_NONE, 100.f));
	TestTrue(TEXT("Versions follow the table"), VersionOf(0) == EVersion::V10 && VersionOf(1) == EVersion::V11 && VersionOf(2) == EVersion::V12 && VersionOf(3) == EVersion::V20 && VersionOf(4) == EVersion::V21 && VersionOf(5) == EVersion::Overrun && VersionOf(40) == EVersion::Overrun);
	return true;
}

bool FJevReleaseBudgetTest::RunTest(const FString&)
{
	using namespace JevRelease;
	const int32 Base[] = { 0, 150, 250, 400, 600, 300, 300 };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Base); ++Index)
		TestEqual(*FString::Printf(TEXT("Base budget of release %d"), Index), BaseBudget(Index), Base[Index]);
	// N = 1..4 gives factors 1, 1.3, 1.6 and 1.9.
	const int32 Scaled[4][7] = { { 0, 150, 250, 400, 600, 300, 300 }, { 0, 195, 325, 520, 780, 390, 390 },
		{ 0, 240, 400, 640, 960, 480, 480 }, { 0, 285, 475, 760, 1140, 570, 570 } };
	for (int32 Humans = 1; Humans <= 4; ++Humans)
		for (int32 Index = 0; Index < 7; ++Index)
			TestEqual(*FString::Printf(TEXT("Budget of release %d for %d humans"), Index, Humans),
				WaveBudget(Index, Humans), Scaled[Humans - 1][Index]);
	TestEqual(TEXT("Counts below one use solo scaling"), WaveBudget(2, 0), 250);
	TestEqual(TEXT("Overrun budgets stay level however late"), WaveBudget(80, 2), 390);
	return true;
}

bool FJevReleaseBehaviourTest::RunTest(const FString&)
{
	using namespace JevRelease;
	const FBehaviour Base = BehaviourFor(0);
	TestTrue(TEXT("v1.0 adds nothing"), !Base.bRaid && !Base.bCounter && !Base.bCoordinated && Base.SpeedFactor == 1.f);
	const FBehaviour Raid = BehaviourFor(1);
	TestTrue(TEXT("v1.1 raids with the cheapest units"), Raid.bRaid && !Raid.bCounter && !Raid.bCoordinated && Raid.SpeedFactor == 1.f);
	const FBehaviour Counter = BehaviourFor(2);
	TestTrue(TEXT("v1.2 keeps the raid and counters"), Counter.bRaid && Counter.bCounter && !Counter.bCoordinated && Counter.SpeedFactor == 1.f);
	const FBehaviour Together = BehaviourFor(3);
	TestTrue(TEXT("v2.0 attacks together"), Together.bRaid && Together.bCounter && Together.bCoordinated && Together.SpeedFactor == 1.f);
	const FBehaviour Rapid = BehaviourFor(4);
	TestTrue(TEXT("v2.1 adds 15% speed"), Rapid.bCoordinated && Rapid.SpeedFactor == 1.15f);
	const FBehaviour Overrun = BehaviourFor(9);
	TestTrue(TEXT("Overrun keeps every earlier behaviour"), Overrun.bRaid && Overrun.bCounter && Overrun.bCoordinated && Overrun.SpeedFactor == 1.15f);
	return true;
}

bool FJevReleasePurchaseTest::RunTest(const FString&)
{
	using namespace JevRelease;
	const TArray<FUnitOption> Options = TodaysUnits();
	const FBehaviour Raid = BehaviourFor(1);
	const FBehaviour Counter = BehaviourFor(2);
	// v1.1: 150 buys seven Brawlers and carries 10.
	FPurchase Wave = Purchase(150, Options, Raid, EArmorClass::Light);
	TestTrue(TEXT("v1.1 buys only the cheapest unit, even against a class something else counters"),
		Wave.Counts[0] == 7 && Wave.Counts[1] == 0 && Wave.Counts[2] == 0);
	TestTrue(TEXT("The leftover carries"), Wave.Spent == 140 && Wave.Carry == 10 && Wave.Units == 7);
	// The carry joins the next budget: 10 + 250 against Heavy is ten Rifles plus a Brawler.
	Wave = Purchase(Wave.Carry + 250, Options, Counter, EArmorClass::Heavy);
	TestTrue(TEXT("v1.2 buys the Rifle against Heavy and fills the rest cheapest"),
		Wave.Counts[0] == 1 && Wave.Counts[1] == 10 && Wave.Counts[2] == 0 && Wave.Units == 11 && Wave.Carry == 0);
	Wave = Purchase(260, Options, Counter, EArmorClass::Light);
	TestTrue(TEXT("Against Light the Brawler (Kinetic) is the answer"), Wave.Counts[0] == 13 && Wave.Units == 13 && Wave.Carry == 0);
	Wave = Purchase(260, Options, Counter, EArmorClass::Shielded);
	TestTrue(TEXT("With no answer to the class the wave falls back to the cheapest unit"), Wave.Counts[0] == 13 && Wave.Carry == 0);
	Wave = Purchase(260, Options, Counter, EArmorClass::Unset);
	TestTrue(TEXT("With no humans to read the wave buys the cheapest unit"), Wave.Counts[0] == 13);
	Wave = Purchase(100, Options, Counter, EArmorClass::Heavy);
	TestTrue(TEXT("100 against Heavy: four Rifles, then the remaining 4 carries"),
		Wave.Counts[1] == 4 && Wave.Units == 4 && Wave.Spent == 96 && Wave.Carry == 4);
	Wave = Purchase(19, Options, Counter, EArmorClass::Heavy);
	TestTrue(TEXT("A pool below the cheapest unit buys nothing and carries whole"), Wave.Units == 0 && Wave.Carry == 19);
	Wave = Purchase(-30, Options, Counter, EArmorClass::Heavy);
	TestTrue(TEXT("A negative pool is empty"), Wave.Units == 0 && Wave.Carry == 0);
	for (int32 Pool = 0; Pool < 700; Pool += 7)
	{
		Wave = Purchase(Pool, Options, Counter, EArmorClass::Heavy);
		TestEqual(TEXT("Spent plus carry is the pool"), Wave.Spent + Wave.Carry, Pool);
		TestTrue(TEXT("The carry is below the cheapest unit"), Wave.Carry < 20);
	}
	const TArray<FUnitOption> Free = { { 0, EArmorClass::Light, EDamageType::Kinetic }, { 20, EArmorClass::Heavy, EDamageType::Piercing } };
	Wave = Purchase(100, Free, Counter, EArmorClass::Light);
	TestTrue(TEXT("A unit without a price is never bought"), Wave.Counts[0] == 0 && Wave.Counts[1] == 5);
	FArmorCounts Humans;
	TestEqual(TEXT("No humans read as no class"), MostNumerous(Humans), EArmorClass::Unset);
	Humans.Count[0] = 3;
	Humans.Count[1] = 5;
	Humans.Count[2] = 5;
	TestEqual(TEXT("The most numerous class wins, ties to the earlier class"), MostNumerous(Humans), EArmorClass::Heavy);
	Humans.Count[2] = 6;
	TestEqual(TEXT("A larger count beats an earlier class"), MostNumerous(Humans), EArmorClass::Shielded);
	return true;
}

bool FJevReleaseForcesTest::RunTest(const FString&)
{
	using namespace JevRelease;
	TestEqual(TEXT("No units, no forces"), SplitForces(0).Num(), 0);
	TestTrue(TEXT("One unit is a force of one"), SplitForces(1) == TArray<int32>({ 1 }));
	TestTrue(TEXT("Six units fill one force"), SplitForces(6) == TArray<int32>({ 6 }));
	TestTrue(TEXT("Seven units split evenly, larger first"), SplitForces(7) == TArray<int32>({ 4, 3 }));
	TestTrue(TEXT("Twelve units are two full forces"), SplitForces(12) == TArray<int32>({ 6, 6 }));
	TestTrue(TEXT("Thirteen units are three forces"), SplitForces(13) == TArray<int32>({ 5, 4, 4 }));
	for (int32 Units = 1; Units < 100; ++Units)
	{
		int32 Total = 0;
		for (int32 Size : SplitForces(Units))
		{
			TestTrue(TEXT("No force exceeds six"), Size >= 1 && Size <= MaxForceSize);
			Total += Size;
		}
		TestEqual(TEXT("Every unit lands in a force"), Total, Units);
	}
	return true;
}

bool FJevReleaseRaidTest::RunTest(const FString&)
{
	using namespace JevRelease;
	JevPlanner::FWorld World = RaidLine();
	World.Regions[3].HostileRigs = 1;
	TestEqual(TEXT("The raid targets the connected human Drill Rig region"), RaidRegion(World), 3);
	// Region 5 touches only JEV's region 1: a human island the human main cannot supply.
	World.Regions[5].bExists = true;
	World.Regions[5].Controller = 0;
	World.Regions[5].HostileRigs = 1;
	World.Regions[5].Position = FVector(1000.f, 1000.f, 0.f);
	World.Regions[5].Neighbours = uint64(1) << 1;
	World.Regions[1].Neighbours |= uint64(1) << 5;
	TestEqual(TEXT("A Drill Rig the human main no longer connects to is not a raid target"), RaidRegion(World), 3);
	World.Regions[2].Controller = 0;
	World.Regions[2].HostileRigs = 1;
	TestEqual(TEXT("The nearest of several connected Drill Rig regions is chosen"), RaidRegion(World), 2);
	World.Regions[2].HostileRigs = 0;
	World.Regions[3].HostileRigs = 0;
	TestEqual(TEXT("With no connected Drill Rig the nearest connected human region is raided"), RaidRegion(World), 2);
	World.Regions[2].Controller = 5;
	World.Regions[3].Controller = 5;
	TestEqual(TEXT("With nothing else human the raid falls on the human main"), RaidRegion(World), 4);
	JevPlanner::FWorld Fork = RaidLine();
	Fork.Regions[5].bExists = Fork.Regions[6].bExists = true;
	Fork.Regions[5].Controller = Fork.Regions[6].Controller = 0;
	Fork.Regions[5].HostileRigs = Fork.Regions[6].HostileRigs = 1;
	Fork.Regions[5].Position = FVector(1000.f, 1500.f, 0.f);
	Fork.Regions[6].Position = FVector(1000.f, -500.f, 0.f);
	Fork.Regions[5].Neighbours = Fork.Regions[6].Neighbours = (uint64(1) << 1) | (uint64(1) << 4);
	Fork.Regions[1].Neighbours |= (uint64(1) << 5) | (uint64(1) << 6);
	Fork.Regions[4].Neighbours |= (uint64(1) << 5) | (uint64(1) << 6);
	TestEqual(TEXT("Equal hops go to the region closer to JEV's main"), RaidRegion(Fork), 6);
	JevPlanner::FWorld Blind = RaidLine();
	Blind.Home = INDEX_NONE;
	Blind.Regions[3].HostileRigs = 1;
	TestEqual(TEXT("Without a known main the raid falls on the human main"), RaidRegion(Blind), 4);
	return true;
}

bool FJevReleaseFightsOnTest::RunTest(const FString&)
{
	using namespace JevPlanner;
	// Line 0 (JEV main) - 1 (JEV) - 2 (human main); an injured force stands in region 1.
	FWorld World;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		World.Regions[Index].bExists = true;
		World.Regions[Index].Position = FVector(Index * 1000.f, 0.f, 0.f);
		World.Regions[Index].Neighbours = (Index > 0 ? uint64(1) << (Index - 1) : 0) | (Index < 2 ? uint64(1) << (Index + 1) : 0);
	}
	World.Regions[0].Controller = World.Regions[1].Controller = 5;
	World.Regions[2].Controller = 0;
	World.Home = 0;
	World.EnemyHome = 2;
	const float Speeds[] = { 400.f };
	FForce Force;
	Force.Source = 1;
	Force.Home = 0;
	Force.UnitCount = 6;
	Force.HealthFraction = .1f;
	Force.Position = World.Regions[1].Position;
	Force.ClassSpeeds = Speeds;
	FCandidates Candidates = Propose(World, Force);
	TestTrue(TEXT("A badly hurt force that a producer can refill retreats to recover"),
		Choose(Candidates) && Choose(Candidates)->Plan.Verb == EVerb::Retreat);
	Force.bCanRefill = false;
	Candidates = Propose(World, Force);
	TestTrue(TEXT("A free force no producer can refill never retreats to recover"),
		Choose(Candidates) && Choose(Candidates)->Plan.Verb != EVerb::Retreat);
	return true;
}
#endif
