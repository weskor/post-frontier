#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/BranchPolicy.h"

// Pure rule tests: no world, no actors. Eligibility, the upgrade timer and the production pause, the
// per-battle reset, the refit queue and what a refit keeps, and the panel and card texts.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBranchEligibilityTest, "CoopRTS.Rules.Branch.Eligibility",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBranchPreconditionsTest, "CoopRTS.Rules.Branch.Preconditions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBranchUpgradeTest, "CoopRTS.Rules.Branch.Upgrade",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBranchRefitQueueTest, "CoopRTS.Rules.Branch.RefitQueue",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBranchDurabilityTest, "CoopRTS.Rules.Branch.Durability",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBranchTextTest, "CoopRTS.Rules.Branch.Text",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
using namespace BranchPolicy;

// A finished, locked, idle Barracks of the buyer with exactly the price in the wallet.
FInput Ready()
{
	FInput In;
	In.bBattleLive = true;
	In.bOwner = true;
	In.bProducer = true;
	In.bBuilt = true;
	In.bTypeLocked = true;
	In.bBranchExists = true;
	In.Power = PowerCost;
	In.Data = DataCost;
	return In;
}

EVerdict Verdict(const FInput& In)
{
	return Evaluate(In).Verdict;
}

FString Reason(const FInput& In)
{
	TStringBuilder<96> Text;
	AppendReason(Text, Evaluate(In));
	return FString(Text.ToView());
}

TArray<FMember> Members(std::initializer_list<TPair<int32, int32>> SlotsAndForms)
{
	TArray<FMember> Out;
	for (const TPair<int32, int32>& Pair : SlotsAndForms)
		Out.Add({ Pair.Key, Pair.Value });
	return Out;
}
}

bool FBranchEligibilityTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A battle's start state offers the branch: nothing bought, so the purchase is open again every battle"),
		FInput().Phase, EBranchPhase::None);
	TestEqual(TEXT("A locked, finished, idle building with the exact price is accepted"), Verdict(Ready()), EVerdict::Accepted);

	FInput Unlocked = Ready();
	Unlocked.bTypeLocked = false;
	TestEqual(TEXT("The type must be locked first"), Verdict(Unlocked), EVerdict::TypeNotLocked);
	TestEqual(TEXT("and the reason says so"), Reason(Unlocked), FString(TEXT("Lock a type first")));

	FInput Upgrading = Ready();
	Upgrading.Phase = EBranchPhase::Upgrading;
	TestEqual(TEXT("A running upgrade refuses a second purchase"), Verdict(Upgrading), EVerdict::Upgrading);
	FInput Bought = Ready();
	Bought.Phase = EBranchPhase::Done;
	TestEqual(TEXT("A finished upgrade refuses another: once per battle per building"), Verdict(Bought), EVerdict::AlreadyBought);

	FInput Stunned = Ready();
	Stunned.bStunned = true;
	TestEqual(TEXT("A stunned building cannot start an upgrade"), Verdict(Stunned), EVerdict::Stunned);

	FInput NoData = Ready();
	NoData.Data = DataCost - 1;
	const FDecision NoDataDecision = Evaluate(NoData);
	TestEqual(TEXT("One Data short is refused"), NoDataDecision.Verdict, EVerdict::NeedResources);
	TestTrue(TEXT("and names exactly the missing Data"), NoDataDecision.DataShort == 1 && NoDataDecision.PowerShort == 0);
	TestEqual(TEXT("with the panel's wording"), Reason(NoData), FString(TEXT("Need 1 more Data")));
	FInput NoPower = Ready();
	NoPower.Power = 70;
	TestEqual(TEXT("A Power shortfall reads in Power"), Reason(NoPower), FString(TEXT("Need 30 more Power")));
	FInput Neither = Ready();
	Neither.Power = 0;
	Neither.Data = 0;
	TestEqual(TEXT("Missing both names both, so the click explains the whole gap"), Reason(Neither),
		FString(TEXT("Need 100 more Power and 50 more Data")));
	FInput Rich = Ready();
	Rich.Power = 5000;
	Rich.Data = 5000;
	TestTrue(TEXT("More than the price is accepted"), Evaluate(Rich).IsAccepted());
	return true;
}

