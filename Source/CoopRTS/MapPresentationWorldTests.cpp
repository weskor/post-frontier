#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "Commands/AbilityCommandComponent.h"
#include "HUD/MapPresentation.h"
#include "MapRegion.h"
#include "Rules/MapPresentationPolicy.h"
#include "TeamEconomyFixture.h"

// The map's supply-cut state on the real region graph: it follows the published connected mask and its change time,
// cables classify by that mask, a cut posts one team feed row, and a Drill Rig reads offline.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapPresentationCutTest, "CoopRTS.Map.Presentation.SupplyCut",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapPresentationJevCutTest, "CoopRTS.Map.Presentation.JevCut",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using MapPresentation::ECable;

// What this client has observed of both teams, advanced the way the HUD does each frame.
struct FSeen
{
	MapPresentation::FCutObserver Humans, Jev;
	void Step(FTeamEconomyFixture& F)
	{
		F.Step();
		MapView::Observe(*F.State, 0, Humans);
		MapView::Observe(*F.State, 5, Jev);
	}
	const MapPresentation::FCutObserver& For(int32 Team) const { return Team == 0 ? Humans : Jev; }
};

// The cable between two regions as the HUD would draw it for the team, or None.
ECable CableBetween(const FSeen& Seen, const ACommandGameState& State, int32 Team, int32 A, int32 B)
{
	ECable Found = ECable::None;
	MapView::ForEachCable(State, Team, Seen.For(Team), [&](const AMapRegion& From, const AMapRegion& To, ECable Cable) {
		if ((From.RegionIndex == A && To.RegionIndex == B) || (From.RegionIndex == B && To.RegionIndex == A))
			Found = Cable;
	});
	return Found;
}

int32 CutRows(const ACommandPlayerController& Controller)
{
	int32 Count = 0;
	for (const FObjectiveEvent& Event : Controller.AbilityCommands->GetEvents())
		Count += Event.Id == FName(MapPresentation::SupplyCutEventId);
	return Count;
}
}

