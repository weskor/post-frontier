#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/JevPlanner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevCandidatesTest, "CoopRTS.Rules.Jev.Candidates",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevChoiceTest, "CoopRTS.Rules.Jev.Choice",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevCommitmentTest, "CoopRTS.Rules.Jev.Commitment",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevExceptionsTest, "CoopRTS.Rules.Jev.Exceptions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevStructureTargetTest, "CoopRTS.Rules.Jev.StructureTarget",
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
	World.EnemyHome = 3;
	return World;
}

JevPlanner::FForce ForceSummary(TConstArrayView<float> Speeds)
{
	JevPlanner::FForce Force;
	Force.Source = Force.Home = 0;
	Force.UnitCount = 5;
	Force.ClassSpeeds = Speeds;
	return Force;
}
}

bool FJevCandidatesTest::RunTest(const FString&)
{
	using namespace JevPlanner;
	FWorld World = WorldSummary();
	const float Speeds[] = { 200.f, 100.f };
	FForce Force = ForceSummary(Speeds);
	TestEqual(TEXT("ETA follows the slowest class across every path leg"), TravelSeconds(World, Force, 2), 20.f);
	FWorld Safe = World;
	Safe.Regions[2].Controller = 5;
	FForce Returning = Force;
	Returning.Home = 2;
	Returning.HealthFraction = .1f;
	TestEqual(TEXT("Retreat ETA includes its sprint speed"), Choose(Propose(Safe, Returning))->Plan.EtaSeconds, 16.f);
	Force.HealthFraction = .5f;
	Force.bRecovering = Force.bAtRecovery = true;
	const FPlan RecoveryHold = Choose(Propose(World, Force))->Plan;
	TestEqual(TEXT("An arrived injured roster holds safety rather than restarting Retreat"), RecoveryHold.Verb, EVerb::MoveAndHold);
	TestEqual(TEXT("Recovery holds the force's safe source"), RecoveryHold.Target, Force.Source);
	Force.HealthFraction = .8f;
	TestEqual(TEXT("Eighty-percent health releases recovery to strategic expansion"), Choose(Propose(World, Force))->Plan.Target, 1);
	Force.bRecovering = Force.bAtRecovery = false;
	World.Regions[0].Neighbours = uint64(1) << 1;
	World.Regions[1].Position = FVector(0.f, 1000.f, 0.f);
	TestTrue(TEXT("ETA uses the region path, not the endpoint distance"), TravelSeconds(World, Force, 2) > 30.f);
	World.Regions[1].Neighbours = 1;
	TestEqual(TEXT("Disconnected target has no ETA"), TravelSeconds(World, Force, 2), -1.f);
	World.Regions[1].bClaimed = true;
	const FCandidates Candidates = Propose(World, Force);
	for (int32 Index = 0; Index < Candidates.Count; ++Index)
		TestEqual(TEXT("Claimed and disconnected targets cannot be proposed"), Candidates.Values[Index].Plan.Target, 0);
	World.Regions[0].Hostiles = 1;
	Force.HealthFraction = .1f;
	const FCandidates Threatened = Propose(World, Force);
	for (int32 Index = 0; Index < Threatened.Count; ++Index)
		TestNotEqual(TEXT("Retreat cannot select a threatened recovery region"), Threatened.Values[Index].Plan.Verb, EVerb::Retreat);
	Force.UnitCount = 0;
	TestEqual(TEXT("An empty force cannot publish a deployment"), Propose(World, Force).Count, 0);
	for (int32 Count = 0; Count <= 9; ++Count)
		TestEqual(TEXT("Size band rounds half upward with minimum two"), SizeBand(Count), FMath::Max(2, ((Count + 1) / 2) * 2));
	return true;
}

bool FJevChoiceTest::RunTest(const FString&)
{
	using namespace JevPlanner;
	FWorld World = WorldSummary();
	const float Speeds[] = { 100.f };
	FForce Force = ForceSummary(Speeds);
	TestEqual(TEXT("Neutral expansion prefers the nearest legal region"), Choose(Propose(World, Force))->Plan.Target, 1);
	World.Regions[2].DepositValue = 10;
	TestEqual(TEXT("Economic value can outweigh distance"), Choose(Propose(World, Force))->Plan.Target, 2);
	World.bAdvantage = true;
	TestEqual(TEXT("Advantage authorizes attack on hostile main"), Choose(Propose(World, Force))->Plan.Verb, EVerb::Attack);
	Force.HealthFraction = .34f;
	TestEqual(TEXT("Injury prefers recovery over assault"), Choose(Propose(World, Force))->Plan.Verb, EVerb::Retreat);
	Force.bRecovering = true;
	Force.HealthFraction = .79f;
	TestEqual(TEXT("Recovery persists below release health"), Choose(Propose(World, Force))->Plan.Verb, EVerb::Retreat);
	Force.HealthFraction = .8f;
	TestEqual(TEXT("Recovery releases at boundary"), Choose(Propose(World, Force))->Plan.Verb, EVerb::Attack);
	FCandidates Ties;
	Ties.Count = 3;
	Ties.Values[0].Plan.Target = 3;
	Ties.Values[1].Plan.Target = 2;
	Ties.Values[2].Plan.Target = 1;
	TestEqual(TEXT("Equal scores choose stable region index, not enumeration order"), Choose(Ties)->Plan.Target, 1);
	Ties.Values[0].Score = 1.f;
	TestEqual(TEXT("Strictly better score wins tie key"), Choose(Ties)->Plan.Target, 3);
	return true;
}

