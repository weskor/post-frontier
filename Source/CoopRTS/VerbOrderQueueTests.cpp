#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderQueueTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::Queue) {}

private:
	bool RunScenario() override
	{
		if (const EStepResult Result = StartQueue(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (const EStepResult Result = AdvanceSecondOrder(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (const EStepResult Result = AdvanceThirdOrder(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (Stage == 3 && Holding(Target))
		{
			if (!Check(Force->Orders.Num() == 1, TEXT("Third and last order settles as persistent MoveHold"))
				|| !Issue(EForceVerb::Attack, Home))
				return true;
			if (!Check(Force->Verb == EForceVerb::Attack && Force->TargetRegionIndex == Home
						&& Force->Status == EForceStatus::Marching,
					TEXT("Attack on an already controlled clear region still begins physical travel")))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(4);
		}
		if (Stage == 4 && Holding(Home))
			return Check(FVector::Dist2D(StartPosition, Force->GetCenter()) > 500.f
					&& Force->MarchSpeed == 0.f,
				TEXT("Controlled-region Attack completes only after arrival and releases its speed cap"));
		return false;
	}
	EStepResult StartQueue()
	{
		if (Stage == 0)
		{
			if (!Issue(EForceVerb::MoveHold, Intermediate) || !Issue(EForceVerb::MoveHold, Home, nullptr, true)
				|| !Issue(EForceVerb::MoveHold, Target, nullptr, true))
				return EStepResult::Finished;
			const uint32 Serial = Force->OrderSerial;
			const FVector Destination = Force->Destination;
			if (!Check(!FCommandService::IssueForceOrder(Wallet, Force.Get(), EForceVerb::Attack, EnemyHome, nullptr, true).IsAccepted()
						&& Force->Orders.Num() == 3 && Force->TargetRegionIndex == Intermediate && Force->OrderSerial == Serial
						&& Force->Destination == Destination && Force->Orders[0].RegionIndex == Intermediate
						&& Force->Orders[1].RegionIndex == Home && Force->Orders[2].RegionIndex == Target,
					TEXT("Queue holds three TOTAL orders and rejects a fourth without replacing or reordering them")))
				return EStepResult::Finished;
			SetStage(1);
		}
		return EStepResult::Continue;
	}
	EStepResult AdvanceSecondOrder()
	{
		if (Stage == 1)
		{
			bVisitedIntermediate |= Occupies(Intermediate);
			if (Force->TargetRegionIndex != Intermediate)
			{
				if (!Check(Force->TargetRegionIndex == Home && Force->Orders.Num() == 2
							&& Force->Orders[0].RegionIndex == Home && Force->Orders[1].RegionIndex == Target
							&& bVisitedIntermediate && State->GetRegionController(Intermediate) == 0,
						TEXT("Second order starts only after physical first-region capture")))
					return EStepResult::Finished;
				SetStage(2);
			}
		}
		return EStepResult::Continue;
	}
	EStepResult AdvanceThirdOrder()
	{
		if (Stage == 2)
		{
			bVisitedHome |= At(Force.Get(), Home);
			if (Force->TargetRegionIndex != Home)
			{
				if (!Check(Force->TargetRegionIndex == Target && Force->Orders.Num() == 1 && Force->Orders[0].RegionIndex == Target
							&& bVisitedHome,
						TEXT("Third order starts only after the return-home arrival")))
					return EStepResult::Finished;
				SetStage(3);
			}
		}
		return EStepResult::Continue;
	}
};
}

VERB_WORLD_TEST(FVerbQueueTest, "Queue", Queue)

#endif
