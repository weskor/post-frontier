#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "SupplyDeliveryFixture.h"
#include "FormationFitSite.h"
#include "MapRegion.h"
#include "NavigationSystem.h"
#include "Rules/ArmyGroupPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationFitRecruitTest, "CoopRTS.Forces.FormationFit.Recruit",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace FormationFitRecruit
{
// A force holds at a post 56 cm inside the border of Far with one member; a delivered recruit takes the next
// slot of the same fitted formation, so it stands at that slot, inside the region and clear of the border.
class FScenario : public SupplyTests::FScenarioBase
{
public:
	using FScenarioBase::FScenarioBase;

protected:
	bool Run() override
	{
		if (!bSited && !Site())
			return true;
		if (Stage < 2)
		{
			if (!PutForceInFar())
				return false;
			SetStage(2);
		}
		if (Stage == 2)
		{
			Produce();
			SetStage(3);
			return false;
		}
		if (Joined() < 2)
			return !Check(StageSeconds() < 12., TEXT("The recruit joins within the delivery delay"));
		if (SettledSince < 0.)
			SettledSince = Now();
		const AArmyUnit* Veteran = FirstMember();
		const AArmyUnit* Recruit = nullptr;
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (IsValid(Unit) && Unit != Veteran)
				Recruit = Unit;
		if (!Check(Recruit && Veteran, TEXT("The force has a veteran and a recruit")))
			return true;
		if (Recruit->GetVelocity().Size2D() > 5.f || Veteran->GetVelocity().Size2D() > 5.f || Now() - SettledSince < 3.)
			return false;
		return Verify(*Veteran, *Recruit);
	}

private:
	bool Site()
	{
		AMapRegion* Region = nullptr;
		for (AMapRegion* Candidate : State->Regions)
			if (IsValid(Candidate) && Candidate->RegionIndex == Far)
				Region = Candidate;
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GameWorld);
		// The fixture's hostile exists for lethal-damage scenarios; here it would raise the region alarm.
		if (Attacker.IsValid() && Attacker->GetGroup())
			Attacker->GetGroup()->Destroy();
		FormationFitSite::FSite Found;
		if (!Check(Region && Navigation && FormationFitSite::Find(*Navigation, *Region, 130., 56., Found),
				TEXT("Far needs a long border edge with open navigable ground along it")))
			return false;
		FormationFitSite::Apply(*Region, Found);
		FarRegion = Region;
		bSited = true;
		return true;
	}

	bool Verify(const AArmyUnit& Veteran, const AArmyUnit& Recruit)
	{
		const AMapRegion& Region = *FarRegion;
		const ArmyGroupPolicy::FFormation Formation{ true, Force->GetCapacity(), false };
		const ArmyGroupPolicy::FFit Fit = ArmyGroupPolicy::FitForce(Formation, Region.Polygon, Force->HoldPostLocation);
		const FVector VeteranSlot = ArmyGroupPolicy::FittedSlot(Formation, Fit, Region.Polygon, Veteran.GetCompositionSlot());
		const FVector RecruitSlot = ArmyGroupPolicy::FittedSlot(Formation, Fit, Region.Polygon, Recruit.GetCompositionSlot());
		const double SlotClearance = FormationFitSite::Clearance(Region, RecruitSlot);
		const double Miss = FVector::Dist2D(Recruit.GetActorLocation(), RecruitSlot);
		Test->AddInfo(FString::Printf(TEXT("Recruit slot %d: clearance %.0f cm, stands %.0f cm from it; fit scale %.2f yaw %.2f"),
			Recruit.GetCompositionSlot(), SlotClearance, Miss, Fit.Scale, Fit.Yaw));
		Test->AddInfo(FString::Printf(TEXT("Hold: holding=%d region=%d post=%d responding=%d postLocation=%s veteran=%s recruit=%s"),
			Force->IsHoldingRegion(), Force->HoldRegionIndex, Force->HoldPostIndex, Force->bHoldResponding,
			*Force->HoldPostLocation.ToCompactString(), *Veteran.GetActorLocation().ToCompactString(),
			*Recruit.GetActorLocation().ToCompactString()));
		Check(!Fit.bClamped && SlotClearance >= ArmyGroupPolicy::FitMargin - 1., TEXT("The post's formation fits inside Far with its margin"));
		Check(Region.Contains(Recruit.GetActorLocation()) && Region.Contains(Veteran.GetActorLocation()),
			TEXT("The veteran and the recruit both stand inside Far"));
		Check(Miss <= 60., TEXT("The delivered recruit stands at its fitted slot"));
		Check(FVector::Dist2D(Veteran.GetActorLocation(), VeteranSlot) <= 60.
				&& FVector::Dist2D(Veteran.GetActorLocation(), Recruit.GetActorLocation()) >= 45.,
			TEXT("The veteran holds its own fitted slot, apart from the recruit"));
		return true;
	}

	bool bSited = false;
	double SettledSince = -1.;
	TWeakObjectPtr<AMapRegion> FarRegion;
};
}

bool FFormationFitRecruitTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FormationFitRecruit::FScenario(this));
	return true;
}
#endif
