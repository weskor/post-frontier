#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "HAL/PlatformTime.h"
#include "HUD/HUDPanels.h"
#include "JevIntentFixture.h"
#include "JevIntentView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevIntentUiTest, "CoopRTS.HUD.JevIntent",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace CommandHUDPanels;
constexpr float ViewportWidth = 1280.f;
constexpr float ViewportHeight = 720.f;

bool Overlaps(const FRect& A, const FRect& B)
{
	return A.W > 0.f && B.W > 0.f && A.Intersects(B);
}
}

// The timeline, badges and memo feed are read back through the same functions the HUD draws
// with, and compared with the replicated plans as a fixture creates, escalates and replaces them.
class FJevIntentUiScenario : public IAutomationLatentCommand
{
public:
	explicit FJevIntentUiScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 40.)
		{
			Test->AddError(TEXT("JEV intent scenario exceeded 40 seconds."));
			return true;
		}
		UWorld* World = ArmyTestSetup::World();
		ACommandPlayerController* PC = World ? ArmyTestSetup::Controller(World) : nullptr;
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		UJevIntentFeed* Feed = UJevIntentFeed::Get(World);
		if (!PC || !Feed || !ArmyTestSetup::MapReady(State) || !IsValid(State->EnemyCommander)
			|| !PC->GetPlayerState<ACommandPlayerState>())
			return false;
		if (Stage == 0)
			return Create(World, *State, *Feed);
		if (Stage == 1)
			return Drift(World, *State, *Feed);
		if (Stage == 2)
			return Escalate(World, *State, *Feed);
		if (Stage == 3)
			return Replace(World, *State, *Feed);
		return Prune(World, *State, *Feed);
	}

