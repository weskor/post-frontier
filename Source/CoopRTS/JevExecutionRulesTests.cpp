#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/JevExecution.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevExecutionEconomyTest, "CoopRTS.Rules.JevExecution.Economy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevExecutionClaimsTest, "CoopRTS.Rules.JevExecution.Claims",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevExecutionOrderChangeTest, "CoopRTS.Rules.JevExecution.OrderChange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevExecutionPublicationTest, "CoopRTS.Rules.JevExecution.Publication",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevExecutionPlansTest, "CoopRTS.Rules.JevExecution.Plans",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevExecutionSummaryTest, "CoopRTS.Rules.JevExecution.Summary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevExecutionForwardRegionTest, "CoopRTS.Rules.JevExecution.ForwardRegion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
// A line of four regions 1000 cm apart: team 5 holds region 0, team 0 holds region 3.
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
	World.EnemyHome = 3;
	return World;
}

JevPlanner::FPlan Plan(JevPlanner::EVerb Verb, int32 Target, float CommittedUntil)
{
	JevPlanner::FPlan Result;
	Result.Verb = Verb;
	Result.Target = Target;
	Result.CommittedUntil = CommittedUntil;
	Result.bRequiresUnownedTarget = Verb != JevPlanner::EVerb::Retreat;
	return Result;
}
}

bool FJevExecutionEconomyTest::RunTest(const FString&)
{
	using namespace JevExecution;
	FEconomy Economy;
	Economy.Established = 1;
	Economy.Producers = 1;
	Economy.bForwardAnchor = true;
	Economy.bWorkshopDefined = true;
	Economy.Resources = 300;
	Economy.Reserve = 100;
	Economy.ProducerCost = 200;
	Economy.WorkshopCost = 150;
	Economy.ResearchCost = 150;
	TestEqual(TEXT("A producer is bought when cost plus the squad reserve is exactly affordable"),
		NextEconomyAction(Economy), EEconomyAction::BuildProducer);
	Economy.Resources = 299;
	TestEqual(TEXT("One short of the producer's reserve falls through to the cheaper workshop"),
		NextEconomyAction(Economy), EEconomyAction::BuildWorkshop);
	Economy.Resources = 249;
	TestEqual(TEXT("Nothing is bought below the workshop's reserve"), NextEconomyAction(Economy), EEconomyAction::None);
	Economy.Resources = 1000;
	Economy.bThreatened = true;
	TestEqual(TEXT("A threatened commander builds no forward producer"), NextEconomyAction(Economy), EEconomyAction::BuildWorkshop);
	Economy.bThreatened = false;
	Economy.Producers = MaxProducers;
	TestEqual(TEXT("The producer cap falls through to the workshop"), NextEconomyAction(Economy), EEconomyAction::BuildWorkshop);
	Economy = FEconomy();
	Economy.Resources = 1000;
	Economy.bForwardAnchor = true;
	Economy.bWorkshopDefined = true;
	TestEqual(TEXT("No completed extractor, no expansion"), NextEconomyAction(Economy), EEconomyAction::None);
	Economy.Established = 1;
	Economy.Producers = MaxProducers;
	Economy.bHasWorkshop = true;
	Economy.ResearchCost = 150;
	TestEqual(TEXT("An unfinished workshop researches nothing"), NextEconomyAction(Economy), EEconomyAction::None);
	Economy.bWorkshopComplete = true;
	TestEqual(TEXT("A finished workshop researches a doctrine"), NextEconomyAction(Economy), EEconomyAction::Research);
	Economy.bDoctrineChosen = true;
	TestEqual(TEXT("A chosen doctrine is not researched again"), NextEconomyAction(Economy), EEconomyAction::None);
	TestTrue(TEXT("Affordability keeps the reserve"), CanAfford(250, 150, 100) && !CanAfford(249, 150, 100));
	return true;
}

