#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderRetreatTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::Retreat) {}

private:
	bool RunScenario() override
	{
		if (const EStepResult Result = BeginRetreat(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (const EStepResult Result = ReplacePursuit(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (const EStepResult Result = ReachSafety(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (const EStepResult Result = AwaitRecruits(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (const EStepResult Result = AwaitJoined(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (const EStepResult Result = CompleteRefill(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (Stage == 7 && Holding(Producer->RallyRegionIndex))
			return Check(FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f,
				TEXT("Completed Retreat physically returns from safety to its distinct default rally"));
		return false;
	}
	EStepResult BeginRetreat()
	{
		if (Stage <= 2)
		{
			if (!BeginCombatTrip())
				return bFailed ? EStepResult::Finished : EStepResult::Waiting;
			if (!Holding(Target))
				return EStepResult::Waiting;
			if (!KillTo(3))
				return EStepResult::Finished;
			const FVector Center = Force->GetCenter();
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GameWorld);
			bool bPlaced = false;
			for (int32 Direction = 0; Nav && Direction < 8; ++Direction)
			{
				const float Angle = Direction * PI / 4.f;
				const FVector Candidate = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 900.f;
				FNavLocation Ground;
				if (Region(State, Target)->Contains(Candidate)
					&& Nav->ProjectPointToNavigation(Candidate, Ground, FVector(90.f, 90.f, 500.f))
					&& Region(State, Target)->Contains(Ground.Location))
				{
					PutHostile(FVector(Ground.Location.X, Ground.Location.Y, Center.Z));
					bPlaced = true;
					break;
				}
			}
			if (!Check(bPlaced, TEXT("Stationary hostile occupies reachable held-region ground outside weapon range")))
				return EStepResult::Finished;
			SetStage(16);
			return EStepResult::Waiting;
		}
		return EStepResult::Continue;
	}
	EStepResult ReplacePursuit()
	{
		if (Stage == 16)
		{
			TickForce();
			bool bObservedPursuit = false;
			for (const AArmyUnit* Unit : Force->GetUnits())
				if (const AAIController* AI = Cast<AAIController>(Unit->GetController());
					Unit->bPursuing && AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle)
					bObservedPursuit = true;
			if (!bObservedPursuit)
				return EStepResult::Waiting; // Await real shared-alarm movement before replacing it.
			Test->AddInfo(TEXT("Retreat replaces observed active regional combat pursuit."));
			AArmyUnit* Shooter = Force->GetUnits()[0];
			PutHostile(Shooter->GetActorLocation() + FVector(60.f, 0.f, 0.f));
			if (!Issue(EForceVerb::Retreat))
				return EStepResult::Finished;
			if (!Check(Force->Verb == EForceVerb::Retreat && Force->Status == EForceStatus::Retreating
						&& FMath::IsNearlyEqual(Force->GetMarchSpeed(), Force->GetBaseMarchSpeed() * 1.25f),
					TEXT("Explicit Retreat sprints at exactly twenty-five percent above base speed")))
				return EStepResult::Finished;
			SafeRegion = Force->WaypointRegionIndex;
			RetreatAttackCount = Attacks();
			for (AArmyUnit* Unit : Force->GetUnits())
				Unit->NextAttackTime = 0.f;
			TickForce();
			if (!Check(Attacks() == RetreatAttackCount, TEXT("Retreat does not fire at a live in-range hostile")))
				return EStepResult::Finished;
			for (const AArmyUnit* Unit : Force->GetUnits())
				if (!Check(!Unit->Target && !Unit->bPursuing
							&& FMath::IsNearlyEqual(Unit->GetCharacterMovement()->MaxWalkSpeed, Force->GetBaseMarchSpeed() * 1.25f),
						TEXT("Every retreating member clears combat and receives sprint movement speed")))
					return EStepResult::Finished;
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
			if (!Check(Attacks() == RetreatAttackCount, TEXT("Retreat route remains weapon-silent")))
				return EStepResult::Finished;
			if (Force->Status != EForceStatus::Refilling)
				return EStepResult::Waiting;
			if (!Check(At(Force.Get(), SafeRegion) && FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f,
					TEXT("Retreat physically reaches its safe region before refill")))
				return EStepResult::Finished;
			RefillBalance = Wallet->Resources;
			// Sprint is over. Refilling may fight even with production paused.
			AArmyUnit* Shooter = Force->GetUnits()[0];
			AArmyUnit* Victim = Hostile->GetUnits()[0];
			PutHostile(Shooter->GetActorLocation() + FVector(70.f, 0.f, 0.f));
			const int32 BeforeHealth = Victim->GetHealth();
			Shooter->NextAttackTime = 0.f;
			Shooter->FireAt(Victim);
			if (!Check(Victim->GetHealth() < BeforeHealth && Force->Status == EForceStatus::Refilling,
					TEXT("Retreat refilling permits actual in-range damage after weapon-silent sprint")))
				return EStepResult::Finished;
			ParkHostile();
			if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, true).IsAccepted(), TEXT("Owner starts paid Retreat refill")))
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
			if (!Check(Force->GetAliveCount() == 5
						&& FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, false).IsAccepted(),
					TEXT("Pause Retreat refill at exactly five paid living members")))
				return EStepResult::Finished;
			SetStage(5);
		}
		return EStepResult::Continue;
	}
	EStepResult AwaitJoined()
	{
		if (Stage == 5)
		{
			if (Force->GetJoinedCount() < 5)
				return EStepResult::Waiting;
			if (!Check(Force->GetJoinedCount() == 5 && Force->Verb == EForceVerb::Retreat && Force->Status == EForceStatus::Refilling,
					TEXT("Retreat requires full joined capacity rather than Attack's eighty-percent resume")))
				return EStepResult::Finished;
			if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, true).IsAccepted(),
					TEXT("Owner recruits the final Retreat refill member")))
				return EStepResult::Finished;
			SetStage(6);
		}
		return EStepResult::Continue;
	}
	EStepResult CompleteRefill()
	{
		if (Stage == 6)
		{
			if (Force->GetJoinedCount() < 6)
				return EStepResult::Waiting;
			if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, false).IsAccepted(), TEXT("Owner pauses completed Retreat refill")))
				return EStepResult::Finished;
			Force->TickOrders();
			if (!Check(Force->Verb == EForceVerb::MoveHold && Force->TargetRegionIndex == Producer->RallyRegionIndex
						&& Force->GetJoinedCount() == 6 && Wallet->Resources == RefillBalance - 3 * Producer->GetProductionCost(),
					TEXT("Full paid joined refill completes Retreat into idle MoveHold at the producer rally")))
				return EStepResult::Finished;
			StartPosition = Force->GetCenter();
			SetStage(7);
		}
		return EStepResult::Continue;
	}
};
}

VERB_WORLD_TEST(FVerbRetreatTest, "Retreat", Retreat)

#endif
