#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderNeverTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::Never) {}

private:
	bool RunScenario() override
	{
		if (Stage == 0)
		{
			if (!Check(FCommandService::SetRetreatThreshold(Wallet, Force.Get(), ERetreatThreshold::Never).IsAccepted()
						&& Force->RetreatThreshold == ERetreatThreshold::Never,
					TEXT("Owner selects Never retreat threshold")))
				return true;
			if (!Issue(EForceVerb::Attack, EnemyHome) || !KillTo(1))
				return true;
			Force->TickOrders();
			StartPosition = Force->GetCenter();
			SetStage(1);
		}
		if (!Check(Force->Verb == EForceVerb::Attack && Force->TargetRegionIndex == EnemyHome
					&& Force->Status == EForceStatus::Marching && Force->GetAliveCount() == 1,
				TEXT("Never permits a one-of-six Attack to continue instead of withdrawing")))
			return true;
		if (FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f)
			return Check(FVector::Dist2D(Force->GetCenter(), Force->Destination) < FVector::Dist2D(StartPosition, Force->Destination),
				TEXT("One-of-six Never force physically advances toward its Attack waypoint"));
		return false;
	}
};
}

VERB_WORLD_TEST(FVerbNeverTest, "ThresholdNever", Never)

#endif