bool FBranchPreconditionsTest::RunTest(const FString& Parameters)
{

	FInput Foreign = Ready();
	Foreign.bOwner = false;
	TestEqual(TEXT("Only the owner buys"), Verdict(Foreign), EVerdict::NotOwner);
	FInput Idle = Ready();
	Idle.bBattleLive = false;
	TestEqual(TEXT("No live battle"), Verdict(Idle), EVerdict::NoBattle);
	FInput Hut = Ready();
	Hut.bProducer = false;
	TestEqual(TEXT("Only production buildings upgrade"), Verdict(Hut), EVerdict::NotProducer);
	FInput Scaffold = Ready();
	Scaffold.bBuilt = false;
	TestEqual(TEXT("An unfinished or dead building does not"), Verdict(Scaffold), EVerdict::NotBuilt);
	FInput Bare = Ready();
	Bare.bBranchExists = false;
	TestEqual(TEXT("A type without a branch has none to buy"), Verdict(Bare), EVerdict::NoBranch);

	FInput Everything = Ready();
	Everything.bBattleLive = true;
	Everything.bTypeLocked = false;
	Everything.bStunned = true;
	Everything.Phase = EBranchPhase::Done;
	Everything.Power = 0;
	Everything.Data = 0;
	TestEqual(TEXT("The lock fails before the purchase state, the stun and the wallet"), Verdict(Everything), EVerdict::TypeNotLocked);
	Everything.bTypeLocked = true;
	TestEqual(TEXT("A finished purchase fails before the stun and the wallet"), Verdict(Everything), EVerdict::AlreadyBought);
	Everything.Phase = EBranchPhase::None;
	TestEqual(TEXT("The stun fails before the wallet"), Verdict(Everything), EVerdict::Stunned);
	return true;
}

bool FBranchUpgradeTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Production waits while upgrading"), PausesProduction(EBranchPhase::Upgrading));
	TestFalse(TEXT("and only then: before the purchase"), PausesProduction(EBranchPhase::None));
	TestFalse(TEXT("and after it completes, branched"), PausesProduction(EBranchPhase::Done));

	FUpgradeStep Step{ 0.f, false };
	for (int32 Second = 0; Second < 19; ++Second)
		Step = Advance(Step.Progress, 1.f, false);
	TestTrue(TEXT("19 s of ticks leave the upgrade running"), !Step.bCompleted && FMath::IsNearlyEqual(Step.Progress, 19.f));
	Step = Advance(Step.Progress, 1.f, false);
	TestTrue(TEXT("The twentieth second completes it"), Step.bCompleted && Step.Progress == UpgradeSeconds);
	Step = Advance(Step.Progress, 5.f, false);
	TestEqual(TEXT("Progress never passes the duration"), Step.Progress, UpgradeSeconds);

	const FUpgradeStep Frozen = Advance(7.f, 3.f, true);
	TestTrue(TEXT("A stunned building's upgrade does not advance"), Frozen.Progress == 7.f && !Frozen.bCompleted);
	TestEqual(TEXT("A negative or non-finite step changes nothing"), Advance(7.f, -1.f, false).Progress, 7.f);
	TestEqual(TEXT("Non-finite elapsed time changes nothing"), Advance(7.f, std::numeric_limits<float>::quiet_NaN(), false).Progress, 7.f);
	TestTrue(TEXT("One long frame still completes exactly once"), Advance(0.f, 90.f, false).bCompleted);
	return true;
}

