#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

// A force marches a long first leg, so its members walk to column or assigned slots that are not their box slots.
// It then waits at an intermediate anchor whose capture takes 16 s (the enemy holds it fully). While it waits,
// progress must follow the members' planned targets: no member is re-pathed or settled for standing in its own
// slot, none changes target, and the force is not re-ordered.
namespace VerbOrderColumnWaitTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::MoveHold) {}

private:
	bool AllAtRest() const
	{
		for (const AArmyUnit* Unit : Force->GetUnits())
		{
			const AAIController* AI = Cast<AAIController>(Unit->GetController());
			if (!AI || AI->GetMoveStatus() != EPathFollowingStatus::Idle || Unit->GetVelocity().Size2D() > 5.f
				|| Unit->FormationTarget.IsZero() || FVector::Dist2D(Unit->GetActorLocation(), Unit->FormationTarget) > 100.f)
				return false;
		}
		return true;
	}

	bool RunScenario() override
	{
		const double Now = ArmyTestSetup::GameSeconds(GameWorld);
		if (Stage == 0)
		{
			ACapturePoint* Anchor = Region(State, Intermediate)->Anchor;
			Anchor->ControllingTeam = 5;
			Anchor->CaptureProgress = -1.f;
			if (!Issue(EForceVerb::MoveHold, Target))
				return true;
			SetStage(1);
		}
		if (Stage == 1 && Force->WaypointRegionIndex == Intermediate && Region(State, Intermediate)->Contains(Force->GetCenter()) && AllAtRest())
		{
			// How far the planned targets are from the plain box slots this leg would otherwise have used.
			float Furthest = 0.f;
			for (const AArmyUnit* Unit : Force->GetUnits())
			{
				Targets.Add(Unit->FormationTarget);
				Furthest = FMath::Max(Furthest, static_cast<float>(FVector::Dist2D(Unit->FormationTarget,
					Force->Destination + ArmyGroupPolicy::FormationOffset({ false, 6, false }, Unit->GetCompositionSlot()))));
			}
			Test->AddInfo(FString::Printf(TEXT("At rest at the anchor; the planned targets lie up to %.0f cm from the box slots"), Furthest));
			if (!Check(Furthest > 100.f, TEXT("The leg planned slots that differ from the box slots, so the test can tell the two apart")))
				return true;
			Serial = Force->OrderSerial;
			Settled = Force->GetSettledUnitCount();
			RestStart = Now;
			SetStage(2);
		}
		if (Stage == 2 && Now - RestStart >= 7.)
		{
			if (!Check(State->GetRegionController(Intermediate) != 0, TEXT("The region is still the enemy's, so the force has been waiting")))
				return true;
			int32 Index = 0;
			for (const AArmyUnit* Unit : Force->GetUnits())
				if (!Check(FVector::Dist2D(Unit->FormationTarget, Targets[Index++]) <= 100.f,
						TEXT("No member's planned target changed while it waited")))
					return true;
			return Check(Force->OrderSerial == Serial && Force->GetSettledUnitCount() == Settled && AllAtRest(),
				TEXT("The force was not re-ordered, and no member was re-pathed or settled for standing in its own planned slot"));
		}
		return false;
	}

	TArray<FVector> Targets;
	uint32 Serial = 0;
	int32 Settled = 0;
	double RestStart = 0.;
};
}

VERB_WORLD_TEST(FVerbColumnWaitTest, "ColumnWait", ColumnWait)

#endif
