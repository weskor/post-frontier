#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "SupplyDeliveryFixture.h"

// A withdrawn Attack force resumes at 80% of its capacity in joined members. Recruits in transit
// are strength on the way, not strength: they must not bring the resume forward.

namespace SupplyTests
{
class FResumeJoinedOnly : public FScenarioBase
{
public:
	using FScenarioBase::FScenarioBase;

	// Each stage returns true when the scenario is over, by failure or at the end.
protected:
	bool Run() override
	{
		switch (Stage)
		{
		case 0:
			return Fill();
		case 1:
			return AwaitFullForce();
		case 2:
			return AwaitWithdrawal();
		case 3:
			return AwaitReplacements();
		case 4:
			return AwaitLastReplacement();
		default:
			return AwaitResume();
		}
	}

private:
	bool Fill()
	{
		Produce();
		if (!Check(Joined() == 1, TEXT("The first recruit joins at the exit")))
			return true;
		for (int32 Index = 0; Index < 5; ++Index)
			if (!Check(Produce() == UnitCost(), TEXT("Each queued recruit is paid once")))
				return true;
		if (!Check(Force->RecruitsInTransit == 5 && Joined() == 1, TEXT("Five recruits are in transit to a force of one")))
			return true;
		SetStage(1);
		return false;
	}
	bool AwaitFullForce()
	{
		if (Joined() < 6)
			return !Check(StageSeconds() < 12., TEXT("Five queued recruits reach the force at home within the delay"));
		Fixture->SetController(Far, -1);
		if (!Check(FCommandService::IssueForceOrder(Wallet, Force.Get(), EForceVerb::Attack, Far).IsAccepted(),
				TEXT("The full force accepts an Attack order")))
			return true;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			AArmyUnit* Victim = FirstMember();
			Victim->ReceiveAttack((Victim->GetHealth() + Victim->GetShield()) * 10, Attacker.Get());
		}
		if (!Check(Force->GetAliveCount() == 2, TEXT("Four casualties leave two members")))
			return true;
		SetStage(2);
		return false;
	}
	bool AwaitWithdrawal()
	{
		if (Force->Status != EForceStatus::Withdrawing && Force->Status != EForceStatus::Refilling)
			return !Check(StageSeconds() < 4., TEXT("Losing four of six starts the automatic withdrawal"));
		if (!Check(Produce() == UnitCost() && Produce() == UnitCost() && Force->RecruitsInTransit == 2,
				TEXT("Two replacements are paid and in transit")))
			return true;
		SetStage(3);
		return false;
	}
	bool AwaitReplacements()
	{
		if (Joined() < 4)
			return !StillWithdrawn(12.);
		// Four joined plus one in transit is five, the resume count; only joined members count.
		if (!Check(Produce() == UnitCost() && Force->RecruitsInTransit == 1, TEXT("The last replacement is paid and in transit")))
			return true;
		SetStage(4);
		return false;
	}
	bool AwaitLastReplacement()
	{
		if (Joined() < 5)
			return !StillWithdrawn(12.);
		SetStage(5);
		return false;
	}
	bool AwaitResume()
	{
		if (Force->Status != EForceStatus::Marching)
			return !Check(StageSeconds() < 4., TEXT("Five joined members resume the retained Attack"));
		Check(Joined() == 5 && Force->Verb == EForceVerb::Attack && Force->TargetRegionIndex == Far,
			TEXT("At 5 joined of 6 the force resumes its retained Attack"));
		return true;
	}
	bool StillWithdrawn(double Seconds)
	{
		return Check(Force->Verb == EForceVerb::Attack && Force->Status != EForceStatus::Marching && StageSeconds() < Seconds,
			TEXT("Recruits in transit do not resume the Attack; fewer than 5 of 6 joined keeps the force withdrawn"));
	}
};
}

using namespace SupplyTests;
SUPPLY_WORLD_TEST(FSupplyResumeJoinedOnlyTest, "ResumeJoinedOnly", FResumeJoinedOnly(this))
#endif