bool FJevCommitmentTest::RunTest(const FString&)
{
	using namespace JevPlanner;
	FWorld World = WorldSummary();
	const float Speeds[] = { 100.f };
	FForce Force = ForceSummary(Speeds);
	FPlan Initial;
	TestTrue(TEXT("Initial proposal is legal"), Decide(World, Force, 10.f, nullptr, Initial));
	TestEqual(TEXT("Commitment ends exactly 25 seconds later"), Initial.CommittedUntil, 35.f);
	World.bAdvantage = true;
	Force.HealthFraction = .1f;
	Force.UnitCount = 2;
	FPlan Held;
	TestTrue(TEXT("Commitment remains available despite score changes"), Decide(World, Force, 34.999f, &Initial, Held));
	TestEqual(TEXT("Injury does not add an undeclared commitment exception"), Held.Verb, Initial.Verb);
	TestEqual(TEXT("Target held until exact expiry"), Held.Target, Initial.Target);
	TestEqual(TEXT("Published size is committed, not live flicker"), Held.SizeBand, Initial.SizeBand);
	TestEqual(TEXT("Repeated evaluations do not extend deadline"), Held.CommittedUntil, 35.f);
	TestTrue(TEXT("Expiry permits recovery"), Decide(World, Force, 35.f, &Initial, Held));
	TestEqual(TEXT("At exact expiry the better recovery wins"), Held.Verb, EVerb::Retreat);
	TestEqual(TEXT("New commitment has a new deadline"), Held.CommittedUntil, 60.f);
	TestEqual(TEXT("Remaining commitment clamps after expiry"), Remaining(Held, 61.f), 0.f);
	return true;
}

bool FJevExceptionsTest::RunTest(const FString&)
{
	using namespace JevPlanner;
	FWorld World = WorldSummary();
	const float Speeds[] = { 100.f };
	FForce Force = ForceSummary(Speeds);
	FPlan Initial, Next;
	Decide(World, Force, 0.f, nullptr, Initial);
	World.Regions[0].bAttacked = true;
	TestTrue(TEXT("Damage from outside the source polygon permits defense"), Decide(World, Force, 1.f, &Initial, Next));
	TestTrue(TEXT("Ranged region damage escalates even without an invading unit"), Next.bEscalated);
	World.Regions[0].bAttacked = false;
	World.Regions[0].Hostiles = 1;
	TestTrue(TEXT("Own-region attack permits escalation"), Decide(World, Force, 2.f, &Initial, Next));
	TestTrue(TEXT("Defense is visibly escalated"), Next.bEscalated);
	TestEqual(TEXT("Defense targets own current region"), Next.Target, Force.Source);
	TestEqual(TEXT("Defense uses Move and Hold"), Next.Verb, EVerb::MoveAndHold);
	TestEqual(TEXT("Escalation retains deadline"), Next.CommittedUntil, Initial.CommittedUntil);
	Force.UnitCount = 2;
	FPlan Repeated;
	Decide(World, Force, 3.f, &Next, Repeated);
	TestEqual(TEXT("Ongoing defense does not flicker its committed size band"), Repeated.SizeBand, Next.SizeBand);
	World.Regions[0].Hostiles = 0;
	FPlan Defending;
	Decide(World, Force, 4.f, &Next, Defending);
	TestTrue(TEXT("Defense remains committed after threat leaves"), Defending.bEscalated);
	World.Regions[Initial.Target].bTargetAlive = false;
	TestTrue(TEXT("Destroyed target triggers immediate replacement"), Decide(World, Force, 4.f, &Initial, Next));
	TestNotEqual(TEXT("Destroyed region cannot be selected again"), Next.Target, Initial.Target);
	TestEqual(TEXT("Replacement receives full commitment"), Next.CommittedUntil, 29.f);
	World.Regions[Initial.Target].bTargetAlive = true;
	World.Regions[Initial.Target].Controller = 5;
	TestTrue(TEXT("Captured target triggers immediate replacement"), Decide(World, Force, 5.f, &Initial, Next));
	TestNotEqual(TEXT("Captured expansion cannot be selected again"), Next.Target, Initial.Target);
	World.Regions[Initial.Target].Controller = 0;
	Decide(World, Force, 6.f, &Initial, Next);
	TestEqual(TEXT("Capture by opponents is not an exception"), Next.CommittedUntil, Initial.CommittedUntil);
	return true;
}

bool FJevStructureTargetTest::RunTest(const FString&)
{
	using namespace JevPlanner;
	FWorld World = WorldSummary();
	World.Regions[1].Controller = 0;
	FTarget Targets[] = { { 101, 1, true }, { 102, 1, true } };
	World.Targets = Targets;
	const float Speeds[] = { 100.f };
	const FForce Force = ForceSummary(Speeds);
	FPlan Initial, Held, Replacement;
	TestTrue(TEXT("A hostile structure is a legal attack target"), Decide(World, Force, 0.f, nullptr, Initial));
	TestEqual(TEXT("Structure attack records stable identity"), Initial.TargetIdentity, uint32(101));
	Targets[1].bAlive = false;
	Decide(World, Force, 2.f, &Initial, Held);
	TestEqual(TEXT("An unrelated structure dying does not release commitment"), Held.CommittedUntil, Initial.CommittedUntil);
	Targets[0].bAlive = false;
	TestTrue(TEXT("Actual structure death releases commitment immediately"), Decide(World, Force, 3.f, &Initial, Replacement));
	TestEqual(TEXT("The replacement can keep attacking the surviving region"), Replacement.Target, Initial.Target);
	TestEqual(TEXT("The dead structure is not reused"), Replacement.TargetIdentity, uint32(0));
	TestEqual(TEXT("Structure invalidation starts a fresh window"), Replacement.CommittedUntil, 28.f);
	World.Targets = {};
	TestFalse(TEXT("Removed actors invalidate their committed identity"), TargetValid(World, Initial));
	return true;
}
#endif
