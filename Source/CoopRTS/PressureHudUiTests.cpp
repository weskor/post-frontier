#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "DepositSite.h"
#include "EnemyCommander.h"
#include "Engine/World.h"
#include "Headquarters.h"
#include "HUD/HUDPanels.h"
#include "HUD/PressurePanels.h"
#include "HUD/PressureView.h"
#include "MapRegion.h"
#include "TeamEconomyFixture.h"

// The pressure surfaces read from the real replicated state: the release cell and clock off JEV's published schedule,
// the supply-cut chip off the connected mask and the deposits, a building's stun chip and feed row off its end time,
// and the clicks that focus what each points at.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressureHudReleaseTest, "CoopRTS.HUD.Pressure.Release",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressureHudCutTest, "CoopRTS.HUD.Pressure.SupplyCut",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressureHudStunTest, "CoopRTS.HUD.Pressure.Stun",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPressureHudLiftTest, "CoopRTS.HUD.Pressure.BadgeLift",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
using namespace CommandHUDPanels;
constexpr float ViewportWidth = 1280.f;
constexpr float ViewportHeight = 720.f;

// JEV with a published schedule: Current is in force, the next release follows. The planner stays off so nothing rewrites it.
AEnemyCommander* PublishSchedule(FTeamEconomyFixture& F, int32 Current)
{
	AEnemyCommander* Jev = nullptr;
	for (TActorIterator<AEnemyCommander> It(F.World); It; ++It)
		Jev = *It;
	if (!Jev)
		Jev = F.World->SpawnActor<AEnemyCommander>();
	if (!Jev)
		return nullptr;
	Jev->SetActorTickEnabled(false);
	Jev->Release.Current = Current;
	Jev->Release.Next = Current + 1;
	Jev->Release.NextAt = JevRelease::ReleaseTime(Current + 1);
	return Jev;
}

// Puts the view's match clock at Seconds: the game state's battle clock, moved forward without waiting.
void SetMatch(FTeamEconomyFixture& F, UPressureView& View, float Seconds)
{
	View.SetClockSkew(Seconds - (F.State->GetServerWorldTimeSeconds() - F.State->GetBattleClockStartServerTime()));
}

int32 RowsWith(const UPressureView& View, const TCHAR* Id)
{
	int32 Count = 0;
	for (const FObjectiveEvent& Row : View.Rows())
		Count += Row.Id == FName(Id);
	return Count;
}
}

