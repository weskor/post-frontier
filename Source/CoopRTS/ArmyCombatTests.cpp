#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandPlayerController.h"
#include "EnemyCommander.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyCombatTest, "CoopRTS.Combat.Encounter",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Run alone in a fresh standalone Boot world. Observe real authoritative actors,
// order execution, health and motion over time; never share a world with movement tests.
class FArmyCombatScenario : public IAutomationLatentCommand
{
public:
	explicit FArmyCombatScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		if (!bIsolated)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
				if (UWorld* World = Context.World())
					if (World->IsGameWorld() && World->GetNetMode() == NM_Standalone)
					{
						for (TActorIterator<AEnemyCommander> It(World); It; ++It) It->Destroy();
						bIsolated = true; // This one historical encounter deliberately owns the enemy orders.
						break;
					}
		}
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 65.)
		{
			Test->AddError(FString::Printf(TEXT("Combat encounter timed out at stage %d"), Stage));
			return true;
		}
		if (Stage == 0) return Begin(Now);
		if (Stage == 7)
		{
			if (!Army.IsValid() || !Victim.IsValid()) { Test->AddError(TEXT("Encounter disappeared before Attack was accepted")); return true; }
			if (!Army->IssueAttack(Army->GetHomeLocation() + FVector(400.f, 0.f, 0.f), Victim.Get())) return false;
			if (!Check(Army->Order == EArmyOrder::Attack && Army->AttackTarget == Victim.Get(),
				TEXT("Accepted Attack records the explicit enemy and location"))) return true;
			EncounterAttacks = TotalAttacks();
			SetStage(1, Now);
			return false;
		}
		if (!Check(Army.IsValid() && Enemy.IsValid() && Controller.IsValid(), TEXT("Encounter groups and owner remain valid"))) return true;

		switch (Stage)
		{
		case 1: // Attack is a real encounter; pursuit is bounded around the accepted destination.
			for (AArmyUnit* Unit : Army->GetUnits())
				if (!Check(FVector::Dist2D(Unit->GetActorLocation(), Army->Destination) <= Army->PursuitRadius + 200.f,
					TEXT("Attack member remains inside its pursuit boundary"))) return true;
			if (Now - StageStarted < 3.) break;
			if (!Check(TotalAttacks() > EncounterAttacks,
				TEXT("Live encounter causes a weapon attack, not just a recorded order"))) return true;
			Victim = Enemy->GetUnits().Last();
			if (!Check(Victim.IsValid() && Victim->IsAlive(), TEXT("Boundary probe has a live hostile"))) return true;
			Victim->SetActorLocation(Army->Destination + FVector(700.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			if (!Check(Army->IssueAttack(Army->Destination, Victim.Get()), TEXT("Boundary probe accepts a targeted Attack"))) return true;
			Victim->SetActorLocation(Army->Destination + FVector(0.f, Army->PursuitRadius + 700.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			SetStage(8, Now);
			break;
		case 8:
			if (Now - StageStarted < .7) break;
			if (!Check(!Army->AttackTarget, TEXT("Target leaving pursuit area is invalidated"))) return true;
			for (AArmyUnit* Unit : Army->GetUnits())
				if (!Check(Unit->Target != Victim.Get()
					&& FVector::Dist2D(Unit->GetActorLocation(), Army->Destination) <= Army->PursuitRadius + 200.f,
					TEXT("Members do not follow a target beyond Attack boundary"))) return true;
			{
				const uint32 Serial = Army->OrderSerial;
				const FVector MoveGoal = Army->GetHomeLocation() + FVector(0.f, -600.f, 0.f);
				Controller->ServerIssueOrder(Army.Get(), EArmyOrder::Move, MoveGoal);
				if (!Check(Army->Order == EArmyOrder::Move && Army->OrderSerial > Serial && !Army->AttackTarget,
					TEXT("Move replaces Attack and clears the explicit target"))) return true;
				for (AArmyUnit* Unit : Army->GetUnits())
					if (!Check(!Unit->Target, TEXT("Move clears each unit's stale attack target immediately"))) return true;
				// Leave a live defender within siege range but off the Move route.
				if (!Check(Enemy->GetUnits().Num() >= 3, TEXT("Move probe has a live defender"))) return true;
				Enemy->GetUnits()[2]->SetActorLocation(Army->GetHomeLocation() + FVector(950.f, 450.f, 0.f),
					false, nullptr, ETeleportType::TeleportPhysics);
				MoveStart = Army->GetCenter();
				SetStage(2, Now);
			}
			break;
		case 2:
			if (Now - StageStarted < 2.) break;
			if (!Check(FVector::Dist2D(Army->GetCenter(), Army->Destination) + 70.f < FVector::Dist2D(MoveStart, Army->Destination),
				TEXT("Move follows its destination rather than pursuing the encounter"))) return true;
			for (AArmyUnit* Unit : Army->GetUnits())
				if (!Check(FVector::Dist2D(Unit->GetActorLocation(), Army->Destination) < 750.f,
					TEXT("Every Move member stays with its ordered route instead of chasing a nearby enemy"))) return true;
			Controller->ServerIssueOrder(Army.Get(), EArmyOrder::Hold, FVector::ZeroVector);
			if (!Check(Army->Order == EArmyOrder::Hold, TEXT("Hold replaces Move during the encounter"))) return true;
			HeldPositions.Reset();
			for (AArmyUnit* Unit : Army->GetUnits()) HeldPositions.Add(Unit->GetActorLocation());
			SetStage(3, Now);
			break;
		case 3:
			if (Now - StageStarted < 1.) break;
			if (!Check(Army->GetUnits().Num() == HeldPositions.Num(), TEXT("Hold retains the living formation"))) return true;
			for (int32 Index = 0; Index < HeldPositions.Num(); ++Index)
				if (!Check(FVector::Dist2D(Army->GetUnits()[Index]->GetActorLocation(), HeldPositions[Index]) < 6.f,
					TEXT("Hold remains position-bound while a hostile army is nearby"))) return true;
			if (!Check(Enemy->GetUnits().Num() >= 3, TEXT("A live hostile remains for a replacement Attack"))) return true;
			Victim = Enemy->GetUnits()[2];
			Victim->SetActorLocation(Army->GetHomeLocation() + FVector(850.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			if (!Check(Army->IssueAttack(Army->GetHomeLocation() + FVector(400.f, 0.f, 0.f), Victim.Get()),
				TEXT("Attack can replace Hold with a live hostile target"))) return true;
			SetStage(4, Now);
			break;
		case 4:
			if (Now - StageStarted < 1.) break;
			if (!Check(Army->Order == EArmyOrder::Attack, TEXT("Army is engaging before Retreat"))) return true;
			Controller->ServerIssueOrder(Army.Get(), EArmyOrder::Retreat, FVector::ZeroVector);
			if (!Check(Army->Order == EArmyOrder::Retreat && !Army->AttackTarget,
				TEXT("Retreat replaces combat and clears the group target"))) return true;
			for (AArmyUnit* Unit : Army->GetUnits())
				if (!Check(!Unit->Target, TEXT("Retreat clears every unit's combat target"))) return true;
			RetreatAttacks = TotalAttacks();
			SetStage(5, Now);
			break;
		case 5:
			if (Now - StageStarted < 3.5) break; // Longer than even the siege attack interval.
			if (!Check(TotalAttacks() == RetreatAttacks && Army->Order == EArmyOrder::Retreat,
				TEXT("Retreat prevents all combat attacks even with an enemy in range"))) return true;
			for (AArmyUnit* Unit : Army->GetUnits())
				if (!Check(!Unit->Target, TEXT("No retreating member reacquires a target"))) return true;
			// Kill a live enemy via the same server damage entry point as a weapon.
			Victim = Enemy->GetUnits().Last();
			if (!Check(Victim.IsValid() && Victim->IsAlive(), TEXT("A live opposing unit is available for death check"))) return true;
			Victim->SetActorLocation(Army->GetHomeLocation() + FVector(850.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			if (!Check(Army->IssueAttack(Army->GetHomeLocation() + FVector(400.f, 0.f, 0.f), Victim.Get())
				&& Army->AttackTarget == Victim.Get(), TEXT("Targeted Attack records a live enemy"))) return true;
			{
				AArmyUnit* Shooter = Army->GetUnits()[0];
				Victim->ReceiveAttack(Victim->GetHealth(), Shooter);
				if (!Check(!Victim->IsAlive() && Victim->GetHealth() == 0 && !Enemy->GetUnits().Contains(Victim.Get()),
					TEXT("Server lethal damage removes the dead unit from its group"))) return true;
			}
			SetStage(6, Now);
			break;
		case 6:
			if (Now - StageStarted < .6) break;
			if (!Check(!Army->AttackTarget, TEXT("Dead explicit target is invalidated on the next acquisition"))) return true;
			for (AArmyUnit* Unit : Army->GetUnits())
				if (!Check(Unit->Target != Victim.Get(), TEXT("No unit retains a dead target"))) return true;
			Army->IssueHold();
			Test->AddInfo(TEXT("Live combat passed: role-specific effective range, server damage/death, target invalidation, bounded Attack, Move and Hold boundaries, Retreat override and stale-intent replacement."));
			return true;
		default: break;
		}
		return false;
	}

private:
	bool Check(bool Condition, const TCHAR* Message)
	{
		if (!Condition) Test->AddError(Message);
		return Condition;
	}

	void SetStage(int32 Next, double Now) { Stage = Next; StageStarted = Now; }

	uint32 TotalAttacks() const
	{
		uint32 Count = 0;
		for (const AArmyUnit* Unit : Army->GetUnits()) Count += Unit->AttackCount;
		return Count;
	}

	bool Begin(double Now)
	{
		if (Now - Started < 3.) return false; // Dynamic navmesh and AI controllers initialize asynchronously.
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetNetMode() == NM_Standalone)
				{ World = Context.World(); break; }
		if (!World) return false;
		if (!ArmyTestSetup::CombatActors(World)) return false;
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
		{
			if (It->IsOpposingArmy()) Enemy = *It;
			else if (It->GetArmyIndex() == 0 && It->GetUnits().Num() == 6)
				if (ACommandPlayerController* Owner = Cast<ACommandPlayerController>(It->GetOwner()))
					if (Owner->IsLocalController()) { Army = *It; Controller = Owner; }
		}
		if (!Army.IsValid() || !Enemy.IsValid() || !Controller.IsValid()) return false;
		if (!Check(Army->GetUnits().Num() == 6 && Enemy->GetUnits().Num() == 6 && Army->GetTeamIndex() != Enemy->GetTeamIndex(),
			TEXT("Fresh Boot has two opposed six-unit armies"))) return true;
		AArmyUnit* Front = Army->GetUnits()[0];
		AArmyUnit* Ranged = Army->GetUnits()[2];
		AArmyUnit* Siege = Army->GetUnits()[4];
		if (!Check(Front->GetUnitRole() == EUnitRole::Frontline && Ranged->GetUnitRole() == EUnitRole::Ranged && Siege->GetUnitRole() == EUnitRole::Siege
			&& Front->WeaponRange() < Ranged->WeaponRange() && Ranged->WeaponRange() < Siege->WeaponRange(),
			TEXT("Frontline, ranged, siege spawn with strictly increasing weapon ranges"))) return true;
		Victim = Enemy->GetUnits()[0];
		const FVector OriginalPosition = Victim->GetActorLocation();
		// FireAt is the authoritative weapon path. Probe just outside and inside each
		// role's effective range on a living hostile; one update prevents AI ticks
		// or cooldowns from contaminating the boundary observations.
		for (AArmyUnit* Shooter : {Front, Ranged, Siege})
		{
			const int32 Health = Victim->GetHealth();
			const uint32 Shots = Shooter->AttackCount;
			Victim->SetActorLocation(Shooter->GetActorLocation() + FVector(Shooter->WeaponRange() + 100.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			Shooter->FireAt(Victim.Get());
			if (!Check(Victim->GetHealth() == Health && Shooter->AttackCount == Shots,
				TEXT("Weapon cannot hit beyond its own role range"))) return true;
			Victim->SetActorLocation(Shooter->GetActorLocation() + FVector(Shooter->WeaponRange() - 75.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			Shooter->FireAt(Victim.Get());
			if (!Check(Victim->GetHealth() < Health && Shooter->AttackCount == Shots + 1,
				TEXT("Role weapon applies authoritative damage within its own range"))) return true;
		}
		Victim->SetActorLocation(OriginalPosition, false, nullptr, ETeleportType::TeleportPhysics);
		Army->IssueHold(); // Reset target acquired by the direct range probes.
		Victim = Enemy->GetUnits()[1]; // Fresh defender: range probes do not pre-damage the encounter target.
		// Bring one frontline defender into the army's approach, leaving the other
		// five defenders at their arena spawn. Both armies still run their real AI.
		Victim->SetActorLocation(Army->GetHomeLocation() + FVector(850.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		SetStage(7, Now); // A rejected request while dynamic navigation starts is safe to retry.
		return false;
	}

	FAutomationTestBase* Test;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Army;
	TWeakObjectPtr<AArmyGroup> Enemy;
	TWeakObjectPtr<AArmyUnit> Victim;
	TArray<FVector> HeldPositions;
	FVector MoveStart = FVector::ZeroVector;
	uint32 EncounterAttacks = 0;
	uint32 RetreatAttacks = 0;
	int32 Stage = 0;
	bool bIsolated = false;
	double Started;
	double StageStarted = 0.;
};

bool FArmyCombatTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FArmyCombatScenario(this));
	return true;
}

#endif