bool FBranchRefitQueueTest::RunTest(const FString& Parameters)
{
	constexpr int32 Base = 0, Branch = 5;
	const TArray<FMember> Mixed = Members({ { 3, Base }, { 1, Base }, { 4, Branch }, { 2, Base } });
	TestEqual(TEXT("The lowest composition slot in the base form goes first, whatever the array order"), NextRefit(Mixed, Base), 1);

	const TArray<FMember> AfterOne = Members({ { 3, Base }, { 1, Branch }, { 4, Branch }, { 2, Base } });
	TestEqual(TEXT("A refitted member leaves the queue: the next slot follows"), NextRefit(AfterOne, Base), 2);
	const TArray<FMember> Done = Members({ { 1, Branch }, { 2, Branch } });
	TestEqual(TEXT("A fully branched force has nothing to refit"), NextRefit(Done, Base), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("An empty force has nothing to refit"), NextRefit(TConstArrayView<FMember>(), Base), static_cast<int32>(INDEX_NONE));
	const TArray<FMember> Other = Members({ { 0, 9 } });
	TestEqual(TEXT("A member of neither form is left alone"), NextRefit(Other, Base), static_cast<int32>(INDEX_NONE));

	const FRefitProgress Part = RefitProgress(AfterOne, Branch);
	TestTrue(TEXT("Progress counts the branched members out of the living ones"), Part.Branched == 2 && Part.Living == 4 && !Part.IsComplete());
	TestTrue(TEXT("A casualty in the base form moves it toward done"),
		RefitProgress(Members({ { 1, Branch }, { 4, Branch } }), Branch).IsComplete());
	TestTrue(TEXT("A new branched recruit raises both counts"), RefitProgress(Members({ { 1, Branch }, { 2, Base }, { 3, Branch } }), Branch).Branched == 2);
	return true;
}

bool FBranchDurabilityTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A full-health Brawler refits to the Warden's full health"), ScaleDurability(330, 330, 429), 429);
	TestEqual(TEXT("Half health stays half: 165 of 330 is 214.5 of 429, rounded to the nearest"), ScaleDurability(165, 330, 429), 215);
	TestEqual(TEXT("A quarter stays a quarter"), ScaleDurability(80, 320, 416), 104);
	TestEqual(TEXT("A unit that is alive stays alive: 1 HP of 330 scales to at least 1"), ScaleDurability(1, 330, 429), 1);
	TestEqual(TEXT("A shield refits the same way: 40 of 80 to 60 of 120"), ScaleDurability(40, 80, 120), 60);
	TestEqual(TEXT("A depleted shield stays empty"), ScaleDurability(0, 80, 120), 0);
	TestEqual(TEXT("A unit that gains a shield from nothing starts at nothing"), ScaleDurability(0, 0, 120), 0);
	TestEqual(TEXT("Never above the new maximum"), ScaleDurability(500, 330, 429), 429);
	return true;
}

bool FBranchTextTest::RunTest(const FString& Parameters)
{
	TStringBuilder<128> Text;
	AppendBranchTitle(Text, TEXT("Marksman"), TEXT("+20% range"));
	TestEqual(TEXT("The button's first line is the branch and its effect from data"), FString(Text.ToView()),
		FString(TEXT("MARKSMAN +20% range")));
	Text.Reset();
	AppendPrice(Text);
	TestEqual(TEXT("and its second line the price: 100 Power + 50 Data and 20 s"), FString(Text.ToView()),
		FString(TEXT("100 Power + 50 Data \u00B7 20 s")));
	Text.Reset();
	AppendUpgradeText(Text, TEXT("Marksman"), 12.9f);
	TestEqual(TEXT("The bar text counts whole elapsed seconds"), FString(Text.ToView()), FString(TEXT("Upgrading to Marksman 12 / 20 s")));
	Text.Reset();
	AppendDoneText(Text, TEXT("Marksman"), TEXT("+20% range"));
	TestEqual(TEXT("The done state is a check mark and the effect"), FString(Text.ToView()), FString(TEXT("\u2713 MARKSMAN +20% range")));
	Text.Reset();
	AppendRefitChip(Text, { 2, 5 });
	TestEqual(TEXT("The force card chip"), FString(Text.ToView()), FString(TEXT("REFIT 2/5")));
	Text.Reset();
	AppendRefitLine(Text, { 2, 5 }, TEXT("Marksman"));
	TestEqual(TEXT("and its line say cut-off members keep the old form"), FString(Text.ToView()),
		FString(TEXT("Refit 2/5 \u2192 Marksman \u00B7 cut-off members keep old form")));
	return true;
}
#endif