bool FPressureHudReleaseTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		ACommandGameState& State = *F.State;
		UPressureView* View = UPressureView::Get(F.World);
		if (!T.TestNotNull(TEXT("The pressure view exists in a game world"), View) || !T.TestNotNull(TEXT("and JEV's main"), State.EnemyHeadquarters.Get()))
			return;
		if (!T.TestNotNull(TEXT("JEV publishes a schedule"), PublishSchedule(F, 0)))
			return;
		View->Observe(State, F.Wallets[0]);
		T.TestTrue(TEXT("The HUD's match clock is the game state's battle clock, not JEV's published copy"),
			FMath::IsNearlyEqual(JevIntent::MatchSeconds(View->Release(), State.GetServerWorldTimeSeconds()),
				State.GetServerWorldTimeSeconds() - State.GetBattleClockStartServerTime(), .001f));
		SetMatch(F, *View, 50.f);
		View->Observe(State, F.Wallets[0]);
		FContext Context = MakeContext(F.Controller);
		const FLayout Layout = MakeLayout(Context, ViewportWidth, ViewportHeight);
		FJevIntentModel Model;
		BuildJevIntentModel(Context, Model);
		T.TestTrue(TEXT("The schedule has replicated into the view"), View->Release().bKnown);
		T.TestTrue(TEXT("Fifty seconds in, v1.1 is not yet a cell and no plan is published"), Model.Timeline.IsEmpty());
		const FRect Bar = JevTimelineRect(Context, Layout, Model);
		T.TestEqual(TEXT("The bar still holds its 40 px"), Bar.H, 40.f);
		TStringBuilder<96> Empty;
		JevIntent::AppendEmptyTimeline(Empty, View->Release(), JevIntentView::Now(State));
		T.TestEqual(TEXT("and says when the next release is due"), FString(Empty.ToView()),
			FString(TEXT("No JEV plans \u00B7 next release v1.1 in 1:10")));
		T.TestEqual(TEXT("An empty bar has no cell to click"), HitTestPressure(Context, Layout, Bar.Center()), EHUDAction::None);

		// Ninety seconds in, v1.1 enters its last 30 s.
		SetMatch(F, *View, 100.f);
		View->Observe(State, F.Wallets[0]);
		BuildJevIntentModel(Context, Model);
		if (!T.TestEqual(TEXT("A release cell appears for the last 30 s"), Model.Timeline.Num(), 1))
			return;
		const JevIntent::FTimelineEntry& Cell = Model.Timeline[0];
		T.TestTrue(TEXT("pinned first as a release"), Cell.Kind == JevIntent::EEntryKind::Release && Cell.Release == 1);
		T.TestTrue(TEXT("counting down to match second 120"), FMath::IsNearlyEqual(Cell.Seconds, 20.f, .1f));
		Context.JevIntent = &Model;
		const EHUDAction Click = HitTestPressure(Context, Layout, JevTimelineCell(Bar, 0).Center());
		T.TestEqual(TEXT("Clicking the cell hits timeline cell 0"), Click, EHUDAction::JevTimelineCell0);
		T.TestEqual(TEXT("and the main window's hit test agrees"), HitTest(Context, Layout, JevTimelineCell(Bar, 0).Center()), Click);
		T.TestEqual(TEXT("A cell beyond the entries is not a target"), HitTestPressure(Context, Layout, JevTimelineCell(Bar, 1).Center()),
			EHUDAction::None);
		FVector Focus = FVector::ZeroVector;
		T.TestTrue(TEXT("A release focuses a place"), PressureFocusTarget(Context, Click, Focus));
		T.TestTrue(TEXT("which is JEV's main"), Focus.Equals(State.EnemyHeadquarters->GetActorLocation()));

		// A release happens: the feed posts one row, and a client that arrives later does not replay it.
		const int32 Before = RowsWith(*View, PressureView::ReleaseRowId);
		PublishSchedule(F, 1);
		SetMatch(F, *View, 121.f);
		View->Observe(State, F.Wallets[0]);
		T.TestEqual(TEXT("The feed posts a row when v1.1 arrives"), RowsWith(*View, PressureView::ReleaseRowId), Before + 1);
		T.TestEqual(TEXT("with the release's tag"), View->Rows().Last().TargetForceOwnerName,
			FString(TEXT("JEV v1.1 released: WAVE \u00B7 RAIDS DRILL RIGS")));
		T.TestTrue(TEXT("on a local sequence no replicated ring uses"), PressureView::IsLocalSequence(View->Rows().Last().Sequence));
		View->Observe(State, F.Wallets[0]);
		T.TestEqual(TEXT("Observing again posts nothing more"), RowsWith(*View, PressureView::ReleaseRowId), Before + 1);
		T.TestEqual(TEXT("The clock reads match time from the published start"),
			FMath::RoundToInt(JevIntent::MatchSeconds(View->Release(), State.GetServerWorldTimeSeconds())), 121);

		// v1.2 counters the humans' most numerous armor class: the row names the class the cell showed, though JEV's own
		// value has moved on by the time the release is in force.
		AArmyUnit* Human = F.SpawnAttacker();
		if (!T.TestNotNull(TEXT("A human unit stands"), Human))
			return;
		// JEV publishes the class it counters (JevReleaseWorldTests asserts that side); the view carries it to the row.
		PublishSchedule(F, 1)->Release.CounterArmor = Human->GetArmorClass();
		SetMatch(F, *View, 235.f);
		View->Observe(State, F.Wallets[0]);
		T.TestEqual(TEXT("With v1.2 next the view carries the class JEV published"), View->Release().CounterArmor, Human->GetArmorClass());
		PublishSchedule(F, 2)->Release.CounterArmor = EArmorClass::Unset;
		SetMatch(F, *View, 241.f);
		View->Observe(State, F.Wallets[0]);
		TStringBuilder<64> Expected;
		Expected << TEXT("JEV v1.2 released: ");
		JevIntent::AppendReleaseTag(Expected, 2, Human->GetArmorClass());
		T.TestEqual(TEXT("The v1.2 release row names that class"), View->Rows().Last().TargetForceOwnerName, FString(Expected.ToView()));
		T.TestFalse(TEXT("not the generic word"), View->Rows().Last().TargetForceOwnerName.EndsWith(TEXT("COUNTERS ARMOR")));
	}));
	return true;
}

