#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/JevIntent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevIntentCountdownTest, "CoopRTS.Rules.JevIntent.Countdown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevIntentTimelineTest, "CoopRTS.Rules.JevIntent.Timeline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevIntentBadgesTest, "CoopRTS.Rules.JevIntent.Badges",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevIntentMemoPostingTest, "CoopRTS.Rules.JevIntent.MemoPosting",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevIntentMemoLifetimeTest, "CoopRTS.Rules.JevIntent.MemoLifetime",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
JevIntent::FPlanView Plan(int32 Ticket, uint32 Force, int32 Target, float Eta, float IssuedAt = 0.f)
{
	JevIntent::FPlanView View;
	View.Ticket = Ticket;
	View.Force = Force;
	View.ForceNumber = static_cast<int32>(Force);
	View.Target = Target;
	View.EtaSeconds = Eta;
	View.EtaIssuedAt = IssuedAt;
	View.Memo = TEXT("memo");
	return View;
}

FString Countdown(float Seconds)
{
	TStringBuilder<16> Text;
	JevIntent::AppendCountdown(Text, Seconds);
	return FString(Text.ToView());
}
}

bool FJevIntentCountdownTest::RunTest(const FString&)
{
	const JevIntent::FPlanView Marching = Plan(1, 1, 2, 40.f, 100.f);
	TestEqual(TEXT("A plan has its full ETA at the moment it was computed"), JevIntent::EtaRemaining(Marching, 100.f), 40.f);
	TestEqual(TEXT("The countdown runs from the moment the ETA was computed, not from receipt"),
		JevIntent::EtaRemaining(Marching, 112.5f), 27.5f);
	TestEqual(TEXT("The countdown stops at zero"), JevIntent::EtaRemaining(Marching, 500.f), 0.f);
	TestEqual(TEXT("A clock behind the issue time never extends the ETA"), JevIntent::EtaRemaining(Marching, 90.f), 40.f);
	TestEqual(TEXT("Whole minutes and padded seconds"), Countdown(65.f), FString(TEXT("1:05")));
	TestEqual(TEXT("Fractions round up as the memo templates do"), Countdown(29.01f), FString(TEXT("0:30")));
	TestEqual(TEXT("Zero reads 0:00"), Countdown(0.f), FString(TEXT("0:00")));
	TestEqual(TEXT("Negative input clamps to zero"), Countdown(-3.f), FString(TEXT("0:00")));
	return true;
}

bool FJevIntentTimelineTest::RunTest(const FString&)
{
	const JevIntent::FPlanView Plans[] = {
		Plan(30, 3, 1, 50.f), Plan(10, 1, 2, 20.f), Plan(20, 2, 3, 20.f), Plan(40, 4, 1, 5.f, -10.f)
	};
	JevIntent::FTimeline Timeline;
	JevIntent::BuildTimeline(Plans, 0.f, Timeline);
	if (!TestEqual(TEXT("Every published plan has exactly one entry"), Timeline.Num(), 4))
		return false;
	TestEqual(TEXT("A plan whose ETA elapsed sorts first at zero"), Timeline[0].Ticket, 40);
	TestEqual(TEXT("Equal countdowns order by ticket"), Timeline[1].Ticket, 10);
	TestEqual(TEXT("Equal countdowns order by ticket (second)"), Timeline[2].Ticket, 20);
	TestEqual(TEXT("The latest arrival is last"), Timeline[3].Ticket, 30);
	TestEqual(TEXT("Entries carry the plan's target"), Timeline[3].Target, 1);
	TestEqual(TEXT("Entries carry the live countdown"), Timeline[1].Seconds, 20.f);
	JevIntent::BuildTimeline(TConstArrayView<JevIntent::FPlanView>(), 0.f, Timeline);
	TestEqual(TEXT("No plans, no entries"), Timeline.Num(), 0);
	return true;
}

bool FJevIntentBadgesTest::RunTest(const FString&)
{
	JevIntent::FPlanView Late = Plan(5, 1, 4, 60.f);
	Late.Verb = JevPlanner::EVerb::Attack;
	JevIntent::FPlanView Soon = Plan(6, 2, 4, 12.f);
	Soon.Verb = JevPlanner::EVerb::MoveAndHold;
	JevIntent::FPlanView Defender = Plan(7, 3, 2, 0.f);
	Defender.bEscalated = true;
	const JevIntent::FPlanView Plans[] = { Late, Soon, Defender };
	JevIntent::FBadges Badges;
	JevIntent::BuildBadges(Plans, 0.f, Badges);
	if (!TestEqual(TEXT("One badge per targeted region"), Badges.Num(), 2))
		return false;
	TestEqual(TEXT("Badges ascend by region"), Badges[0].Region, 2);
	TestTrue(TEXT("A region with a defending plan is escalated"), Badges[0].bEscalated);
	TestEqual(TEXT("A region two forces target shows the sooner arrival"), Badges[1].Seconds, 12.f);
	TestEqual(TEXT("The badge names the sooner plan"), Badges[1].Ticket, 6);
	TestTrue(TEXT("The badge verb follows the sooner plan"), Badges[1].Verb == JevPlanner::EVerb::MoveAndHold);
	TestEqual(TEXT("The badge counts every plan targeting the region"), Badges[1].Plans, 2);
	TestFalse(TEXT("Inbound plans alone are not escalated"), Badges[1].bEscalated);
	JevIntent::FPlanView Untargeted = Plan(8, 4, INDEX_NONE, 3.f);
	JevIntent::BuildBadges(MakeArrayView(&Untargeted, 1), 0.f, Badges);
	TestEqual(TEXT("A plan without a target region badges nothing"), Badges.Num(), 0);
	return true;
}

