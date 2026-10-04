#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/JevIntent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevIntentCountdownTest, "CoopRTS.Rules.JevIntent.Countdown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevIntentTimelineTest, "CoopRTS.Rules.JevIntent.Timeline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevIntentReleaseCellTest, "CoopRTS.Rules.JevIntent.ReleaseCell",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevIntentReleaseTextTest, "CoopRTS.Rules.JevIntent.ReleaseText",
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

// JEV's schedule as a client reads it: the clock started at server time 1000, so Now 1090 is match second 90.
JevIntent::FReleaseView Schedule(int32 Next, float NextAt)
{
	JevIntent::FReleaseView View;
	View.bKnown = true;
	View.Current = Next - 1;
	View.Next = Next;
	View.NextAt = NextAt;
	View.ClockStartServerTime = 1000.f;
	return View;
}

FString Text(void (*Append)(FStringBuilderBase&, int32, EArmorClass), int32 Release, EArmorClass Armor = EArmorClass::Unset)
{
	TStringBuilder<64> Out;
	Append(Out, Release, Armor);
	return FString(Out.ToView());
}

FString Empty(const JevIntent::FReleaseView& Release, float Now)
{
	TStringBuilder<96> Out;
	JevIntent::AppendEmptyTimeline(Out, Release, Now);
	return FString(Out.ToView());
}

FString Elapsed(float Seconds)
{
	TStringBuilder<16> Out;
	JevIntent::AppendElapsed(Out, Seconds);
	return FString(Out.ToView());
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
	JevIntent::BuildTimeline(Plans, JevIntent::FReleaseView(), 0.f, Timeline);
	if (!TestEqual(TEXT("Every published plan has exactly one entry"), Timeline.Num(), 4))
		return false;
	TestEqual(TEXT("A plan whose ETA elapsed sorts first at zero"), Timeline[0].Ticket, 40);
	TestEqual(TEXT("Equal countdowns order by ticket"), Timeline[1].Ticket, 10);
	TestEqual(TEXT("Equal countdowns order by ticket (second)"), Timeline[2].Ticket, 20);
	TestEqual(TEXT("The latest arrival is last"), Timeline[3].Ticket, 30);
	TestEqual(TEXT("Entries carry the plan's target"), Timeline[3].Target, 1);
	TestEqual(TEXT("Entries carry the live countdown"), Timeline[1].Seconds, 20.f);
	JevIntent::BuildTimeline(TConstArrayView<JevIntent::FPlanView>(), JevIntent::FReleaseView(), 0.f, Timeline);
	TestEqual(TEXT("No plans and no release, no entries"), Timeline.Num(), 0);
	return true;
}

bool FJevIntentReleaseCellTest::RunTest(const FString&)
{
	const JevIntent::FPlanView Plans[] = { Plan(30, 3, 1, 50.f, 1090.f), Plan(10, 1, 2, 20.f, 1090.f), Plan(20, 2, 3, 5.f, 1090.f) };
	JevIntent::FTimeline Timeline;
	const JevIntent::FReleaseView Early = Schedule(1, 120.f);
	JevIntent::BuildTimeline(Plans, Early, 1089.9f, Timeline);
	TestEqual(TEXT("v1.1 is not a cell 30.1 s ahead: the plans stand alone"), Timeline.Num(), 3);
	JevIntent::BuildTimeline(Plans, Early, 1090.f, Timeline);
	if (!TestEqual(TEXT("v1.1 becomes a cell exactly 30 s ahead"), Timeline.Num(), 4))
		return false;
	TestTrue(TEXT("The release cell is pinned first even though a plan arrives sooner"), Timeline[0].Kind == JevIntent::EEntryKind::Release);
	TestEqual(TEXT("The release cell counts down to the release"), Timeline[0].Seconds, 30.f);
	TestEqual(TEXT("The cell names its release"), Timeline[0].Release, 1);
	TestEqual(TEXT("Plans keep their own order after the release (soonest first)"), Timeline[1].Ticket, 20);
	TestEqual(TEXT("Plans keep their own order after the release (second)"), Timeline[2].Ticket, 10);
	TestEqual(TEXT("Plans keep their own order after the release (last)"), Timeline[3].Ticket, 30);
	JevIntent::BuildTimeline(Plans, Early, 1119.f, Timeline);
	TestEqual(TEXT("One second out the cell reads 1 s"), Timeline[0].Seconds, 1.f);
	JevIntent::BuildTimeline(Plans, Early, 1120.5f, Timeline);
	TestTrue(TEXT("Half a second past the release the cell reads 0 while the schedule catches up"),
		Timeline[0].Kind == JevIntent::EEntryKind::Release && Timeline[0].Seconds == 0.f);
	JevIntent::BuildTimeline(Plans, Early, 1121.5f, Timeline);
	TestEqual(TEXT("A second past the release the cell is gone"), Timeline.Num(), 3);
	JevIntent::BuildTimeline(Plans, Schedule(2, 240.f), 1120.5f, Timeline);
	TestEqual(TEXT("Once the schedule moves to v1.2 its cell waits for its own 30 s window"), Timeline.Num(), 3);
	JevIntent::FReleaseView Unknown;
	JevIntent::BuildTimeline(Plans, Unknown, 1100.f, Timeline);
	TestEqual(TEXT("Before the schedule replicates there is no release cell"), Timeline.Num(), 3);
	return true;
}

