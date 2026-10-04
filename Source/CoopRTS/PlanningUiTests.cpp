#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyGroup.h"
#include "CommandCamera.h"
#include "ArmyTestSetup.h"
#include "DepositSite.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "PlanningUiFixture.h"
#include "Rules/PlanningHudPolicy.h"
#include "Rules/PlanningPolicy.h"

// A commander plans through the real controller and HUD: Enter asks before leaving a piece to its default spot, the KIT
// bar places and moves the kit for free, a chip picks the unit type, RMB / A / Shift queue the first order, Ready locks
// every edit, Enter un-readies, and when the last Ready comes the panel gives way to Pause and the battle.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningUiFlowTest, "CoopRTS.Planning.UI.Flow",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace PlanningUiFlowTests
{
using namespace CommandHUDPanels;

FString ReadyText(const FContext& Context)
{
	const FPlanningView View = ReadPlanning(Context);
	TStringBuilder<64> Text;
	PlanningHud::AppendReady(Text, View.Kit && View.Kit->bReady, View.ReadyHumans, View.Humans);
	return FString(Text.ToView());
}

bool MotionBlurSuppressed(const ACommandPlayerController* Controller)
{
	const ACommandCamera* Camera = Cast<ACommandCamera>(Controller->GetPawn());
	return Camera && Camera->IsMotionBlurSuppressed();
}

class FScenario final : public PlanningUiFixture::FScenario
{
public:
	using PlanningUiFixture::FScenario::FScenario;

private:
	bool Step() override
	{
		switch (Stage)
		{
		case 0:
			return KitsAndHudUp() ? Advance(1) : false;
		case 1:
			Layouts();
			Key(EKeys::Enter);
			return Advance(2);
		case 2:
			return Settled() && AskedFirst() ? PressEnter(3) : false;
		case 3:
			return Settled() && AcceptedDefaults() ? PressEnter(4) : false;
		case 4:
			return Settled() && UnreadiedByEnter() ? PressEnter(5) : false;
		case 5:
			return Settled() && AskedAgain() && Placement() ? PressKey(EKeys::Escape, 6) : false;
		case 6:
			return Settled() && Edits() ? PressKey(EKeys::P, 7) : false;
		case 7:
			return Settled() && PauseExplained() ? PressKey(EKeys::H, 8) : false;
		case 8:
			return Settled() && FortifyExplained() ? PressEnter(9) : false;
		case 9:
			return Settled() && ReadiedByEnter() ? PressEnter(10) : false;
		case 10:
			return Settled() && UnreadyEdits() ? PressEnter(11) : false;
		default:
			return Settled() && ZeroHour() ? Done() : false;
		}
	}

	bool Advance(int32 Next)
	{
		Enter(Next);
		return false;
	}
	bool PressKey(FKey Value, int32 Next)
	{
		Key(Value);
		return Advance(Next);
	}
	bool PressEnter(int32 Next) { return PressKey(EKeys::Enter, Next); }

	// Both viewport sizes: the 720x186 panel, 28 px targets that win their own centres, the READY slot in Pause's place
	// and a free KIT bar with the Workshop closed.
	void Layouts()
	{
		Host->Resources = 100;
		const FContext Context = MakeContext(PC);
		if (!Check(Context.bPlanning && !Context.bKitReady, TEXT("The phase is on and the kit is not Ready")))
			return;
		Check(MotionBlurSuppressed(PC), TEXT("Motion blur is held off on this camera while the frozen world is on screen"));
		const AMapRegion* Home = State->FindRegionAt(State->FriendlyHeadquarters->GetActorLocation());
		Check(Home && State->IsRegionConnected(0, Home->RegionIndex),
			TEXT("The team's main reads connected while the world is frozen, so the map shows no false supply cut"));
		for (const FVector2D& Size : { FVector2D(1600.f, 900.f), FVector2D(1280.f, 720.f) })
		{
			const FLayout Layout = MakeLayout(Context, Size.X, Size.Y);
			const FPlanningGeometry G = PlanningGeometry(Layout.Inspector);
			Check(Layout.bDeck && Layout.Inspector.W == 720.f && Layout.Inspector.H == 186.f, TEXT("The planning panel is the pinned 720x186 deck"));
			for (const FRect& Chip : G.Chips)
				Check(Chip.W == 104.f && Chip.H == 30.f && Chip.X >= Layout.Inspector.X && Chip.Right() <= Layout.Inspector.Right(),
					TEXT("Five 104x30 unit chips share one row inside the panel"));
			Check(G.Look.H >= 28.f && G.Clear.H >= 28.f && G.Look.Right() <= Layout.Inspector.Right(), TEXT("LOOK AT JEV BASE and CLEAR are at least 28 px"));
			Buttons(Context, Layout);
		}
	}

	void Buttons(const FContext& Context, const FLayout& Layout)
	{
		bool bPause = false, bReady = false, bRecipe = false;
		int32 Open = 0, Closed = 0;
		ForEachButton(Context, Layout, [&](const FButton& Button) {
			bPause |= Button.Action == EHUDAction::ActivePause;
			bReady |= Button.Action == EHUDAction::PlanReady && Button.Rect.X == Layout.Pause.X && Button.Rect.Y == Layout.Pause.Y;
			bRecipe |= RecipeSlot(Button.Action) != INDEX_NONE;
			Open += (BuildSlot(Button.Action) == 0 || BuildSlot(Button.Action) == 1) && Button.Block == EBlock::None;
			Closed += BuildSlot(Button.Action) == 2 && Button.Block == EBlock::Planning;
			if (Button.Action == EHUDAction::PlanPanel || (!IsPlanningAction(Button.Action) && BuildSlot(Button.Action) == INDEX_NONE))
				return;
			Check(Button.Rect.W >= 28.f && Button.Rect.H >= 28.f, TEXT("Every planning target is at least 28 px"));
			Check(HitTest(Context, Layout, Button.Rect.Center()) == Button.Action, TEXT("Each planning button wins its own centre"));
		});
		Check(bReady && !bPause, TEXT("READY takes the Pause slot"));
		Check(Open == 2 && Closed == 1, TEXT("The Barracks and Drill Rig cards are free at 100 Power; the Workshop is closed until 0:00"));
		Check(!bRecipe, TEXT("No producer controls show while the panel is the deck"));
	}

	bool AskedFirst()
	{
		return Check(!Kit()->bReady && Feedback() == Question(), TEXT("Enter with nothing placed asks and does not ready"));
	}

	bool AcceptedDefaults()
	{
		if (!Check(Kit()->bReady && State->IsPlanning(), TEXT("A second Enter while the question stands readies; the phase goes on")))
			return false;
		const EUnitRole Before = Kit()->UnitRole;
		Click(EHUDAction::PlanUnit2);
		Check(Kit()->UnitRole == Before && Feedback() == PlanningPolicy::EditRejection(true, true), TEXT("Ready locks the unit type, with the reason"));
		Click(EHUDAction::BuildSlot0);
		Check(!PC->IsPlacingBuilding() && Feedback() == PlanningPolicy::EditRejection(true, true), TEXT("and the KIT bar"));
		const FContext Context = MakeContext(PC);
		const FLayout Layout = MakeLayout(Context, 1600.f, 900.f);
		int32 Locked = 0;
		ForEachButton(Context, Layout, [&](const FButton& Button) {
			Locked += (Button.Action == EHUDAction::BuildSlot0 || Button.Action == EHUDAction::PlanUnit0) && Button.Block == EBlock::Locked;
		});
		return Check(Locked == 2 && ReadyText(Context) == TEXT("\u2713 READY (1/2)  [Enter] Undo"),
			TEXT("The card and the chips read as locked, and the slot counts one human Ready of two"));
	}

	bool UnreadiedByEnter()
	{
		return Check(!Kit()->bReady && ReadyText(MakeContext(PC)) == TEXT("[Enter] READY (0/2)"), TEXT("Enter un-readies and unlocks the kit"));
	}

	bool AskedAgain()
	{
		return Check(!Kit()->bReady && Feedback() == Question(), TEXT("A spent question is not remembered: Enter asks again"));
	}

	// The KIT bar through the existing placement mode: free (100 Power is under a Barracks' price), finished, movable.
	bool Placement()
	{
		FVector First, Second;
		const TArray<ADepositSite*> Free = OwnFreeDeposits();
		if (!Check(BarracksSpot(0, First) && BarracksSpot(1, Second) && Free.Num() >= 1, TEXT("Two legal Barracks spots and a free deposit exist")))
			return false;
		Click(EHUDAction::BuildSlot0);
		Check(PC->IsPlacingBuilding() && PC->GetPlacementIndex() == 0 && PC->IsHUDExpanded(), TEXT("The Barracks card arms placement and the panel stays up"));
		PC->PlaceBuildingAt(First, false);
		const ACommandBuilding* Barracks = Kit()->Barracks;
		Check(IsValid(Barracks) && Barracks->IsComplete() && Barracks->OwningPlayerState == Host && Host->Resources == 100 && !PC->IsPlacingBuilding(),
			TEXT("A kit Barracks stands finished and free, and the mode ends"));
		Click(EHUDAction::BuildSlot1);
		PC->PlaceBuildingAt(Free[0]->GetActorLocation(), false);
		Check(IsValid(Kit()->Rig) && Kit()->Rig->Deposit == Free[0] && Kit()->Rig->IsComplete() && Host->Resources == 100, TEXT("The Drill Rig snaps to a free deposit for free"));
		Click(EHUDAction::BuildSlot0);
		PC->PlaceBuildingAt(Second, false);
		int32 Owned = 0;
		for (const ACommandBuilding* Building : State->Buildings)
			Owned += IsValid(Building) && Building->OwningPlayerState == Host && Building->Kind == EBuildingKind::Barracks;
		Check(Owned == 1 && FVector::Dist2D(Kit()->Barracks->GetActorLocation(), Second) < 100., TEXT("Clicking a placed card moves its piece"));
		Click(EHUDAction::BuildSlot0);
		PC->PlaceBuildingAt(ArmyTestSetup::OutsideArena(State), false);
		return Check(PC->IsPlacingBuilding() && !Feedback().IsEmpty() && FVector::Dist2D(Kit()->Barracks->GetActorLocation(), Second) < 100.,
			TEXT("A refused spot explains itself, keeps the mode and leaves the piece where it was"));
	}

	// Unit chips, the first order through RMB, Shift and A on the minimap, CLEAR, and LOOK AT JEV BASE.
	bool Edits()
	{
		const FContext Context = MakeContext(PC);
		const UArmyUnitDefinition* Ranged = PlanningUnit(Context, 1);
		const UArmyUnitDefinition* Siege = PlanningUnit(Context, 2);
		if (!Check(!PC->IsPlacingBuilding() && Ranged && Siege, TEXT("Esc ended the armed placement and the chips name the catalogue's types")))
			return false;
		Click(EHUDAction::PlanUnit2);
		const bool bSiege = Kit()->UnitRole == Siege->Role;
		Click(EHUDAction::PlanUnit1);
		Check(bSiege && Kit()->UnitRole == Ranged->Role, TEXT("A chip sets the unit type, and the last pick wins"));
		return FirstOrders() && Check(LookAtJev(), TEXT("LOOK AT JEV BASE centres the camera on JEV's headquarters"));
	}

	bool FirstOrders()
	{
		const AMapRegion* Home = State->FindRegionAt(State->FriendlyHeadquarters->GetActorLocation());
		AActor* Hostile = State->Planning.JevKits[0].Barracks;
		if (!Check(Home && !Home->Neighbours.IsEmpty() && Hostile, TEXT("A neighbouring region and a JEV structure exist")))
			return false;
		const int32 Target = Home->Neighbours[0];
		const FVector2D Region = Minimap(State->GetRegionAnchor(Target));
		PC->HandleOrderClick(Minimap(Hostile->GetActorLocation()));
		// The minimap picks the structure whose marker is on top: JEV's headquarters stands over its Barracks there.
		const AActor* Picked = Kit()->Orders.IsEmpty() ? nullptr : Kit()->Orders[0].Structure.Get();
		Check(Kit()->Orders.Num() == 1 && Kit()->Orders[0].Verb == EForceVerb::Attack && Picked
				&& State->FindRegionAt(Picked->GetActorLocation()) == State->FindRegionAt(Hostile->GetActorLocation()),
			FString::Printf(TEXT("RMB on JEV's structures sets an Attack first order on one (%d orders, feedback %s)"), Kit()->Orders.Num(), *Feedback()));
		PC->HandleOrderClick(Region, true);
		Check(Kit()->Orders.Num() == 2 && Kit()->Orders[1].Verb == EForceVerb::MoveHold && Kit()->Orders[1].RegionIndex == Target,
			TEXT("Shift+RMB on a region queues a Move & Hold"));
		Click(EHUDAction::PlanClearOrders);
		Check(Kit()->Orders.IsEmpty(), TEXT("CLEAR empties the queue"));
		Key(EKeys::A);
		if (!Check(PC->IsAssigningOrder() && PC->IsHUDExpanded(), TEXT("A arms the Attack mode with no force and the panel stays up")))
			return false;
		PC->ConfirmAttackAtScreenPosition(Region, false);
		return Check(!PC->IsAssigningOrder() && Kit()->Orders.Num() == 1 && Kit()->Orders[0].Verb == EForceVerb::Attack
				&& Kit()->Orders[0].RegionIndex == Target && !Kit()->Orders[0].Structure,
			TEXT("A then a region click sets an Attack on the region and ends the mode"));
	}

	bool LookAtJev()
	{
		Click(EHUDAction::PlanLookJev);
		const FVector Hq = State->EnemyHeadquarters->GetActorLocation(), At = PC->GetPawn()->GetActorLocation();
		const FVector2D Extent = State->Arena->HalfExtent;
		return FMath::IsNearlyEqual(At.X, FMath::Clamp(Hq.X, -Extent.X, Extent.X), 1.) && FMath::IsNearlyEqual(At.Y, FMath::Clamp(Hq.Y, -Extent.Y, Extent.Y), 1.);
	}

	bool PauseExplained()
	{
		return Check(Feedback() == TEXT("Nothing runs during planning.") && !State->IsActivePaused(), TEXT("P explains that nothing runs during planning"));
	}

	bool FortifyExplained()
	{
		return Check(Feedback() == TEXT("Opens at 0:00") && !PC->IsFortifyTargeting(), TEXT("H does not arm Fortify and says when it opens"));
	}

	// With the kit placed Enter readies at once.
	bool ReadiedByEnter()
	{
		return Check(Kit()->bReady && Feedback() != Question() && State->IsPlanning(), TEXT("A placed kit readies on one Enter and the phase goes on"));
	}

	// Un-Ready edits again: the last unit type pick is the one that starts, and the last Ready ends the phase.
	bool UnreadyEdits()
	{
		if (!Check(!Kit()->bReady && State->IsPlanning(), TEXT("Enter un-readies until 0:00")))
			return false;
		Click(EHUDAction::PlanUnit1);
		Check(Kit()->UnitRole == EUnitRole::Ranged, TEXT("and edits work again"));
		// The KIT card stays armed when the last Ready lands: it must not survive into the battle.
		Click(EHUDAction::BuildSlot0);
		Check(PC->IsPlacingBuilding(), TEXT("A KIT card is armed as planning ends"));
		return Check(FPlanningCommands::SetReady(GuestPtr.Get(), true).IsAccepted() && State->IsPlanning(), TEXT("The other commander readies; one human is still not"));
	}

	bool ZeroHour()
	{
		if (!Check(!State->IsPlanning(), TEXT("The last Ready ends planning")))
			return false;
		const FContext Context = MakeContext(PC);
		const FLayout Layout = MakeLayout(Context, ViewWidth, ViewHeight);
		bool bPause = false, bPlanning = false;
		ForEachButton(Context, Layout, [&](const FButton& Button) {
			bPause |= Button.Action == EHUDAction::ActivePause;
			bPlanning |= IsPlanningAction(Button.Action);
		});
		Check(!Context.bPlanning && bPause && !bPlanning, TEXT("At 0:00 the panel goes and Pause is back"));
		Check(!PC->IsPlacingBuilding() && PC->IsHUDExpanded() && Feedback().IsEmpty(),
			TEXT("At 0:00 the armed KIT card, its hint and the Ready line are gone, so a click cannot buy a second Barracks"));
		Check(!MotionBlurSuppressed(PC), TEXT("and back on at 0:00"));
		const ACommandBuilding* Barracks = nullptr;
		for (const ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->OwningPlayerState == Host && Building->Kind == EBuildingKind::Barracks)
				Barracks = Building;
		const AArmyGroup* Force = Barracks ? Barracks->ForceGroup.Get() : nullptr;
		return Check(Barracks && Barracks->bForceConfigured && Barracks->ProductionRole == EUnitRole::Ranged && Force && Force->Orders.Num() == 1
				&& Force->Orders[0].Verb == EForceVerb::Attack,
			TEXT("The picked type and the queued first order start with the battle"));
	}
};
}

bool FPlanningUiFlowTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(PlanningUiFlowTests::FScenario(this));
	return true;
}
#endif
