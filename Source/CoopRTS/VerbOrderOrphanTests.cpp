#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderOrphanTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::Orphan) {}

private:
	bool RunScenario() override
	{
		if (Stage == 0)
		{
			const int32 Number = Force->ForceNumber;
			Producer->ReceiveAttack(Producer->Health, Hostile->GetUnits()[0]);
			if (!Check(Force.IsValid() && !Force->GetProductionBuilding() && Force->GetAliveCount() == 6
						&& Force->GetOwningPlayerState() == Wallet && Force->ForceNumber == Number,
					TEXT("Real producer death leaves six owned orphan survivors with their force identity")))
				return true;
			if (!Issue(EForceVerb::MoveHold, Target))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(1);
		}
		if (Stage == 1 && Holding(Target))
		{
			if (!Check(Force->GetAliveCount() == 6 && FVector::Dist2D(StartPosition, Force->GetCenter()) > 500.f,
					TEXT("Orphan accepts and physically executes its new order without producer replacement"))
				|| !Issue(EForceVerb::Attack, EnemyHome))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(2);
		}
		if (Stage == 2 && FVector::Dist2D(StartPosition, Force->GetCenter()) >= 700.f)
		{
			if (!KillTo(2))
				return true;
			TickForce();
			if (!Check(Force->Status == EForceStatus::Withdrawing,
					TEXT("Two-of-six orphan starts automatic Attack withdrawal")))
				return true;
			SafeRegion = Force->WaypointRegionIndex;
			StartPosition = Force->GetCenter();
			SetStage(3);
		}
		if (Stage == 3 && Holding(SafeRegion))
			return Check(!Force->GetProductionBuilding() && Force->GetJoinedCount() == 2
					&& FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f,
				TEXT("Orphan Attack withdrawal physically arrives and becomes MoveHold instead of impossible refill"));
		return false;
	}
};
}

VERB_WORLD_TEST(FVerbOrphanTest, "Orphan", Orphan)

#endif