bool FJevExecutionClaimsTest::RunTest(const FString&)
{
	using namespace JevExecution;
	JevPlanner::FWorld World = LineWorld();
	JevPlanner::FPlan Attack = Plan(JevPlanner::EVerb::Attack, 2, 30.f);
	TestTrue(TEXT("An unexpired attack on an unowned region holds its claim"), HoldsClaim(World, Attack, 10.f));
	TestFalse(TEXT("The claim lapses exactly at the commitment deadline"), HoldsClaim(World, Attack, 30.f));
	TestFalse(TEXT("A retreat claims nothing"), HoldsClaim(World, Plan(JevPlanner::EVerb::Retreat, 2, 30.f), 10.f));
	TestFalse(TEXT("A missing target claims nothing"), HoldsClaim(World, Plan(JevPlanner::EVerb::Attack, INDEX_NONE, 30.f), 10.f));
	JevPlanner::FPlan Structure = Attack;
	Structure.TargetIdentity = 77;
	TestFalse(TEXT("A structure target the world no longer lists is not claimed"), HoldsClaim(World, Structure, 10.f));
	World.Regions[2].Controller = 5;
	TestFalse(TEXT("Capturing the target releases the claim"), HoldsClaim(World, Attack, 10.f));
	TestFalse(TEXT("A newly committed plan on an owned region claims nothing"), ClaimsTarget(World, Attack));
	World.Regions[2].Controller = 0;
	TestTrue(TEXT("A newly committed plan claims an enemy-held region regardless of deadline"), ClaimsTarget(World, Plan(JevPlanner::EVerb::MoveAndHold, 2, 0.f)));
	TestFalse(TEXT("A newly committed retreat claims nothing"), ClaimsTarget(World, Plan(JevPlanner::EVerb::Retreat, 2, 30.f)));
	TestFalse(TEXT("A plan without a region claims nothing"), ClaimsTarget(World, Plan(JevPlanner::EVerb::Attack, ForceOrders::MaxRegions, 30.f)));
	return true;
}

bool FJevExecutionOrderChangeTest::RunTest(const FString&)
{
	using namespace JevExecution;
	const JevPlanner::FPlan Current = Plan(JevPlanner::EVerb::Attack, 2, 25.f);
	JevPlanner::FPlan Next = Current;
	FOrderChange Same = OrderChange(Next, &Current, true);
	TestFalse(TEXT("The same ticket is not fresh"), Same.bFresh);
	TestFalse(TEXT("An unchanged decision never reissues an order"), Same.bChanged);
	Next.CommittedUntil = 50.f;
	TestTrue(TEXT("A new deadline is a fresh commitment"), NewCommitment(&Current, Next));
	TestFalse(TEXT("A fresh commitment keeps a matching order"), OrderChange(Next, &Current, false).bChanged);
	TestTrue(TEXT("A fresh commitment reissues a differing order"), OrderChange(Next, &Current, true).bChanged);
	Next = Current;
	Next.Target = 1;
	const FOrderChange Retarget = OrderChange(Next, &Current, true);
	TestFalse(TEXT("A new target under the same ticket is not fresh"), Retarget.bFresh);
	TestTrue(TEXT("A new target reissues a differing order"), Retarget.bChanged);
	Next = Current;
	Next.TargetIdentity = 9;
	TestTrue(TEXT("A new structure target counts as a decision change"), OrderChange(Next, &Current, true).bChanged);
	const FOrderChange First = OrderChange(Current, nullptr, true);
	TestTrue(TEXT("Without a record the decision is fresh"), First.bFresh && First.bChanged);
	TestFalse(TEXT("Without a record a matching order stays"), OrderChange(Current, nullptr, false).bChanged);

	JevPlanner::FPlan Held = Current;
	Held.bEscalated = true;
	TestFalse(TEXT("No record, no escalation"), IsEscalation(nullptr, Held));
	TestFalse(TEXT("A plain plan is not an escalation"), IsEscalation(&Current, Current));
	TestTrue(TEXT("Becoming escalated is an escalation"), IsEscalation(&Current, Held));
	TestFalse(TEXT("Staying escalated on the same target is not"), IsEscalation(&Held, Held));
	JevPlanner::FPlan Moved = Held;
	Moved.Target = 1;
	TestTrue(TEXT("Escalating onto a new target is"), IsEscalation(&Held, Moved));
	return true;
}

