#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "DepositSite.h"
#include "PlanningFixture.h"

// Kit placement and moves under the normal rules, the Rig's deposit, the unit type, first orders, the Ready lock
// and what 0:00 turns the kit into.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningKitWorldTest, "CoopRTS.Planning.Kit",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace PlanningKitTests
{
class FScenario final : public PlanningFixture::FScenario
{
public:
	using PlanningFixture::FScenario::FScenario;

private:
	bool Step() override
	{
		if (!JevKitStands(2) || State->Planning.Kits.Num() != 2)
		{
			if (StageSeconds() > 20.)
			{
				Check(false, TEXT("The kits and JEV's matching start stand"));
				return Done();
			}
			return false;
		}
		if (!Barracks() || !Rig() || !Orders() || !Lock())
			return Done();
		const FPlanningKit* Kit = State->FindKit(Host);
		ACommandBuilding* Producer = Kit->Barracks;
		if (!Check(FPlanningCommands::SetReady(Host, true).IsAccepted() && FPlanningCommands::SetReady(GuestPtr.Get(), true).IsAccepted()
					&& !State->IsPlanning(),
				TEXT("Both Ready ends planning")))
			return Done();
		const AArmyGroup* Force = Producer->ForceGroup;
		Check(Producer->bForceConfigured && Producer->ProductionRole == EUnitRole::Ranged, TEXT("The picked unit type is what the Barracks produces"));
		Check(Force && Force->Orders.Num() == 2 && Force->Orders[0].Verb == EForceVerb::MoveHold && Force->Orders[1].Verb == EForceVerb::Attack
				&& Force->Orders[1].Structure == JevBarracks.Get(),
			TEXT("The first orders are issued to the force at 0:00, queue order kept"));
		return Done();
	}

	bool Barracks()
	{
		FVector First, Second;
		const FVector Outside = ArmyTestSetup::OutsideArena(State);
		if (!Check(BarracksSpot(0, First) && BarracksSpot(1, Second), TEXT("Two distinct legal Barracks spots exist")))
			return false;
		FirstSpot = First;
		const FCommandResult Stranger = FPlanningCommands::PlaceKit(State->EnemyCommander, EBuildingKind::Barracks, First);
		const FCommandResult Workshop = FPlanningCommands::PlaceKit(Host, EBuildingKind::Workshop, First);
		const FCommandResult Off = FPlanningCommands::PlaceKit(Host, EBuildingKind::Barracks, Outside);
		if (!Check(!Stranger.IsAccepted() && !FPlanningCommands::SetReady(State->EnemyCommander, true).IsAccepted(),
				TEXT("Only roster commanders have a kit"))
			|| !Check(!Workshop.IsAccepted() && !Off.IsAccepted() && !Off.Message.IsEmpty() && !IsValid(State->FindKit(Host)->Barracks),
				TEXT("The kit is a Barracks and a Drill Rig placed under the normal rules, with a reason on refusal")))
			return false;
		const FCommandResult Placed = FPlanningCommands::PlaceKit(Host, EBuildingKind::Barracks, First);
		if (!Check(Placed.IsAccepted() && Placed.Building && Placed.Building->IsComplete() && Placed.Building->OwningPlayerState == Host
					&& Host->Resources == 200,
				TEXT("A kit Barracks is free and stands finished")))
			return false;
		const TWeakObjectPtr<ACommandBuilding> Old = Placed.Building;
		const FCommandResult Moved = FPlanningCommands::PlaceKit(Host, EBuildingKind::Barracks, Second);
		int32 Owned = 0;
		for (const ACommandBuilding* Building : State->Buildings)
			Owned += IsValid(Building) && Building->OwningPlayerState == Host && Building->Kind == EBuildingKind::Barracks;
		if (!Check(Moved.IsAccepted() && !Old.IsValid() && Moved.Building != Placed.Building && Owned == 1
					&& FVector::Dist2D(Moved.Building->GetActorLocation(), Second) < 100. && Host->Resources == 200,
				TEXT("Moving the Barracks replaces it for free")))
			return false;
		const FCommandResult Refused = FPlanningCommands::PlaceKit(Host, EBuildingKind::Barracks, Outside);
		const ACommandBuilding* Kept = State->FindKit(Host)->Barracks;
		return Check(!Refused.IsAccepted() && IsValid(Kept) && Kept->IsComplete() && Kept->OwningPlayerState == Host
				&& FVector::Dist2D(Kept->GetActorLocation(), Second) < 100.,
			TEXT("A refused move leaves the Barracks where it was"));
	}

	bool Rig()
	{
		const TArray<ADepositSite*> Free = OwnFreeDeposits();
		if (!Check(Free.Num() >= 2, TEXT("Two own deposits are free")))
			return false;
		const FCommandResult Placed = FPlanningCommands::PlaceKit(Host, EBuildingKind::Extractor, Free[0]->GetActorLocation() + FVector(100.f, 0.f, 0.f));
		if (!Check(Placed.IsAccepted() && Placed.Building->Deposit == Free[0] && Free[0]->Extractor == Placed.Building
					&& Placed.Building->IsComplete() && Host->Resources == 200,
				TEXT("The Drill Rig snaps to a free deposit and costs nothing")))
			return false;
		const FCommandResult Moved = FPlanningCommands::PlaceKit(Host, EBuildingKind::Extractor, Free[1]->GetActorLocation());
		if (!Check(Moved.IsAccepted() && Free[1]->Extractor == Moved.Building && !IsValid(Free[0]->Extractor) && Host->Resources == 200,
				TEXT("Moving the Rig takes the new deposit and frees the old one")))
			return false;
		const FCommandResult Refused = FPlanningCommands::PlaceKit(Host, EBuildingKind::Extractor, ArmyTestSetup::OutsideArena(State));
		const ACommandBuilding* Kept = State->FindKit(Host)->Rig;
		return Check(!Refused.IsAccepted() && IsValid(Kept) && Kept->IsComplete() && Free[1]->Extractor == Kept && !IsValid(Free[0]->Extractor),
			TEXT("A refused Rig move keeps its deposit"));
	}

	bool Orders()
	{
		const AMapRegion* Home = State->FindRegionAt(State->FriendlyHeadquarters->GetActorLocation());
		if (!Check(Home && !Home->Neighbours.IsEmpty() && State->Planning.JevKits[0].Barracks, TEXT("A neighbouring region and a JEV structure exist")))
			return false;
		const int32 Target = Home->Neighbours[0];
		AActor* Hostile = State->Planning.JevKits[0].Barracks;
		JevBarracks = State->Planning.JevKits[0].Barracks;
		AActor* Own = State->FindKit(Host)->Barracks;
		if (!Check(FPlanningCommands::SetFirstOrder(Host, EForceVerb::MoveHold, Target).IsAccepted()
					&& FPlanningCommands::SetFirstOrder(Host, EForceVerb::Attack, INDEX_NONE, Hostile, true).IsAccepted()
					&& FPlanningCommands::SetFirstOrder(Host, EForceVerb::MoveHold, Target, nullptr, true).IsAccepted()
					&& State->FindKit(Host)->Orders.Num() == 3,
				TEXT("A first order and two queued orders are accepted"))
			|| !Check(!FPlanningCommands::SetFirstOrder(Host, EForceVerb::MoveHold, Target, nullptr, true).IsAccepted(),
				TEXT("Three orders at most, as for any force"))
			|| !Check(!FPlanningCommands::SetFirstOrder(Host, EForceVerb::Retreat, INDEX_NONE).IsAccepted()
					&& !FPlanningCommands::SetFirstOrder(Host, EForceVerb::Attack, INDEX_NONE, Own).IsAccepted()
					&& !FPlanningCommands::SetFirstOrder(Host, EForceVerb::MoveHold, 99).IsAccepted(),
				TEXT("Retreat, attacking your own building and unknown regions are refused")))
			return false;
		return Check(FPlanningCommands::SetFirstOrder(Host, EForceVerb::MoveHold, Target).IsAccepted() && State->FindKit(Host)->Orders.Num() == 1
				&& FPlanningCommands::SetFirstOrder(Host, EForceVerb::Attack, INDEX_NONE, Hostile, true).IsAccepted(),
			TEXT("Without queueing a new order replaces the list"));
	}

	bool Lock()
	{
		if (!Check(FPlanningCommands::SetReady(Host, true).IsAccepted(), TEXT("Ready is accepted")))
			return false;
		const FVector Spot = FirstSpot;
		const FCommandResult Locked = FPlanningCommands::SetUnitType(Host, EUnitRole::Ranged);
		if (!Check(!Locked.IsAccepted() && !Locked.Message.IsEmpty()
					&& !FPlanningCommands::PlaceKit(Host, EBuildingKind::Barracks, Spot).IsAccepted()
					&& !FPlanningCommands::SetFirstOrder(Host, EForceVerb::MoveHold, State->FindRegionAt(Spot)->RegionIndex).IsAccepted()
					&& !FPlanningCommands::ClearFirstOrders(Host).IsAccepted() && State->FindKit(Host)->Orders.Num() == 2,
				TEXT("Ready locks placement, unit type and orders, with a reason")))
			return false;
		return Check(FPlanningCommands::SetReady(Host, false).IsAccepted() && FPlanningCommands::SetUnitType(Host, EUnitRole::Siege).IsAccepted()
				&& FPlanningCommands::SetUnitType(Host, EUnitRole::Ranged).IsAccepted() && State->FindKit(Host)->UnitRole == EUnitRole::Ranged,
			TEXT("Un-Ready unlocks edits and the last unit type wins"))
			&& Check(FPlanningCommands::ClearFirstOrders(Host).IsAccepted() && State->FindKit(Host)->Orders.IsEmpty()
					&& FPlanningCommands::SetFirstOrder(Host, EForceVerb::MoveHold, State->FindRegionAt(Spot)->RegionIndex).IsAccepted()
					&& FPlanningCommands::SetFirstOrder(Host, EForceVerb::Attack, INDEX_NONE, JevBarracks.Get(), true).IsAccepted(),
				TEXT("Orders can be cleared and set again once unlocked"));
	}

	TWeakObjectPtr<ACommandBuilding> JevBarracks;
	FVector FirstSpot = FVector::ZeroVector;
};
}

bool FPlanningKitWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(PlanningKitTests::FScenario(this));
	return true;
}

#endif