bool FPressureHudCutTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		ACommandGameState& State = *F.State;
		F.SetRole(F.Far, ERegionRole::Reward);
		// The fixture holds the isolated alternate region; a neutral one is nobody's to cut.
		F.SetController(F.Alternate, -1);
		if (!T.TestNotNull(TEXT("A Drill Rig stands in the far region"), F.SpawnRig(F.Far, F.Wallets[0], 0)))
			return;
		F.Step();
		FContext Context = MakeContext(F.Controller);
		const FLayout Layout = MakeLayout(Context, ViewportWidth, ViewportHeight);
		T.TestTrue(TEXT("The HUD is on the game screen"), F.Controller->GetUIScreen() == ECommandScreen::Game);
		T.TestFalse(TEXT("A whole chain shows no chip"), ReadCutLoss(Context).Any());
		T.TestEqual(TEXT("and the top bar has no chip to click"), CutChipRect(Layout, ReadCutLoss(Context)).W, 0.f);

		// The enemy takes the neck: the far region is held but unreachable.
		F.SetController(F.Neck, 5);
		F.Step();
		TArray<int32> Cut;
		const PressureHud::FCutLoss Loss = ReadCutLoss(Context, &Cut);
		T.TestEqual(TEXT("One region is cut"), Loss.Regions, 1);
		T.TestTrue(TEXT("the far one"), Cut == TArray<int32>({ F.Far }));
		const double Rig = F.DepositIn(F.Far)->RatePerSecond();
		T.TestEqual(TEXT("Each of the two commanders loses half the offline rig's Power"), Loss.PowerPerSecond, Rig / 2.);
		T.TestEqual(TEXT("and the reward region's Data, a full second's worth each"), Loss.DataPerSecond, 1.);
		TStringBuilder<96> Text;
		PressureHud::AppendCutChip(Text, Loss);
		T.TestTrue(TEXT("The chip lists both lost rates"), FString(Text.ToView()).Contains(TEXT("Power/s")) && FString(Text.ToView()).Contains(TEXT("Data/s")));

		const FRect Chip = CutChipRect(Layout, Loss);
		T.TestEqual(TEXT("The chip's click area is the full bar height"), Chip.H, Layout.Top.H);
		T.TestTrue(TEXT("inside the top bar"), Chip.X >= Layout.Top.X && Chip.Right() <= Layout.Top.Right());
		T.TestFalse(TEXT("clear of the battle clock"), Chip.Intersects(BattleClockRect(Layout)));
		T.TestEqual(TEXT("Clicking it hits the cut chip"), HitTest(Context, Layout, Chip.Center()), EHUDAction::JevCutChip);
		T.TestTrue(TEXT("and the chip is a panel point, so the world never sees the click"), IsPanelPoint(Context, Layout, Chip.Center()));

		// A second cut region: clicks walk both in index order and wrap.
		F.SetController(F.Alternate, 0);
		F.Step();
		ReadCutLoss(Context, &Cut);
		T.TestTrue(TEXT("Two regions are cut"), Cut == TArray<int32>({ F.Far, F.Alternate }));
		FVector At = FVector::ZeroVector;
		const EHUDAction Click = EHUDAction::JevCutChip;
		T.TestTrue(TEXT("The first click focuses"), PressureFocusTarget(Context, Click, At));
		T.TestTrue(TEXT("the first cut region"), At.Equals(State.GetRegionAnchor(F.Far)));
		T.TestTrue(TEXT("the next click focuses"), PressureFocusTarget(Context, Click, At));
		T.TestTrue(TEXT("the second"), At.Equals(State.GetRegionAnchor(F.Alternate)));
		T.TestTrue(TEXT("and the third wraps"), PressureFocusTarget(Context, Click, At));
		T.TestTrue(TEXT("to the first"), At.Equals(State.GetRegionAnchor(F.Far)));

		// Reconnected: the chip and its click both go.
		F.SetController(F.Neck, 0);
		F.SetController(F.Alternate, 5);
		F.Step();
		T.TestFalse(TEXT("A restored chain shows no chip"), ReadCutLoss(Context).Any());
		T.TestFalse(TEXT("and a late click focuses nothing"), PressureFocusTarget(Context, Click, At));
	}));
	return true;
}