bool FJevExecutionPublicationTest::RunTest(const FString&)
{
	using namespace JevExecution;
	JevPlanner::FPlan Display = Plan(JevPlanner::EVerb::Attack, 2, 30.f);
	Display.EtaSeconds = 20.f;
	Display.SizeBand = 4;
	FPublished Existing;
	Existing.Ticket = 7;
	Existing.Verb = JevPlanner::EVerb::Attack;
	Existing.Target = 2;
	Existing.EtaSeconds = 20.f;
	Existing.SizeBand = 4;
	FPublicationChange Change = PublicationChange(&Existing, 7, Display);
	TestFalse(TEXT("An identical row keeps its ETA"), Change.bEtaRestarted);
	TestFalse(TEXT("An identical row keeps its memo"), Change.bMemoChanged);
	Display.SizeBand = 6;
	Change = PublicationChange(&Existing, 7, Display);
	TestFalse(TEXT("A size-band change alone keeps the ETA running"), Change.bEtaRestarted);
	TestTrue(TEXT("A size-band change rewrites the memo"), Change.bMemoChanged);
	Display.SizeBand = 4;
	TestTrue(TEXT("A new ticket restarts the ETA and memo"),
		PublicationChange(&Existing, 8, Display).bEtaRestarted && PublicationChange(&Existing, 8, Display).bMemoChanged);
	JevPlanner::FPlan Retreating = Display;
	Retreating.Verb = JevPlanner::EVerb::Retreat;
	TestTrue(TEXT("A verb change restarts the ETA"), PublicationChange(&Existing, 7, Retreating).bEtaRestarted);
	JevPlanner::FPlan Retargeted = Display;
	Retargeted.Target = 1;
	TestTrue(TEXT("A target change restarts the ETA"), PublicationChange(&Existing, 7, Retargeted).bEtaRestarted);
	JevPlanner::FPlan Slower = Display;
	Slower.EtaSeconds = 21.f;
	TestTrue(TEXT("A new ETA restarts the ETA clock"), PublicationChange(&Existing, 7, Slower).bEtaRestarted);
	JevPlanner::FPlan Escalated = Display;
	Escalated.bEscalated = true;
	TestTrue(TEXT("An escalation flip restarts the ETA"), PublicationChange(&Existing, 7, Escalated).bEtaRestarted);
	Change = PublicationChange(nullptr, 7, Display);
	TestTrue(TEXT("A first publication starts the ETA and memo"), Change.bEtaRestarted && Change.bMemoChanged);
	return true;
}