bool FJevIntentReleaseTextTest::RunTest(const FString&)
{
	const JevIntent::FReleaseView Quiet = Schedule(1, 120.f);
	TestEqual(TEXT("Empty timeline names the next release and its countdown"), Empty(Quiet, 1018.f),
		FString(TEXT("No JEV plans \u00B7 next release v1.1 in 1:42")));
	TestEqual(TEXT("The countdown rounds up as every countdown does"), Empty(Quiet, 1018.5f),
		FString(TEXT("No JEV plans \u00B7 next release v1.1 in 1:42")));
	TestEqual(TEXT("A later schedule names its own release"), Empty(Schedule(3, 360.f), 1300.f),
		FString(TEXT("No JEV plans \u00B7 next release v2.0 in 1:00")));
	TestEqual(TEXT("Overrun releases are named as such"), Empty(Schedule(6, 660.f), 1600.f),
		FString(TEXT("No JEV plans \u00B7 next release overrun in 1:00")));
	TestEqual(TEXT("Without a replicated schedule only the plans are mentioned"), Empty(JevIntent::FReleaseView(), 5.f),
		FString(TEXT("No JEV plans")));

	TestEqual(TEXT("v1.1 adds raids on Drill Rigs"), Text(JevIntent::AppendReleaseTag, 1),
		FString(TEXT("WAVE \u00B7 RAIDS DRILL RIGS")));
	TestEqual(TEXT("v1.2 counters the humans' most numerous armor"), Text(JevIntent::AppendReleaseTag, 2, EArmorClass::Heavy),
		FString(TEXT("COUNTERS HEAVY")));
	TestEqual(TEXT("v1.2 against shielded units"), Text(JevIntent::AppendReleaseTag, 2, EArmorClass::Shielded),
		FString(TEXT("COUNTERS SHIELDED")));
	TestEqual(TEXT("v1.2 with no humans to count says only ARMOR"), Text(JevIntent::AppendReleaseTag, 2),
		FString(TEXT("COUNTERS ARMOR")));
	TestEqual(TEXT("v2.0 sends every force"), Text(JevIntent::AppendReleaseTag, 3), FString(TEXT("ALL FORCES ATTACK")));
	TestEqual(TEXT("v2.1 speed comes from the release data"), Text(JevIntent::AppendReleaseTag, 4), FString(TEXT("WAVES +15% SPEED")));
	TestEqual(TEXT("Overrun"), Text(JevIntent::AppendReleaseTag, 9), FString(TEXT("OVERRUN")));
	TestEqual(TEXT("v1.0 adds nothing"), Text(JevIntent::AppendReleaseTag, 0), FString());

	TestEqual(TEXT("The clock counts up in minutes and seconds"), Elapsed(252.f), FString(TEXT("4:12")));
	TestEqual(TEXT("The clock rounds down, never ahead"), Elapsed(252.99f), FString(TEXT("4:12")));
	TestEqual(TEXT("The clock starts at zero"), Elapsed(0.f), FString(TEXT("0:00")));
	TestEqual(TEXT("Match seconds follow the published clock start"), JevIntent::MatchSeconds(Quiet, 1252.f), 252.f);
	TestEqual(TEXT("A clock start in the future reads zero"), JevIntent::MatchSeconds(Quiet, 990.f), 0.f);
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
