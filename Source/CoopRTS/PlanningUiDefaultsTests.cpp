#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "DepositSite.h"
#include "PlanningUiFixture.h"

// The dashed default-spot ghosts show where 0:00 will put what is still unplaced, and the end of planning agrees: the
// Barracks and the Drill Rig of an untouched kit land exactly on their ghosts. Enter asks before leaving them to it.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningUiDefaultsTest, "CoopRTS.Planning.UI.Defaults",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace PlanningUiDefaultsTests
{
using namespace CommandHUDPanels;

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
			// The Attack mode stays armed through the end of planning: it must not reach the battle.
			if (!GhostsShown())
				return false;
			Key(EKeys::A);
			return Check(PC->IsAssigningOrder(), TEXT("A arms the kit's Attack mode")) ? PressEnter(2) : false;
		case 2:
			return Settled() && Asked() ? PressEnter(3) : false;
		case 3:
			if (!Settled() || !Accepted())
				return false;
			Frames = 0;
			return Advance(4);
		default:
			return Settled() && Landed() ? Done() : false;
		}
	}

	bool Advance(int32 Next)
	{
		Enter(Next);
		return false;
	}
	bool PressEnter(int32 Next)
	{
		Key(EKeys::Enter);
		return Advance(Next);
	}

	// Both pieces of the untouched kit have a ghost: the Rig on the nearest free own deposit, the Barracks on a legal spot.
	bool GhostsShown()
	{
		const FPlanningGhosts& Ghosts = PC->GetPlanningGhosts();
		if (!Ghosts.bBarracks || !Ghosts.bRig)
		{
			if (StageSeconds() > 10.)
				Check(false, TEXT("An untouched kit shows both default-spot ghosts"));
			return false;
		}
		Barracks = Ghosts.Barracks;
		Rig = Ghosts.Rig;
		Deposit = OwnFreeDeposits()[0];
		const double Out = FVector::Dist2D(Barracks, State->FriendlyHeadquarters->GetActorLocation());
		return Check(Deposit && FVector::Dist2D(Rig, Deposit->GetActorLocation()) < 1., TEXT("The Rig's ghost is the nearest free own deposit"))
			&& Check(Out > 100. && Out < 2000. && State->FindRegionAt(Barracks) == State->FindRegionAt(State->FriendlyHeadquarters->GetActorLocation()),
				TEXT("The Barracks' ghost is near the headquarters, in its region"));
	}

	bool Asked()
	{
		return Check(!Kit()->bReady && Feedback() == Question(),
			TEXT("Enter with an untouched kit asks before leaving it to the defaults"));
	}

	bool Accepted()
	{
		return Check(Kit()->bReady && State->IsPlanning(), TEXT("The second Enter readies with the defaults"))
			&& Check(FPlanningCommands::SetReady(GuestPtr.Get(), true).IsAccepted() && !State->IsPlanning(), TEXT("and the last Ready ends the phase"));
	}

	// What 0:00 placed is what the ghosts showed.
	bool Landed()
	{
		const ACommandBuilding* Placed = nullptr;
		const ACommandBuilding* Extractor = nullptr;
		for (const ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->OwningPlayerState == Host)
				(Building->Kind == EBuildingKind::Barracks ? Placed : Extractor) = Building;
		Check(!PC->IsAssigningOrder() && Feedback().IsEmpty(), TEXT("The armed Attack mode and the planning line are gone at 0:00"));
		return Check(Placed && FVector::Dist2D(Placed->GetActorLocation(), Barracks) < 1., TEXT("The Barracks stands where its ghost was"))
			&& Check(Extractor && Extractor->Deposit == Deposit && FVector::Dist2D(Extractor->GetActorLocation(), Rig) < 1. && Host->Resources == 200,
				TEXT("The Drill Rig stands on its ghost's deposit and cost nothing"));
	}

	FVector Barracks = FVector::ZeroVector;
	FVector Rig = FVector::ZeroVector;
	ADepositSite* Deposit = nullptr;
};
}

bool FPlanningUiDefaultsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(PlanningUiDefaultsTests::FScenario(this));
	return true;
}
#endif
