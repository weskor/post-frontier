#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderRallyTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::Rally) {}

private:
	bool RunScenario() override
	{
		bVisitedIntermediate |= Occupies(Intermediate);
		if (Stage == 0 && Holding(Target))
		{
			if (!Check(bVisitedIntermediate && Force->GetJoinedCount() == 6 && Force->GetAliveCount() == 6
						&& Producer->RallyRegionIndex == Target && State->GetRegionController(Intermediate) == 0,
					TEXT("Six paid recruits route to non-home rally, join there and secure the route")))
				return true;
			TArray<int32> Saved = MoveTemp(Region(State, Home)->Neighbours);
			const int32 Rally = Producer->RallyRegionIndex;
			const bool bRejected = !FCommandService::SetRallyPoint(Wallet, Producer.Get(), Target).IsAccepted();
			Region(State, Home)->Neighbours = MoveTemp(Saved);
			if (!Check(bRejected && Producer->RallyRegionIndex == Rally,
					TEXT("Unreachable but valid rally region rejects without mutating producer")))
				return true;
			if (!Check(FCommandService::SetRallyPoint(Wallet, Producer.Get(), Home).IsAccepted()
						&& Force->Verb == EForceVerb::MoveHold && Force->TargetRegionIndex == Home,
					TEXT("Rally change retargets an existing idle force")))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(1);
		}
		if (Stage == 1 && Holding(Home))
		{
			if (!Check(FVector::Dist2D(StartPosition, Force->GetCenter()) > 500.f,
					TEXT("Idle force physically follows its changed producer rally"))
				|| !Check(FCommandService::SetRallyPoint(Wallet, Producer.Get(), Target).IsAccepted(),
					TEXT("Idle producer starts another rally trip before its destruction")))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(2);
		}
		if (Stage == 2 && FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f)
		{
			Producer->ReceiveAttack(Producer->Health, Hostile->GetUnits()[0]);
			StartPosition = Force->GetCenter();
			TickForce();
			if (!Check(!Force->GetProductionBuilding() && Force->Status == EForceStatus::Holding
						&& Force->Verb == EForceVerb::MoveHold && Force->TargetRegionIndex == CurrentRegion(Force.Get())
						&& Force->HoldRegionIndex == INDEX_NONE && Force->HoldPostIndex == INDEX_NONE,
					TEXT("Producer death stops an idle rally trip in place without implicit posts")))
				return true;
			SetStage(3);
		}
		if (Stage == 3 && ArmyTestSetup::GameSeconds(GameWorld) - StageGameStarted > 1.)
			return Check(FVector::Dist2D(StartPosition, Force->GetCenter()) < 5.f
					&& Force->Status == EForceStatus::Holding,
				TEXT("New idle orphan remains physically stopped instead of following the dead producer rally"));
		return false;
	}
};
}

VERB_WORLD_TEST(FVerbRallyTest, "Rally", Rally)

#endif
