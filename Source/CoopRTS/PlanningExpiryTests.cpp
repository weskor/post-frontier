#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "DepositSite.h"
#include "PlanningFixture.h"

// Roster changes during planning, the sixty-second expiry with one human Ready and one not, default kits and the
// Drill Rig refund.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningExpiryWorldTest, "CoopRTS.Planning.Expiry",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace PlanningExpiryTests
{
class FScenario final : public PlanningFixture::FScenario
{
public:
	using PlanningFixture::FScenario::FScenario;

private:
	bool Step() override
	{
		switch (Stage)
		{
		case 0:
			return Join();
		case 1:
			return Leave();
		case 2:
			return Arrange();
		default:
			return Expire();
		}
	}

	bool Join()
	{
		if (!JevKitStands(2) || State->Planning.Kits.Num() != 2)
			return false;
		Third = SpawnCommander();
		if (!Check(Third.IsValid(), TEXT("A third commander can join")))
			return Done();
		Enter(1);
		return false;
	}

	bool Leave()
	{
		if (State->Planning.Kits.Num() != 3 || State->Planning.JevKits.Num() != 3)
		{
			if (StageSeconds() > 5.)
			{
				Check(false, TEXT("A commander who joins during planning gets a kit and JEV a matching slot"));
				return Done();
			}
			return false;
		}
		RemoveCommander(Third.Get());
		Enter(2);
		return false;
	}

	bool Arrange()
	{
		if (State->Planning.Kits.Num() != 2 || State->Planning.JevKits.Num() != 2 || !JevKitStands(2))
			return false;
		for (const FPlanningKit& Kit : State->Planning.Kits)
			if (!Check(Kit.Commander != Third.Get(), TEXT("A commander who leaves has their kit removed")))
				return Done();
		FVector Spot;
		const TArray<ADepositSite*> Free = OwnFreeDeposits();
		if (!Check(BarracksSpot(0, Spot) && Free.Num() >= 2, TEXT("The map offers a Barracks spot and two own deposits")))
			return Done();
		const FCommandResult Placed = FPlanningCommands::PlaceKit(Host, EBuildingKind::Barracks, Spot);
		if (!Check(Placed.IsAccepted() && FPlanningCommands::SetReady(Host, true).IsAccepted(), TEXT("The host places a Barracks and is Ready")))
			return Done();
		HostBarracks = Placed.Building;
		HostSpot = Placed.Building->GetActorLocation();
		// Only the nearest own deposit stays free: the second commander's kit has none left.
		NearestDeposit = Free[0];
		for (int32 Index = 1; Index < Free.Num(); ++Index)
			Free[Index]->Extractor = HostBarracks;
		Enter(3);
		return false;
	}

	bool Expire()
	{
		ACommandPlayerState* Guest = GuestPtr.Get();
		if (State->IsPlanning())
		{
			if (!Check(State->GetPlanningEndCount() == 0 && World->IsPaused(), TEXT("One Ready of two does not end planning early")))
				return Done();
			if (State->Planning.SecondsRemaining <= 30.f)
				bSawHalf = true;
			return false;
		}
		const bool bHostFirst = Host->CommanderIndex < Guest->CommanderIndex;
		ACommandPlayerState* First = bHostFirst ? Host : Guest;
		ACommandPlayerState* Second = bHostFirst ? Guest : Host;
		if (!Check(bSawHalf, TEXT("The countdown passes thirty remaining real seconds"))
			|| !Check(State->GetPlanningEndCount() == 1 && State->GetPlanningEnd() == EPlanningEnd::Expired && !World->IsPaused(),
				TEXT("Sixty real seconds end planning exactly once"))
			|| !Check(State->GetPlanningSeconds() >= 59.9 && State->GetPlanningSeconds() < 62., TEXT("Planning lasts sixty real seconds")))
			return Done();
		const ACommandBuilding* FirstRig = KitPiece(First, EBuildingKind::Extractor);
		const ACommandBuilding* SecondRig = KitPiece(Second, EBuildingKind::Extractor);
		const ACommandBuilding* GuestBarracks = KitPiece(Guest, EBuildingKind::Barracks);
		const FVector Home = State->FriendlyHeadquarters->GetActorLocation();
		Check(HostBarracks && HostBarracks->GetActorLocation() == HostSpot, TEXT("A placed Barracks keeps its spot at expiry"));
		Check(GuestBarracks && GuestBarracks->IsComplete() && State->FindRegionAt(GuestBarracks->GetActorLocation()) == State->FindRegionAt(Home),
			TEXT("An unplaced Barracks is auto-placed finished in the main"));
		Check(FirstRig && FirstRig->IsComplete() && FirstRig->Deposit == NearestDeposit,
			TEXT("The first unplaced Rig defaults to the nearest free deposit in own territory"));
		Check(!SecondRig && Second->Resources == 200 + RigCost(), TEXT("With no free deposit left the Rig is refunded in Power instead"));
		Check(First->Resources == 200, TEXT("A placed Rig costs nothing"));
		return Done();
	}

	ACommandBuilding* KitPiece(const ACommandPlayerState* Owner, EBuildingKind Kind) const
	{
		for (ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->OwningPlayerState == Owner && Building->Kind == Kind)
				return Building;
		return nullptr;
	}

	int32 RigCost() const
	{
		const UBuildingDefinition* Rig = State->Content->Building(ArmyTestSetup::ExtractorIndex);
		return Rig ? Rig->BuildCost : -1;
	}

	TWeakObjectPtr<ACommandPlayerState> Third;
	ACommandBuilding* HostBarracks = nullptr;
	ADepositSite* NearestDeposit = nullptr;
	FVector HostSpot = FVector::ZeroVector;
	bool bSawHalf = false;
};
}

bool FPlanningExpiryWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(PlanningExpiryTests::FScenario(this, 240.));
	return true;
}

#endif