bool FJevIntentMemoPostingTest::RunTest(const FString&)
{
	JevIntent::FMemoFeed Feed;
	JevIntent::FPlanView Attack = Plan(1, 10, 3, 40.f);
	Attack.Verb = JevPlanner::EVerb::Attack;
	Attack.Memo = TEXT("Ticket #1 attack");
	JevIntent::FPlanView Hold = Plan(2, 20, 4, 25.f);
	Hold.Memo = TEXT("Ticket #2 hold");
	TArray<JevIntent::FPlanView> Plans = { Attack, Hold };
	TestEqual(TEXT("Every new plan posts its memo"), Feed.Observe(Plans, 1.f), 2);
	TestEqual(TEXT("An unchanged list posts nothing"), Feed.Observe(Plans, 2.f), 0);

	Plans[0].EtaSeconds = 22.f;
	Plans[0].EtaIssuedAt = 18.f;
	TestEqual(TEXT("ETA drift alone is not a new announcement"), Feed.Observe(Plans, 18.f), 0);

	Plans[1].bEscalated = true;
	Plans[1].Target = 20;
	Plans[1].Memo = TEXT("Ticket #2 Escalated: defending");
	TestEqual(TEXT("Escalation under the same ticket posts the changed memo"), Feed.Observe(Plans, 20.f), 1);
	TestEqual(TEXT("The escalated memo is the plan's memo, verbatim"), Feed.History().Last().Text, FString(TEXT("Ticket #2 Escalated: defending")));
	TestEqual(TEXT("The escalated memo keeps its ticket"), Feed.History().Last().Ticket, 2);

	Plans[0].Ticket = 3;
	Plans[0].Target = 5;
	Plans[0].Memo = TEXT("Ticket #3 attack");
	TestEqual(TEXT("A replacement ticket for the same force posts its memo"), Feed.Observe(Plans, 22.f), 1);

	Plans[0].SizeBand = 4;
	Plans[0].Memo = TEXT("Ticket #3 attack ~4 units");
	TestEqual(TEXT("A size-band change posts the memo that names the new size"), Feed.Observe(Plans, 24.f), 1);

	Plans.RemoveAt(1);
	TestEqual(TEXT("A plan that disappears posts nothing"), Feed.Observe(Plans, 26.f), 0);
	Plans.Add(Hold);
	TestEqual(TEXT("A force that publishes again after disappearing is new"), Feed.Observe(Plans, 28.f), 1);

	JevIntent::FPlanView Silent = Plan(9, 30, 6, 10.f);
	Silent.Memo = FStringView();
	Plans.Add(Silent);
	TestEqual(TEXT("A plan without a memo posts nothing"), Feed.Observe(Plans, 30.f), 0);
	Silent.Memo = TEXT("late memo");
	Plans.Last() = Silent;
	TestEqual(TEXT("A memo arriving for an already-seen announcement is not retro-posted"), Feed.Observe(Plans, 31.f), 0);
	return true;
}

bool FJevIntentMemoLifetimeTest::RunTest(const FString&)
{
	JevIntent::FMemoFeed Feed;
	TArray<JevIntent::FPlanView> Plans;
	TArray<FString> Texts;
	for (int32 Index = 0; Index < JevIntent::MemoHistory + 2; ++Index)
	{
		Texts.Add(FString::Printf(TEXT("memo %d"), Index));
		Plans.Reset();
		JevIntent::FPlanView View = Plan(Index + 1, 1, 2, 10.f);
		View.Memo = Texts.Last();
		Plans.Add(View);
		Feed.Observe(Plans, Index * 1.f);
	}
	TestEqual(TEXT("History is bounded"), Feed.History().Num(), JevIntent::MemoHistory);
	TestEqual(TEXT("The oldest memos drop first"), Feed.History()[0].Text, Texts[2]);

	JevIntent::FVisibleMemos Visible;
	const float Posted = JevIntent::MemoHistory + 1.f;
	Feed.Visible(Posted, Visible);
	if (!TestEqual(TEXT("At most MemoVisible memos show"), Visible.Num(), JevIntent::MemoVisible))
		return false;
	TestEqual(TEXT("Newest first"), Visible[0].Memo->Text, Texts.Last());
	TestEqual(TEXT("A fresh memo is opaque"), Visible[0].Alpha, 1.f);
	Feed.Visible(Posted + JevIntent::MemoHoldSeconds - .5f, Visible);
	TestEqual(TEXT("A memo holds at full opacity for its hold time"), Visible[0].Alpha, 1.f);
	Feed.Visible(Posted + JevIntent::MemoHoldSeconds + JevIntent::MemoFadeSeconds * .5f, Visible);
	TestEqual(TEXT("It is half faded midway through the fade"), Visible[0].Alpha, .5f);
	Feed.Visible(Posted + JevIntent::MemoHoldSeconds + JevIntent::MemoFadeSeconds, Visible);
	TestEqual(TEXT("A memo is gone once its fade ends"), Visible.Num(), 0);
	Feed.Reset();
	TestEqual(TEXT("Reset forgets the history"), Feed.History().Num(), 0);
	return true;
}
#endif