bool FMapPresentationCutTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		const ACommandGameState& State = *F.State;
		FSeen Seen;
		const int32 Neck = F.Neck, Far = F.Far;
		if (!T.TestNotNull(TEXT("A Drill Rig stands in the far region"), F.SpawnRig(Far, F.Wallets[0], 0)))
			return;
		Seen.Step(F);
		T.TestEqual(TEXT("Cables join the connected main, neck and far region"), CableBetween(Seen, State, 0, 0, Neck), ECable::Live);
		T.TestEqual(TEXT("both links"), CableBetween(Seen, State, 0, Neck, Far), ECable::Live);
		T.TestFalse(TEXT("A connected region is not cut off"), MapView::IsCutOff(State, 0, Far));
		T.TestFalse(TEXT("and its Drill Rig is online"), MapView::IsRigOffline(State, *F.DepositIn(Far)));
		T.TestEqual(TEXT("No cut row has been posted"), CutRows(*F.Controller), 0);

		// The enemy takes the neck: the far region is held but unreachable.
		F.SetController(Neck, 5);
		Seen.Step(F);
		T.TestTrue(TEXT("The far region is cut off"), MapView::IsCutOff(State, 0, Far));
		T.TestFalse(TEXT("The neck is lost, not cut off"), MapView::IsCutOff(State, 0, Neck));
		T.TestEqual(TEXT("The cable behind the enemy-held neck is gone"), CableBetween(Seen, State, 0, 0, Neck), ECable::None);
		T.TestEqual(TEXT("and the far region keeps the snapped stub"), CableBetween(Seen, State, 0, Neck, Far), ECable::Snapped);
		T.TestTrue(TEXT("The far region's Drill Rig reads offline"), MapView::IsRigOffline(State, *F.DepositIn(Far)));
		T.TestEqual(TEXT("and counts once"), MapView::OfflineRigs(State, 0, Far), 1);
		const float Now = State.GetServerWorldTimeSeconds();
		T.TestTrue(TEXT("The newly cut far region flashes"), Seen.Humans.FlashAge(Far, Now) >= 0.f);
		T.TestTrue(TEXT("the lost neck does not"), Seen.Humans.FlashAge(Neck, Now) < 0.f);
		T.TestEqual(TEXT("One team feed row announces the cut"), CutRows(*F.Controller), 1);
		const FObjectiveEventView Events = F.Controller->AbilityCommands->GetEvents();
		const FObjectiveEvent& Row = Events.Last();
		T.TestTrue(TEXT("and names the far region at its anchor"),
			Row.Id == FName(MapPresentation::SupplyCutEventId) && Row.RegionIndex == Far
				&& Row.Location.Equals(State.GetRegionAnchor(Far)) && Row.AffectedTeam == 0);
		Seen.Step(F);
		T.TestEqual(TEXT("A later tick with no change posts nothing more"), CutRows(*F.Controller), 1);

		// Reconnecting brings the cables back and posts no row.
		F.SetController(Neck, 0);
		Seen.Step(F);
		T.TestFalse(TEXT("Retaking the neck reconnects the far region"), MapView::IsCutOff(State, 0, Far));
		T.TestEqual(TEXT("and its cable is live again"), CableBetween(Seen, State, 0, Neck, Far), ECable::Live);
		T.TestFalse(TEXT("and its rig is online"), MapView::IsRigOffline(State, *F.DepositIn(Far)));
		T.TestEqual(TEXT("Reconnecting posts no row"), CutRows(*F.Controller), 1);

		// Losing the far region itself is a loss, not a cut.
		F.SetController(Far, 5);
		Seen.Step(F);
		T.TestFalse(TEXT("A region the enemy took is not cut off"), MapView::IsCutOff(State, 0, Far));
		T.TestEqual(TEXT("and posts no cut row"), CutRows(*F.Controller), 1);

		// Two regions beyond one lost neck: the dimmed cable joins them and each is cut.
		F.SetController(Far, 0);
		F.SetController(F.Alternate, 0);
		F.SetNeighbours(Far, { Neck, F.Alternate });
		F.SetNeighbours(F.Alternate, { Far });
		Seen.Step(F);
		T.TestTrue(TEXT("Both are connected before the cut"), State.IsRegionConnected(0, Far) && State.IsRegionConnected(0, F.Alternate));
		F.SetController(Neck, 5);
		Seen.Step(F);
		T.TestTrue(TEXT("Both far regions are cut off"), MapView::IsCutOff(State, 0, Far) && MapView::IsCutOff(State, 0, F.Alternate));
		T.TestEqual(TEXT("A dimmed cable joins them beyond the cut"), CableBetween(Seen, State, 0, Far, F.Alternate), ECable::Beyond);
		T.TestEqual(TEXT("Each cut region got its own row"), CutRows(*F.Controller), 3);
	}));
	return true;
}

bool FMapPresentationJevCutTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		const ACommandGameState& State = *F.State;
		FSeen Seen;
		// JEV's chain: its main (1), the neck and the far region.
		F.SetNeighbours(0, {});
		F.SetNeighbours(1, { F.Neck });
		F.SetNeighbours(F.Neck, { 1, F.Far });
		F.SetNeighbours(F.Far, { F.Neck });
		F.SetController(F.Neck, 5);
		F.SetController(F.Far, 5);
		Seen.Step(F);
		T.TestEqual(TEXT("JEV's cables are live between its connected regions"), CableBetween(Seen, State, 5, F.Neck, F.Far), ECable::Live);
		T.TestEqual(TEXT("and the humans have none there"), CableBetween(Seen, State, 0, F.Neck, F.Far), ECable::None);
		F.SetController(F.Neck, 0);
		Seen.Step(F);
		T.TestTrue(TEXT("Taking JEV's neck cuts off its far region"), MapView::IsCutOff(State, 5, F.Far));
		T.TestFalse(TEXT("which is not a human cut"), MapView::IsCutOff(State, 0, F.Far));
		T.TestEqual(TEXT("JEV's cut posts no human team row"), CutRows(*F.Controller), 0);
		T.TestTrue(TEXT("It flashes on JEV's own observer"),
			Seen.Jev.FlashAge(F.Far, State.GetServerWorldTimeSeconds()) >= 0.f && Seen.Humans.FlashAge(F.Far, 0.f) < 0.f);
	}));
	return true;
}
#endif