bool FPressureHudStunTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		ACommandGameState& State = *F.State;
		UPressureView* View = UPressureView::Get(F.World);
		ACommandBuilding* Mine = F.SpawnBarracks(0, 1.f, 0);
		ACommandBuilding* Theirs = F.SpawnBarracks(5, 1.f, 0);
		if (!T.TestNotNull(TEXT("The pressure view exists"), View) || !T.TestTrue(TEXT("Both Barracks stand"), Mine && Theirs))
			return;
		F.Step();
		const FContext Context = MakeContext(F.Controller);
		View->Observe(State, F.Wallets[0]);
		T.TestFalse(TEXT("An unstunned building shows no chip"), BuildingStun(Context, *Mine).bShown);

		Mine->ApplyStun(3.f);
		Theirs->ApplyStun(3.f);
		View->Observe(State, F.Wallets[0]);
		const PressureHud::FStunChip Chip = BuildingStun(Context, *Mine);
		T.TestTrue(TEXT("A stunned building shows its chip"), Chip.bShown);
		T.TestTrue(TEXT("with the full three seconds"), FMath::IsNearlyEqual(Chip.Remaining, 3.f, .1f));
		T.TestTrue(TEXT("and a full drain bar"), Chip.Drain > .95f);
		T.TestTrue(TEXT("A hostile stunned building shows its chip too"), BuildingStun(Context, *Theirs).bShown);
		T.TestEqual(TEXT("Only the commander's own building posts a feed row"), RowsWith(*View, PressureView::StunRowId), 1);
		const FObjectiveEvent& Row = View->Rows().Last();
		T.TestTrue(TEXT("naming it and the Scrambler"), Row.TargetForceOwnerName.EndsWith(TEXT(" stunned by a Scrambler")));
		T.TestTrue(TEXT("at the building"), Row.Location.Equals(Mine->GetActorLocation()));

		// A second Scrambler pulse that ends no later changes nothing: no new row, and the chip stays up.
		Mine->ApplyStun(3.f);
		View->Observe(State, F.Wallets[0]);
		T.TestEqual(TEXT("A repeated stun posts no second row"), RowsWith(*View, PressureView::StunRowId), 1);
		T.TestTrue(TEXT("and the chip stays up"), BuildingStun(Context, *Mine).bShown);
	}));
	return true;
}

bool FPressureHudLiftTest::RunTest(const FString&)
{
	const auto Anywhere = [](const FRect&) { return true; };
	const FRect Label{ 100.f, 100.f, 148.f, 24.f };
	const FRect Elsewhere{ 400.f, 100.f, 148.f, 24.f };
	const FRect Badge{ 120.f, 90.f, 160.f, 22.f };
	const TArray<FRect> Labels = { Elsewhere, Label };
	const FRect Lifted = PlaceClearOf(Badge, Labels, Anywhere);
	TestFalse(TEXT("A badge over a deposit label no longer covers it"), Lifted.Intersects(Label));
	TestEqual(TEXT("It moves straight up, not sideways"), Lifted.X, Badge.X);
	TestTrue(TEXT("and sits just above the label"), FMath::IsNearlyEqual(Lifted.Bottom(), Label.Y - 2.f, .01f));
	const FRect Clear{ 120.f, 20.f, 160.f, 22.f };
	TestEqual(TEXT("A badge clear of every label stays where it is"), PlaceClearOf(Clear, Labels, Anywhere).Y, Clear.Y);

	// Two labels stacked: clearing the lower one lands on the upper one, so the badge goes above both.
	const TArray<FRect> Stack = { FRect{ 100.f, 60.f, 148.f, 24.f }, Label };
	const FRect Above = PlaceClearOf(Badge, Stack, Anywhere);
	TestFalse(TEXT("Stacked labels are both cleared"), Above.Intersects(Stack[0]) || Above.Intersects(Stack[1]));
	TestTrue(TEXT("by going above the upper one"), Above.Bottom() <= Stack[0].Y);

	// A panel across the top takes "up" away: the badge goes beside the label it covered.
	const FRect Ceiling{ 0.f, 0.f, 1000.f, 80.f };
	const auto BelowCeiling = [&Ceiling](const FRect& Candidate) { return !Candidate.Intersects(Ceiling); };
	const TArray<FRect> Single = { Label };
	const FRect Beside = PlaceClearOf(Badge, Single, BelowCeiling);
	TestFalse(TEXT("With no room above, the badge still clears the label"), Beside.Intersects(Label));
	TestEqual(TEXT("and keeps its height"), Beside.Y, Badge.Y);
	TestTrue(TEXT("on the label's right"), FMath::IsNearlyEqual(Beside.X, Label.Right() + 2.f, .01f));

	// A screen edge on the right as well: the left side is used.
	const FRect RightEdge{ 240.f, 0.f, 1000.f, 1000.f };
	const auto LeftOnly = [&](const FRect& Candidate) { return BelowCeiling(Candidate) && !Candidate.Intersects(RightEdge); };
	const FRect Left = PlaceClearOf(Badge, Single, LeftOnly);
	TestTrue(TEXT("and on its left when the right is taken too"), FMath::IsNearlyEqual(Left.Right(), Label.X - 2.f, .01f));

	// Nowhere clear: the badge stays put rather than vanishing.
	const auto Nowhere = [](const FRect&) { return false; };
	TestEqual(TEXT("A badge with nowhere to go stays where its region puts it"), PlaceClearOf(Badge, Single, Nowhere).Y, Badge.Y);
	return true;
}
#endif
