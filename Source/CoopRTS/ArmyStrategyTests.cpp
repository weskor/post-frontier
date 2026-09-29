#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "EnemyCommander.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Headquarters.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyStrategyTest, "CoopRTS.Enemy.StrategicDecisions",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Observes the actual commander ticking and scoring. Scenario setup positions actors
// and creates casualties, but never chooses a plan or issues an enemy order.
class FEnemyStrategyScenario : public IAutomationLatentCommand
{
public:
	explicit FEnemyStrategyScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 145.)
		{
			Test->AddError(FString::Printf(TEXT("Enemy strategy timed out at stage %d: plan=%s reason=%s center=%s home=%s units=%d cost=%d wallet=%d"),
				Stage, State.IsValid() ? *State->EnemyPlan : TEXT("missing"),
				State.IsValid() ? *State->EnemyPlanRationale : TEXT("missing"),
				Enemy.IsValid() ? *Enemy->GetCenter().ToCompactString() : TEXT("missing"),
				Enemy.IsValid() ? *Enemy->HomeLocation.ToCompactString() : TEXT("missing"),
				Enemy.IsValid() ? Enemy->Units.Num() : -1, Enemy.IsValid() ? Enemy->GetReinforcementCost() : -1,
				State.IsValid() ? State->EnemyResources : -1));
			return true;
		}
		if (Stage == 0) return Begin(Now);
		if (!Enemy.IsValid() || !State.IsValid() || !Player.IsValid() || !Site.IsValid())
		{
			Test->AddError(TEXT("Live strategic actors disappeared"));
			return true;
		}
		switch (Stage)
		{
		case 1: // One non-emergency plan survives multiple scoring evaluations.
			if (Now - StageStarted < 4.4) return false;
			if (!Check(Enemy->OrderSerial == Serial && State->EnemyPlan == Plan
				&& !State->EnemyPlanRationale.IsEmpty() && State->EnemyPlanRationale.StartsWith(TEXT("COMMIT")),
				TEXT("Non-emergency goal remains committed over repeated planner evaluations without order replacement"))) return true;
			Stage = 2;
			StageStarted = Now;
			return false;
		case 2: // Site ownership must be caused by travelling members, not only an order label.
			if (Site->ControllingTeam != Enemy->TeamIndex) return false;
			if (!Check(FVector::Dist2D(Enemy->GetCenter(), Enemy->HomeLocation) > 250.f
				&& FVector::Dist2D(Enemy->GetCenter(), Site->GetActorLocation()) < 800.f,
				TEXT("Enemy physically travels to capture its selected neutral site"))) return true;
			// An owned site is lost to a real friendly capturer while enemy members are
			// repositioned beyond capture radius. Its scoring must select a contest.
			for (AArmyUnit* Unit : Enemy->Units)
				Unit->SetActorLocation(Enemy->HomeLocation + FVector(0.f, 0.f, 95.f),
					false, nullptr, ETeleportType::TeleportPhysics);
			Capturer = Player->Units[0];
			Capturer->SetActorLocation(Site->GetActorLocation() + FVector(0.f, 0.f, 95.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			Site->AdvanceCapture(18.f);
			if (!Check(Site->ControllingTeam == Player->TeamIndex,
				TEXT("Real friendly presence claims the site previously captured by enemy"))) return true;
			Capturer->SetActorLocation(Player->HomeLocation + FVector(0.f, 0.f, 95.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			Stage = 3;
			StageStarted = Now;
			return false;
		case 3:
			if (!State->EnemyPlan.StartsWith(TEXT("CONTEST"))) return false;
			if (!Check(Enemy->Order == EArmyOrder::Attack && State->EnemyPlanRationale.StartsWith(TEXT("COMMIT"))
				&& FVector::Dist2D(Enemy->Destination, Site->GetActorLocation()) < 100.f,
				TEXT("Planner naturally commits to contesting player-held territory"))) return true;
			Serial = Enemy->OrderSerial;
			// HQ threat interrupts contest before the nine-second commitment expires.
			Capturer->SetActorLocation(State->EnemyHeadquarters->GetActorLocation() + FVector(0.f, 1300.f, 95.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			Stage = 4;
			StageStarted = Now;
			return false;
		case 4:
		{
			if (!State->EnemyPlan.Equals(TEXT("DEFEND HQ"))) return false;
			if (!Check(Now - StageStarted < 6. && Enemy->OrderSerial > Serial
				&& Enemy->AttackTarget == Capturer.Get()
				&& State->EnemyPlanRationale.StartsWith(TEXT("EMERGENCY")),
				TEXT("Threatened enemy HQ urgently interrupts an active contest commitment to defend"))) return true;
			Capturer->SetActorLocation(Player->HomeLocation + FVector(0.f, 0.f, 95.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			// Real server damage creates four missing composition roles. Survivors are
			// placed away from home so retreat must travel before paid replacement.
			AArmyUnit* Shooter = Player->Units[1];
			for (const int32 Index : {5, 4, 2, 0})
			{
				AArmyUnit* Victim = Enemy->Units[Index];
				Victim->ReceiveAttack(Victim->Health, Shooter);
			}
			if (!Check(Enemy->Units.Num() == 2 && Enemy->GetReinforcementCost() > 0,
				TEXT("Server damage leaves a priced, badly depleted opposing roster"))) return true;
			for (int32 Index = 0; Index < Enemy->Units.Num(); ++Index)
				Enemy->Units[Index]->SetActorLocation(Site->GetActorLocation()
					+ FVector(-130.f + Index * 260.f, 0.f, 95.f),
					false, nullptr, ETeleportType::TeleportPhysics);
			Quote = Enemy->GetReinforcementCost();
			if (!Check(!Enemy->CanReinforceAtCurrentLocation() && State->EnemyResources >= Quote,
				TEXT("Damaged enemy is away from recovery source but can afford its missing roles"))) return true;
			Stage = 5;
			StageStarted = Now;
			return false;
		}
		case 5:
			if (!State->EnemyPlan.StartsWith(TEXT("RETREAT"))) return false;
			if (!Check(Enemy->Order == EArmyOrder::Retreat && Enemy->Units.Num() == 2
				&& State->EnemyPlanRationale.StartsWith(TEXT("EMERGENCY")),
				TEXT("Depleted enemy interrupts defense and retreats without a free replacement"))) return true;
			RetreatStart = Enemy->GetCenter();
			Stage = 6;
			StageStarted = Now;
			return false;
		case 6:
			if (Enemy->Units.Num() == 2)
			{
				if (FVector::Dist2D(Enemy->GetCenter(), RetreatStart) > 250.f) bRetreatTravelled = true;
				PreviousResources = State->EnemyResources;
				return false;
			}
			if (!Check(bRetreatTravelled && Enemy->Units.Num() == 6 && Enemy->CanReinforceAtCurrentLocation()
				&& State->EnemyResources >= PreviousResources - Quote
				&& State->EnemyResources <= PreviousResources - Quote + 2 * State->GetEnemyIncomePerSecond(),
				TEXT("Enemy reaches an eligible source and pays the exact missing-role quote, allowing at most two income ticks"))) return true;
			{
				bool Occupied[6] = {};
				for (const AArmyUnit* Unit : Enemy->Units)
				{
					if (!Check(IsValid(Unit) && Unit->IsAlive() && Unit->Group == Enemy.Get()
						&& Unit->CompositionSlot >= 0 && Unit->CompositionSlot < 6
						&& !Occupied[Unit->CompositionSlot]
						&& Unit->UnitRole == (Unit->CompositionSlot < 2 ? EUnitRole::Frontline
							: Unit->CompositionSlot < 4 ? EUnitRole::Ranged : EUnitRole::Siege),
						TEXT("Purchased members restore unique frontline, ranged and siege composition slots"))) return true;
					Occupied[Unit->CompositionSlot] = true;
				}
			}
			// Remove HQ defenders from the scoring radius; the restored six-unit enemy
			// now has an actual advantage, without injecting an Attack plan or order.
			for (TActorIterator<AArmyGroup> It(Enemy->GetWorld()); It; ++It)
				if (It->TeamIndex == Player->TeamIndex)
					for (AArmyUnit* Unit : It->Units)
						Unit->SetActorLocation(FVector(-500.f, -3000.f, 95.f),
							false, nullptr, ETeleportType::TeleportPhysics);
			for (AArmyUnit* Unit : Enemy->Units)
				Unit->SetActorLocation(State->FriendlyHeadquarters->GetActorLocation() + FVector(750.f, 0.f, 95.f),
					false, nullptr, ETeleportType::TeleportPhysics);
			Stage = 7;
			StageStarted = Now;
			return false;
		case 7:
			if (!State->EnemyPlan.Equals(TEXT("ATTACK HQ"))) return false;
			if (!Check(Enemy->Order == EArmyOrder::Attack
				&& Enemy->AttackTarget == State->FriendlyHeadquarters
				&& State->EnemyPlanRationale.Contains(TEXT("advantage"))
				&& FMath::IsNearlyEqual(FVector::Dist2D(Enemy->Destination,
					State->FriendlyHeadquarters->GetActorLocation()), 650.f, 125.f),
				TEXT("Restored advantage produces a natural targeted assault on the player HQ"))) return true;
			Test->AddInfo(TEXT("Live scoring, capture, contest, commitment, emergency HQ defense, paid retreat and advantageous HQ assault passed."));
			return true;
		default: return true;
		}
	}

private:
	bool Check(bool Condition, const TCHAR* Message)
	{
		if (!Condition) Test->AddError(Message);
		return Condition;
	}

	bool Begin(double Now)
	{
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetNetMode() == NM_Standalone)
				{ World = Context.World(); break; }
		if (!World) return false;
		ACommandGameState* Found = World->GetGameState<ACommandGameState>();
		AArmyGroup* Opponent = nullptr;
		AArmyGroup* Owned = nullptr;
		AEnemyCommander* Commander = nullptr;
		for (TActorIterator<AEnemyCommander> It(World); It && !Commander; ++It) Commander = *It;
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
		{
			if (It->bOpposingArmy) Opponent = *It;
			else if (It->ArmyIndex == 0) Owned = *It;
		}
		if (!Found || !Commander || !Opponent || !Owned || !Found->EnemyHeadquarters
			|| !Found->FriendlyHeadquarters || Found->CaptureSites.Num() != 3) return false;
		if (!Check(Commander->Army == Opponent && Found->MatchResult == EMatchResult::Ongoing
			&& Opponent->Units.Num() == 6 && Owned->Units.Num() == 6,
			TEXT("Live commander controls a complete opposing army in fresh Boot"))) return true;
		if (!Found->EnemyPlan.StartsWith(TEXT("CAPTURE"))) return false;
		for (ACapturePoint* Candidate : Found->CaptureSites)
			if (Found->EnemyPlan.EndsWith(FString::Printf(TEXT("SITE %d"), Candidate->SiteIndex + 1))) Site = Candidate;
		if (!Check(Site.IsValid() && Site->ControllingTeam == -1
			&& FVector::Dist2D(Opponent->Destination, Site->GetActorLocation()) < 100.f
			&& Opponent->Order == EArmyOrder::Attack,
			TEXT("Natural opening score chooses an actual neutral-site capture order"))) return true;
		Enemy = Opponent;
		Player = Owned;
		State = Found;
		Serial = Opponent->OrderSerial;
		Plan = Found->EnemyPlan;
		Stage = 1;
		StageStarted = Now;
		return false;
	}

	FAutomationTestBase* Test;
	const double Started;
	int32 Stage = 0;
	double StageStarted = 0.;
	uint32 Serial = 0;
	int32 Quote = 0;
	int32 PreviousResources = 0;
	bool bRetreatTravelled = false;
	FString Plan;
	FVector RetreatStart = FVector::ZeroVector;
	TWeakObjectPtr<AArmyGroup> Enemy;
	TWeakObjectPtr<AArmyGroup> Player;
	TWeakObjectPtr<AArmyUnit> Capturer;
	TWeakObjectPtr<ACapturePoint> Site;
	TWeakObjectPtr<ACommandGameState> State;
};

bool FEnemyStrategyTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEnemyStrategyScenario(this));
	return true;
}

#endif