private:
	bool Create(UWorld* World, ACommandGameState& State, UJevIntentFeed& Feed)
	{
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			It->Destroy();
		State.EnemyPlans.Reset();
		const FContext Context = MakeContext(ArmyTestSetup::Controller(World));
		BuildJevIntentModel(Context, Model);
		Check(Model.Timeline.IsEmpty() && Model.Badges.IsEmpty(), TEXT("Nothing is displayed before JEV publishes a plan"));
		const FRect Reserved = JevTimelineRect(Context, MakeLayout(Context, ViewportWidth, ViewportHeight), Model);
		Check(Reserved.W > 0.f && Reserved.H == 40.f && IsPanelPoint(Context, MakeLayout(Context, ViewportWidth, ViewportHeight), Reserved.Center()),
			TEXT("The timeline bar keeps its 40 px, and its clicks, without plans"));
		if (!Check(JevIntentFixture::Publish(World, State, JevIntentFixture::EStage::Create).IsEmpty(), TEXT("Fixture plans publish")))
			return true;
		Check(Feed.Observe(State) == 2, TEXT("Two new plans post two memos"));
		CheckDisplay(World, State, Feed, 2);
		Check(Feed.Observe(State) == 0, TEXT("Observing an unchanged plan list posts no memo"));
		Stage = 1;
		return false;
	}

	bool Drift(UWorld* World, ACommandGameState& State, UJevIntentFeed& Feed)
	{
		// Time passes on the server clock; the countdown follows, the memo count does not.
		FJevPublishedPlan& Moving = State.EnemyPlans[0];
		const float Before = EntrySeconds(World, Moving.TicketNumber);
		Moving.EtaSeconds = FMath::Max(1.f, Moving.EtaSeconds - 10.f);
		Moving.RemainingCommitment -= 2.f;
		Check(Feed.Observe(State) == 0, TEXT("A changed ETA alone posts no new memo"));
		Moving.TargetStructure = State.EnemyHeadquarters;
		Check(Feed.Observe(State) == 0, TEXT("A target structure appearing posts no memo: the text does not print it"));
		Moving.TargetStructure = nullptr;
		Check(Feed.Observe(State) == 0, TEXT("The target structure dying posts no duplicate memo"));
		Check(EntrySeconds(World, Moving.TicketNumber) < Before, TEXT("The timeline countdown follows the published ETA"));
		CheckDisplay(World, State, Feed, 2);
		Stage = 2;
		return false;
	}

	bool Escalate(UWorld* World, ACommandGameState& State, UJevIntentFeed& Feed)
	{
		const int32 Ticket = State.EnemyPlans[1].TicketNumber;
		if (!Check(JevIntentFixture::Publish(World, State, JevIntentFixture::EStage::Escalate).IsEmpty(), TEXT("Fixture escalation publishes")))
			return true;
		Check(State.EnemyPlans[1].TicketNumber == Ticket && State.EnemyPlans[1].bEscalated,
			TEXT("Escalation keeps the ticket and flags the plan"));
		Check(Feed.Observe(State) == 1, TEXT("Escalation posts exactly one changed memo"));
		CheckDisplay(World, State, Feed, 3);
		const FContext Context = MakeContext(ArmyTestSetup::Controller(World));
		BuildJevIntentModel(Context, Model);
		const JevIntent::FRegionBadge* Badge = Model.Badges.FindByPredicate(
			[&](const JevIntent::FRegionBadge& Entry) { return Entry.Region == State.EnemyPlans[1].TargetRegionIndex; });
		TStringBuilder<128> Label;
		if (Check(Badge != nullptr, TEXT("The defended region keeps its badge")))
			JevBadgeLabel(Context, *Badge, Label);
		const FString Expected = FString::Printf(TEXT("JEV  Escalated: defending %s"),
			*JevIntentView::RegionName(State, State.EnemyPlans[1].TargetRegionIndex));
		Check(FString(Label.ToView()) == Expected, TEXT("An escalated plan's badge reads Escalated: defending <region>"));
		Check(Feed.GetFeed().History().Last().Text.Contains(TEXT("Escalated: defending")),
			TEXT("The escalation memo names the defense"));
		Stage = 3;
		return false;
	}

	bool Replace(UWorld* World, ACommandGameState& State, UJevIntentFeed& Feed)
	{
		const int32 OldTicket = State.EnemyPlans[0].TicketNumber;
		const int32 OldRegion = State.EnemyPlans[0].TargetRegionIndex;
		if (!Check(JevIntentFixture::Publish(World, State, JevIntentFixture::EStage::Replace).IsEmpty(), TEXT("Fixture replacement publishes")))
			return true;
		Check(State.EnemyPlans[0].TicketNumber != OldTicket && State.EnemyPlans[0].TargetRegionIndex != OldRegion,
			TEXT("The replacement is a new ticket for a new region"));
		Check(Feed.Observe(State) == 1, TEXT("A replacement ticket posts its memo"));
		CheckDisplay(World, State, Feed, 4);
		const FContext Context = MakeContext(ArmyTestSetup::Controller(World));
		BuildJevIntentModel(Context, Model);
		Check(!Model.Timeline.ContainsByPredicate([&](const JevIntent::FTimelineEntry& Entry) { return Entry.Ticket == OldTicket; }),
			TEXT("The replaced ticket leaves the timeline"));
		Check(!Model.Badges.ContainsByPredicate([&](const JevIntent::FRegionBadge& Entry) { return Entry.Region == OldRegion; }),
			TEXT("The abandoned region loses its badge"));
		if (AArmyGroup* Gone = State.EnemyPlans[1].Force.Get())
			Gone->Destroy();
		Stage = 4;
		return false;
	}

	bool Prune(UWorld* World, ACommandGameState& State, UJevIntentFeed& Feed)
	{
		if (State.EnemyPlans.Num() != 1)
			return false;
		Check(Feed.Observe(State) == 0, TEXT("A plan removed with its force posts nothing"));
		CheckDisplay(World, State, Feed, 4);
		JevIntent::FVisibleMemos Visible;
		Feed.GetFeed().Visible(JevIntentView::Now(State) + JevIntent::MemoHoldSeconds + JevIntent::MemoFadeSeconds, Visible);
		Check(Visible.IsEmpty(), TEXT("Memos leave the feed once their lifetime ends"));
		return true;
	}

	float EntrySeconds(UWorld* World, int32 Ticket)
	{
		const FContext Context = MakeContext(ArmyTestSetup::Controller(World));
		BuildJevIntentModel(Context, Model);
		const JevIntent::FTimelineEntry* Entry = Model.Timeline.FindByPredicate(
			[&](const JevIntent::FTimelineEntry& Candidate) { return Candidate.Ticket == Ticket; });
		return Entry ? Entry->Seconds : -1.f;
	}

	// What is displayed equals what is published: entry, badge and memo for every plan, nothing extra.
	void CheckDisplay(UWorld* World, const ACommandGameState& State, const UJevIntentFeed& Feed, int32 MemoCount)
	{
		ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
		const FContext Context = MakeContext(PC);
		const FLayout Layout = MakeLayout(Context, ViewportWidth, ViewportHeight);
		BuildJevIntentModel(Context, Model);
		const float Now = State.GetServerWorldTimeSeconds();
		Check(Model.Timeline.Num() == State.EnemyPlans.Num(), TEXT("One timeline entry per published plan"));
		float Previous = -1.f;
		for (const JevIntent::FTimelineEntry& Entry : Model.Timeline)
		{
			const FJevPublishedPlan* Plan = State.EnemyPlans.FindByPredicate(
				[&](const FJevPublishedPlan& Candidate) { return Candidate.TicketNumber == Entry.Ticket; });
			if (!Check(Plan != nullptr, TEXT("Every timeline entry is a published ticket")))
				continue;
			const float Expected = FMath::Max(0.f, Plan->EtaSeconds - FMath::Max(0.f, Now - Plan->EtaIssuedAt));
			Check(Entry.ForceNumber == Plan->ForceNumber && Entry.Target == Plan->TargetRegionIndex
					&& Entry.SizeBand == Plan->SizeBand && Entry.bEscalated == Plan->bEscalated
					&& FMath::IsNearlyEqual(Entry.Seconds, Expected, KINDA_SMALL_NUMBER),
				TEXT("Entry force, region, size band, escalation and countdown equal the plan's"));
			Check(Entry.Seconds >= Previous, TEXT("The timeline is ordered soonest first"));
			Previous = Entry.Seconds;
		}
		for (const FJevPublishedPlan& Plan : State.EnemyPlans)
		{
			const JevIntent::FRegionBadge* Badge = Model.Badges.FindByPredicate(
				[&](const JevIntent::FRegionBadge& Candidate) { return Candidate.Region == Plan.TargetRegionIndex; });
			if (Check(Badge != nullptr, TEXT("Every targeted region has a badge")))
			{
				Check(!Plan.bEscalated || Badge->bEscalated, TEXT("An escalated plan escalates its region's badge"));
				TStringBuilder<128> Label;
				JevBadgeLabel(Context, *Badge, Label);
				const FString& Name = JevIntentView::RegionName(State, Plan.TargetRegionIndex);
				Check(!Name.IsEmpty() && FString(Label.ToView()).Contains(Name),
					TEXT("A world badge names its target region, as the timeline and memos do"));
				Check(Plan.bEscalated || FString(Label.ToView()).Contains(Plan.Verb == EForceVerb::Attack ? TEXT("Attack") : TEXT("Move & Hold")),
					TEXT("A non-escalated badge names the plan's verb"));
			}
		}
		for (const JevIntent::FRegionBadge& Badge : Model.Badges)
			Check(State.EnemyPlans.ContainsByPredicate([&](const FJevPublishedPlan& Plan) { return Plan.TargetRegionIndex == Badge.Region; }),
				TEXT("No badge marks a region no plan targets"));
		const TConstArrayView<JevIntent::FMemo> History = Feed.GetFeed().History();
		Check(History.Num() == MemoCount, TEXT("The feed holds exactly the posted memos"));
		for (const FJevPublishedPlan& Plan : State.EnemyPlans)
			Check(History.ContainsByPredicate([&](const JevIntent::FMemo& Memo) { return Memo.Text == Plan.Memo; }),
				TEXT("Every live plan's current memo is in the feed, verbatim"));
		FJevMemoRow Rows[JevIntent::MemoVisible];
		const int32 Rendered = JevMemoRows(Context, Layout, Model, Rows);
		Check(Rendered == FMath::Min(MemoCount, JevIntent::MemoVisible), TEXT("The newest memos have rows"));
		for (int32 Index = 0; Index < Rendered; ++Index)
			Check(Rows[Index].Memo == &History[History.Num() - 1 - Index], TEXT("Memo rows run newest first"));
		CheckCollisions(Context, Layout, Model, Rows, Rendered);
	}

	void CheckCollisions(const FContext& Context, const FLayout& Layout, const FJevIntentModel& Intent,
		const FJevMemoRow (&Rows)[JevIntent::MemoVisible], int32 Rendered)
	{
		TArray<FRect, TInlineAllocator<12>> Others = { Layout.Top, Layout.Objectives, Layout.Alerts, Layout.Build,
			Layout.Bottom, Layout.Minimap, Layout.Feedback, Layout.Menu, Layout.Pause };
		TArray<FRect, TInlineAllocator<4>> Mine;
		if (const FRect Timeline = JevTimelineRect(Context, Layout, Intent); Timeline.W > 0.f)
			Mine.Add(Timeline);
		for (int32 Index = 0; Index < Rendered; ++Index)
			Mine.Add(Rows[Index].Rect);
		for (const FRect& Rect : Mine)
		{
			Check(Rect.X >= 0.f && Rect.Y >= 0.f && Rect.Right() <= Layout.Width && Rect.Bottom() <= Layout.Height,
				TEXT("JEV panels stay inside the viewport"));
			for (const FRect& Other : Others)
				Check(!Overlaps(Rect, Other), TEXT("JEV panels avoid the objective strip, alert feed, deck, build bar, minimap and feedback"));
			Check(IsPanelPoint(Context, Layout, Rect.Center()), TEXT("JEV panels capture clicks like every other panel"));
			FContext Framed = Context;
			Framed.JevIntent = &Intent;
			Check(IsPanelPoint(Framed, Layout, Rect.Center()), TEXT("The HUD's shared frame model captures the same clicks"));
		}
	}

	bool Check(bool bCondition, const TCHAR* Message) { return Test->TestTrue(Message, bCondition); }
	FAutomationTestBase* Test;
	double Started;
	int32 Stage = 0;
	FJevIntentModel Model;
};

bool FJevIntentUiTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevIntentUiScenario(this));
	return true;
}

#endif