bool FJevExecutionPlansTest::RunTest(const FString&)
{
	using namespace JevExecution;
	using JevPlanner::EVerb;
	const JevPlanner::FWorld World = LineWorld();
	const float Speeds[] = { 100.f };
	JevPlanner::FForce Force;
	Force.Source = Force.Home = 0;
	Force.UnitCount = 5;
	Force.ClassSpeeds = Speeds;

	const JevPlanner::FPlan Attack = ActualPlan(World, Force, nullptr, 100.f, EVerb::Attack, 2, 0);
	TestEqual(TEXT("Actual Attack ETA is the march time"), Attack.EtaSeconds, 20.f);
	TestEqual(TEXT("Actual plan takes the force's size band"), Attack.SizeBand, JevPlanner::SizeBand(5));
	TestTrue(TEXT("Actual Attack on an unowned region requires it unowned"), Attack.bRequiresUnownedTarget);
	TestEqual(TEXT("Without a record the deadline is a full commitment away"), Attack.CommittedUntil, 100.f + JevPlanner::CommitmentSeconds);
	const JevPlanner::FPlan Live = Plan(EVerb::Attack, 2, 110.f);
	TestEqual(TEXT("A live commitment keeps its deadline"),
		ActualPlan(World, Force, &Live, 100.f, EVerb::Attack, 2, 0).CommittedUntil, 110.f);
	TestEqual(TEXT("An expired commitment restarts the clock"),
		ActualPlan(World, Force, &Live, 110.f, EVerb::Attack, 2, 0).CommittedUntil, 110.f + JevPlanner::CommitmentSeconds);
	const JevPlanner::FPlan Retreat = ActualPlan(World, Force, nullptr, 100.f, EVerb::Retreat, 1, 0);
	TestEqual(TEXT("A retreat marches faster than the planner's estimate"), Retreat.EtaSeconds, 10.f / RetreatSpeedFactor);
	TestFalse(TEXT("A retreat never requires an unowned target"), Retreat.bRequiresUnownedTarget);
	TestFalse(TEXT("Holding an owned region does not require it unowned"),
		ActualPlan(World, Force, nullptr, 100.f, EVerb::MoveAndHold, 0, 0).bRequiresUnownedTarget);

	JevPlanner::FPlan Next = Plan(EVerb::Attack, 2, 125.f);
	Next.EtaSeconds = 20.f;
	Next.bEscalated = true;
	JevPlanner::FPlan Display = DisplayPlan(World, Force, Next, EVerb::Attack, 2, false);
	TestEqual(TEXT("An executing order shows the planned ETA"), Display.EtaSeconds, 20.f);
	TestFalse(TEXT("Escalation shows only while holding"), Display.bEscalated);
	Display = DisplayPlan(World, Force, Next, EVerb::MoveAndHold, 2, true);
	TestEqual(TEXT("A finished march shows the hold"), Display.Verb, EVerb::MoveAndHold);
	TestEqual(TEXT("A hold has no ETA"), Display.EtaSeconds, 0.f);
	TestTrue(TEXT("A held escalation stays escalated on its target"), Display.bEscalated);
	Display = DisplayPlan(World, Force, Next, EVerb::MoveAndHold, 1, true);
	TestFalse(TEXT("A hold on another region drops the escalation"), Display.bEscalated);
	Display = DisplayPlan(World, Force, Next, EVerb::Retreat, 1, false);
	TestEqual(TEXT("A displayed retreat uses the retreat speed"), Display.EtaSeconds, 10.f / RetreatSpeedFactor);
	TestEqual(TEXT("Display keeps the strategic deadline"), Display.CommittedUntil, 125.f);

	JevPlanner::FPlan Withdraw = Plan(EVerb::Retreat, 0, 125.f);
	AdoptRetreatRegion(World, Force, 1, Withdraw);
	TestEqual(TEXT("A retreat reports the region the force chose"), Withdraw.Target, 1);
	TestEqual(TEXT("A retreat reports its own ETA"), Withdraw.EtaSeconds, 10.f / RetreatSpeedFactor);
	AdoptRetreatRegion(World, Force, INDEX_NONE, Withdraw);
	TestEqual(TEXT("An unusable retreat region changes nothing"), Withdraw.Target, 1);
	JevPlanner::FPlan March = Plan(EVerb::Attack, 2, 125.f);
	AdoptRetreatRegion(World, Force, 1, March);
	TestEqual(TEXT("Only a retreat adopts the retreat region"), March.Target, 2);
	return true;
}

