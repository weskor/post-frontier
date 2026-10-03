#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderWithdrawalTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::Withdrawal) {}

private:
	bool RunScenario() override
	{
		if (const EStepResult Result = BeginWithdrawal(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (const EStepResult Result = ReachSafety(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (const EStepResult Result = AwaitRecruits(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (const EStepResult Result = ResumeAttack(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (Stage == 6)
		{
			if (!Check(Force->Verb == EForceVerb::Attack && Force->TargetRegionIndex == Target
						&& Force->Status == EForceStatus::Marching,
					TEXT("Resumed Attack retains its original target while marching")))
				return true;
			if (FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f)
				return Check(FVector::Dist2D(Force->GetCenter(), Force->Destination) < FVector::Dist2D(StartPosition, Force->Destination),
					TEXT("Eighty-percent joined force physically resumes its forward Attack route"));
		}
		return false;
	}
	EStepResult BeginWithdrawal()
	{
		if (Stage <= 2)
		{
			if (!BeginCombatTrip())
				return bFailed ? EStepResult::Finished : EStepResult::Waiting;
			if (!Check(Force->RetreatThreshold == ERetreatThreshold::Percent40, TEXT("Attack defaults to forty-percent retreat threshold")))
				return EStepResult::Finished;
			if (!KillTo(3))
				return EStepResult::Finished;
			Force->TickOrders();
			if (!Check(Force->Verb == EForceVerb::Attack && Force->Status != EForceStatus::Withdrawing
						&& Force->Status != EForceStatus::Refilling,
					TEXT("Fifty-percent Attack does not withdraw at forty-percent threshold")))
				return EStepResult::Finished;
			if (!KillTo(2))
				return EStepResult::Finished;
			Force->TickOrders();
			if (!Check(Force->Verb == EForceVerb::Attack && Force->TargetRegionIndex == Target
						&& Force->Status == EForceStatus::Withdrawing && Force->ResumeCount == 5,
					TEXT("Two of six trigger withdrawal without discarding Attack, with ceil eighty-percent resume count")))
				return EStepResult::Finished;
			SafeRegion = Force->WaypointRegionIndex;
			if (!Check(SafeRegion == Intermediate, TEXT("Withdrawal prefers the last held connected hostile-free region")))
				return EStepResult::Finished;
			AArmyUnit* Shooter = Force->GetUnits()[0];
			PutHostile(Shooter->GetActorLocation() + FVector(60.f, 0.f, 0.f));
			for (AArmyUnit* Unit : Force->GetUnits())
				Unit->NextAttackTime = 0.f;
			const uint32 Before = Attacks();
			TickForce();
			if (!Check(Attacks() > Before && !Shooter->bPursuing,
					TEXT("Withdrawing Attack actually fires at an in-range hostile without pursuit")))
				return EStepResult::Finished;
			SafeRegion = Force->WaypointRegionIndex;
			ParkHostile();
			StartPosition = Force->GetCenter();
			SetStage(3);
		}
		return EStepResult::Continue;
	}
	EStepResult ReachSafety()
	{
		if (Stage == 3)
		{
			if (Force->Status != EForceStatus::Refilling)
				return EStepResult::Waiting;
			if (!Check(At(Force.Get(), SafeRegion) && FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f
						&& Force->GetJoinedCount() == 2,
					TEXT("Fighting withdrawal physically returns to safety before refill")))
				return EStepResult::Finished;
			RefillBalance = Wallet->Resources;
			if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, true).IsAccepted(), TEXT("Owner starts paid withdrawal refill")))
				return EStepResult::Finished;
			SetStage(4);
		}
		return EStepResult::Continue;
	}
	EStepResult AwaitRecruits()
	{
		if (Stage == 4)
		{
			if (Force->GetAliveCount() < 5)
				return EStepResult::Waiting;
			if (!Check(Force->GetAliveCount() == 5 && Force->GetJoinedCount() < 5
						&& Force->Status == EForceStatus::Refilling,
					TEXT("Eighty-percent ALIVE including travelling recruits does not resume Attack")))
				return EStepResult::Finished;
			if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, false).IsAccepted(), TEXT("Pause refill at exactly five paid living members")))
				return EStepResult::Finished;
			SetStage(5);
		}
		return EStepResult::Continue;
	}
	EStepResult ResumeAttack()
	{
		if (Stage == 5)
		{
			if (Force->GetJoinedCount() < 5)
				return EStepResult::Waiting;
			Force->TickOrders();
			if (!Check(Force->GetJoinedCount() == 5 && Force->GetAliveCount() == 5 && Force->Verb == EForceVerb::Attack
						&& Force->TargetRegionIndex == Target && Force->Status == EForceStatus::Marching
						&& Wallet->Resources == RefillBalance - 3 * Producer->GetProductionCost(),
					TEXT("Five JOINED of six resume the retained Attack, charging only the three real replacements")))
				return EStepResult::Finished;
			StartPosition = Force->GetCenter();
			SetStage(6);
		}
		return EStepResult::Continue;
	}
};
}

VERB_WORLD_TEST(FVerbWithdrawalTest, "AttackWithdrawal", Withdrawal)

#endif
