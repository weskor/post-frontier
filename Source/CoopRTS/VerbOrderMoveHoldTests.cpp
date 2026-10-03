#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderMoveHoldTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::MoveHold) {}

private:
	bool RunScenario() override
	{
		if (Stage == 0)
		{
			const uint32 Serial = Force->OrderSerial;
			const EForceVerb BeforeVerb = Force->Verb;
			const int32 BeforeTarget = Force->TargetRegionIndex;
			const auto Unchanged = [&] { return Force->OrderSerial == Serial && Force->Verb == BeforeVerb
											 && Force->TargetRegionIndex == BeforeTarget; };
			AArmyGroup* const MixedOwnership[] = { Force.Get(), Hostile.Get() };
			AArmyGroup* const Duplicate[] = { Force.Get(), Force.Get() };
			if (!Check(!FCommandService::IssueForceOrder(Wallet, MixedOwnership, EForceVerb::MoveHold, Target).IsAccepted()
						&& Unchanged(),
					TEXT("Mixed owned/foreign selection rejects atomically before touching owned travel"))
				|| !Check(!FCommandService::IssueForceOrder(Wallet, Duplicate, EForceVerb::MoveHold, Target).IsAccepted()
						&& Unchanged(),
					TEXT("Duplicate selected force rejects without replacing its order")))
				return true;
			const ERetreatThreshold Threshold = Force->RetreatThreshold;
			if (!Check(!FCommandService::SetRetreatThreshold(EnemyWallet.Get(), Force.Get(), ERetreatThreshold::Never).IsAccepted()
						&& Force->RetreatThreshold == Threshold,
					TEXT("Foreign threshold command preserves owned setting"))
				|| !Check(!FCommandService::SetRetreatThreshold(Wallet, Force.Get(), static_cast<ERetreatThreshold>(41)).IsAccepted()
						&& Force->RetreatThreshold == Threshold,
					TEXT("Invalid threshold rejects without changing setting")))
				return true;
			if (!Issue(EForceVerb::MoveHold, Target))
				return true;
			if (!Check(Force->WaypointRegionIndex == Intermediate, TEXT("Two-step MoveHold begins at the adjacent neutral waypoint")))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(1);
		}
		bVisitedIntermediate |= Occupies(Intermediate);
		bVisitedTarget |= Occupies(Target);
		if (Stage == 1 && Holding(Target))
		{
			if (!Check(bVisitedIntermediate && bVisitedTarget && State->GetRegionController(Intermediate) == 0
						&& FVector::Dist2D(StartPosition, Force->GetCenter()) > 500.f,
					TEXT("MoveHold physically travels through and captures both real map regions")))
				return true;
			SetStage(2);
		}
		if (Stage == 2)
		{
			if (!Check(Holding(Target) && Force->Orders.Num() == 1, TEXT("Unqueued MoveHold persists at its captured region")))
				return true;
			if (ArmyTestSetup::GameSeconds(GameWorld) - StageGameStarted >= 1.)
				return true;
		}
		return false;
	}
};
}

VERB_WORLD_TEST(FVerbMoveHoldTest, "MoveHold", MoveHold)

#endif