bool FJevExecutionSummaryTest::RunTest(const FString&)
{
	using namespace JevExecution;
	const JevRelease::FArmorCounts NoHumans;
	const EDamageType CatalogueDamage[RoleSlots] = { EDamageType::Kinetic, EDamageType::Piercing, EDamageType::Demolition,
		EDamageType::Piercing, EDamageType::EMP };
	const int32 Empty[RoleSlots] = {};
	TestEqual(TEXT("With no producers the first role is Frontline"), NextRoleSlot(Empty, NoHumans, CatalogueDamage), 0);
	const int32 FrontOnly[RoleSlots] = { 2, 0, 0, 0, 0 };
	TestEqual(TEXT("The first missing role is filled next"), NextRoleSlot(FrontOnly, NoHumans, CatalogueDamage), 1);
	const int32 NoSiege[RoleSlots] = { 1, 1, 0, 0, 0 };
	TestEqual(TEXT("Siege is filled once both main roles exist"), NextRoleSlot(NoSiege, NoHumans, CatalogueDamage), 2);
	const int32 FrontHeavy[RoleSlots] = { 2, 1, 1, 0, 0 };
	TestEqual(TEXT("Ranged catches up when Frontline outnumbers it"), NextRoleSlot(FrontHeavy, NoHumans, CatalogueDamage), 1);
	const int32 Even[RoleSlots] = { 1, 1, 1, 0, 0 };
	TestEqual(TEXT("Ties go to Frontline"), NextRoleSlot(Even, NoHumans, CatalogueDamage), 0);
	const int32 Extras[RoleSlots] = { 0, 1, 1, 1, 1 };
	TestEqual(TEXT("A missing base role is still filled when the new roles exist"), NextRoleSlot(Extras, NoHumans, CatalogueDamage), 0);

	TestTrue(TEXT("A 5:4 edge with a full squad and equal income is an advantage"), HasAdvantage(5, 4, 5, 3, 3));
	TestFalse(TEXT("A narrower edge is not"), HasAdvantage(5, 5, 5, 3, 3));
	TestFalse(TEXT("Less than a squad is not"), HasAdvantage(4, 0, 5, 3, 3));
	TestTrue(TEXT("Against no enemies a squad is an advantage"), HasAdvantage(5, 0, 5, 3, 3));
	TestFalse(TEXT("An income deficit forfeits the advantage"), HasAdvantage(10, 1, 5, 2, 3));

	TestTrue(TEXT("Below the entry health a force recovers"), Recovering(.34f, false));
	TestFalse(TEXT("The entry threshold itself does not start recovery"), Recovering(RecoveryEnterHealth, false));
	TestTrue(TEXT("Recovery holds above the entry threshold"), Recovering(.5f, true));
	TestFalse(TEXT("Recovery ends at the exit threshold"), Recovering(RecoveryExitHealth, true));
	TestFalse(TEXT("A mid-health force that was not recovering keeps fighting"), Recovering(.5f, false));

	JevPlanner::FWorld World = LineWorld();
	TestFalse(TEXT("A quiet map is not a threat"), IsThreatened(World));
	World.Regions[2].Hostiles = 3;
	TestFalse(TEXT("Hostiles in an unowned region are not a threat"), IsThreatened(World));
	World.Regions[0].Hostiles = 1;
	TestTrue(TEXT("Hostiles in an owned region are"), IsThreatened(World));
	World.Regions[0].Hostiles = 0;
	World.Regions[0].bAttacked = true;
	TestTrue(TEXT("An attacked owned region is"), IsThreatened(World));

	TestEqual(TEXT("Deposit rate outweighs distance"), DepositScore(3, 4000.0), 3 * DepositRateWeight - 2.f);
	TestTrue(TEXT("A nearer deposit of equal rate scores higher"), DepositScore(3, 1000.0) > DepositScore(3, 3000.0));
	return true;
}

bool FJevExecutionForwardRegionTest::RunTest(const FString&)
{
	using namespace JevExecution;
	JevPlanner::FWorld World = LineWorld();
	const FVector EnemyHome = World.Regions[3].Position;
	TestEqual(TEXT("Nothing forward is available without controlled regions"), ForwardRegion(World, EnemyHome, 0, 0), static_cast<int32>(INDEX_NONE));
	World.Regions[1].Controller = 5;
	World.Regions[2].Controller = 5;
	TestEqual(TEXT("The controlled region nearest the enemy home wins"), ForwardRegion(World, EnemyHome, 0, 0), 2);
	TestEqual(TEXT("A contested region is skipped"), ForwardRegion(World, EnemyHome, uint64(1) << 2, 0), 1);
	TestEqual(TEXT("A region that already has a producer is skipped"), ForwardRegion(World, EnemyHome, 0, uint64(1) << 2), 1);
	TestEqual(TEXT("No uncontested, producer-free region means no forward build"),
		ForwardRegion(World, EnemyHome, uint64(1) << 2, uint64(1) << 1), static_cast<int32>(INDEX_NONE));
	World.Regions[2].Controller = 0;
	TestEqual(TEXT("Enemy-held regions are not candidates"), ForwardRegion(World, EnemyHome, 0, 0), 1);
	return true;
}
#endif
