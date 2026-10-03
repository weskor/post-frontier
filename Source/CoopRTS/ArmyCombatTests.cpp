#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CombatTarget.h"
#include "CommandPlayerController.h"
#include "EnemyCommander.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Headquarters.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyCombatTest, "CoopRTS.Combat.Encounter",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Run alone in a fresh standalone world. Explicit mixed-role fixtures exercise
// authoritative combat and internal orders, not starting forces or paid production.
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
						for (TActorIterator<AEnemyCommander> It(World); It; ++It)
							It->Destroy();
						bIsolated = true; // The fixture, not the enemy planner, owns encounter orders.
						break;
					}
		}
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 65.)
		{
			Test->AddError(FString::Printf(TEXT("Combat encounter timed out at stage %d"), Stage));
			return true;
		}
		if (Stage == 0)
			return Begin(Now);
		if (Stage == 7)
		{
			if (!Army.IsValid() || !Enemy.IsValid() || !Victim.IsValid())
			{
				Test->AddError(TEXT("Encounter disappeared before Attack was accepted"));
				return true;
			}
			AArmyUnit* Frontline = Army->GetUnits()[0];
			AArmyUnit* CounterTarget = Enemy->GetUnits()[2];
			const FVector CounterPosition = CounterTarget->GetActorLocation();
			CounterTarget->SetActorLocation(Frontline->GetActorLocation() + FVector(100.f, 0.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			for (AArmyUnit* Unit : Army->GetUnits())
				Unit->NextAttackTime = TNumericLimits<float>::Max();
			static_cast<AActor*>(Army.Get())->Tick(.25f);
			if (!Check(Frontline->Target == CounterTarget,
					TEXT("Automatic acquisition establishes a living Light counter lock before the targeted order")))
				return true;
			if (!FCommandService::IssueAttack(Army->GetOwningPlayerState(), Army.Get(), Army->GetHomeLocation() + FVector(400.f, 0.f, 0.f), Victim.Get()))
			{
				CounterTarget->SetActorLocation(CounterPosition, false, nullptr, ETeleportType::TeleportPhysics);
				return false;
			}
			if (!Check(Army->Order == EArmyOrder::Attack && Army->AttackTarget == Victim.Get(),
					TEXT("Accepted Attack records the explicit enemy and location")))
				return true;
			static_cast<AActor*>(Army.Get())->Tick(.25f);
			if (!Check(Frontline->Target == Victim.Get(),
					TEXT("Real targeted IssueAttack interrupts the prior counter lock and selects explicit Heavy under Attack")))
				return true;
			// Remove the explicit hint to probe retained-target eligibility under Attack,
			// beyond melee range but still inside the real order's pursuit leash.
			Army->AttackTarget = nullptr;
			CounterTarget->SetActorLocation(Frontline->GetActorLocation() + FVector(75.f, 0.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			static_cast<AActor*>(Army.Get())->Tick(.25f);
			if (!Check(Army->Order == EArmyOrder::Attack && Frontline->Target == Victim.Get()
						&& FVector::Dist2D(Frontline->GetActorLocation(), Victim->GetActorLocation()) > Frontline->WeaponRange()
						&& FVector::Dist2D(Victim->GetActorLocation(), Army->Destination) < Army->PursuitRadius,
					TEXT("Attack retains its eligible non-counter lock inside the leash despite a closer counter in melee range")))
				return true;
			Army->AttackTarget = Victim.Get();
			CounterTarget->SetActorLocation(CounterPosition, false, nullptr, ETeleportType::TeleportPhysics);
			for (AArmyUnit* Unit : Army->GetUnits())
				Unit->NextAttackTime = 0.f;
			EncounterAttacks = TotalAttacks();
			SetStage(1, Now);
			return false;
		}
		if (!Check(Army.IsValid() && Enemy.IsValid() && Controller.IsValid(), TEXT("Encounter groups and owner remain valid")))
			return true;

		switch (Stage)
		{
		case 1: // Attack is a real encounter; pursuit is bounded around the accepted destination.
			for (AArmyUnit* Unit : Army->GetUnits())
				if (!Check(FVector::Dist2D(Unit->GetActorLocation(), Army->Destination) <= Army->PursuitRadius + 200.f,
						TEXT("Attack member remains inside its pursuit boundary")))
					return true;
			if (Now - StageStarted < 3.)
				break;
			if (!Check(TotalAttacks() > EncounterAttacks,
					TEXT("Live encounter causes a weapon attack, not just a recorded order")))
				return true;
			Victim = Enemy->GetUnits().Last();
			if (!Check(Victim.IsValid() && Victim->IsAlive(), TEXT("Boundary probe has a live hostile")))
				return true;
			Victim->SetActorLocation(Army->Destination + FVector(700.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			if (!Check(FCommandService::IssueAttack(Army->GetOwningPlayerState(), Army.Get(), Army->Destination, Victim.Get()).IsAccepted(), TEXT("Boundary probe accepts a targeted Attack")))
				return true;
			Victim->SetActorLocation(Army->Destination + FVector(0.f, Army->PursuitRadius + 700.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			SetStage(8, Now);
			break;
		case 8:
			if (Now - StageStarted < .7)
				break;
			if (!Check(!Army->AttackTarget, TEXT("Target leaving pursuit area is invalidated")))
				return true;
			for (AArmyUnit* Unit : Army->GetUnits())
				if (!Check(Unit->Target != Victim.Get()
							&& FVector::Dist2D(Unit->GetActorLocation(), Army->Destination) <= Army->PursuitRadius + 200.f,
						TEXT("Members do not follow a target beyond Attack boundary")))
					return true;
			{
				const uint32 Serial = Army->OrderSerial;
				const FVector MoveGoal = Army->GetHomeLocation() + FVector(0.f, -600.f, 0.f);
				FCommandService::IssueOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EArmyOrder::Move, MoveGoal);
				if (!Check(Army->Order == EArmyOrder::Move && Army->OrderSerial > Serial && !Army->AttackTarget,
						TEXT("Move replaces Attack and clears the explicit target")))
					return true;
				for (AArmyUnit* Unit : Army->GetUnits())
					if (!Check(!Unit->Target, TEXT("Move clears each unit's stale attack target immediately")))
						return true;
				// Leave a live defender within siege range but off the Move route.
				if (!Check(Enemy->GetUnits().Num() >= 3, TEXT("Move probe has a live defender")))
					return true;
				Enemy->GetUnits()[2]->SetActorLocation(Army->GetHomeLocation() + FVector(950.f, 450.f, 0.f),
					false, nullptr, ETeleportType::TeleportPhysics);
				MoveStart = Army->GetCenter();
				SetStage(2, Now);
			}
			break;
		case 2:
			if (Now - StageStarted < 2.)
				break;
			if (!Check(FVector::Dist2D(Army->GetCenter(), Army->Destination) + 70.f < FVector::Dist2D(MoveStart, Army->Destination),
					TEXT("Move follows its destination rather than pursuing the encounter")))
				return true;
			for (AArmyUnit* Unit : Army->GetUnits())
				if (!Check(FVector::Dist2D(Unit->GetActorLocation(), Army->Destination) < 750.f,
						TEXT("Every Move member stays with its ordered route instead of chasing a nearby enemy")))
					return true;
			FCommandService::IssueOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EArmyOrder::Hold, Army->GetCenter());
			if (!Check(Army->Order == EArmyOrder::Hold, TEXT("Hold replaces Move during the encounter")))
				return true;
			HeldPositions.Reset();
			for (AArmyUnit* Unit : Army->GetUnits())
				HeldPositions.Add(Unit->GetActorLocation());
			SetStage(3, Now);
			break;
		case 3:
			if (Now - StageStarted < 1.)
				break;
			if (!Check(Army->GetUnits().Num() == HeldPositions.Num(), TEXT("Hold retains the living formation")))
				return true;
			for (int32 Index = 0; Index < HeldPositions.Num(); ++Index)
				if (!Check(FVector::Dist2D(Army->GetUnits()[Index]->GetActorLocation(), HeldPositions[Index]) < 6.f,
						TEXT("Hold remains position-bound while a hostile army is nearby")))
					return true;
			if (!Check(Enemy->GetUnits().Num() >= 3, TEXT("A live hostile remains for a replacement Attack")))
				return true;
			Victim = Enemy->GetUnits()[2];
			Victim->SetActorLocation(Army->GetHomeLocation() + FVector(850.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			if (!Check(FCommandService::IssueAttack(Army->GetOwningPlayerState(), Army.Get(), Army->GetHomeLocation() + FVector(400.f, 0.f, 0.f), Victim.Get()).IsAccepted(),
					TEXT("Attack can replace Hold with a live hostile target")))
				return true;
			SetStage(4, Now);
			break;
		case 4:
			if (Now - StageStarted < 1.)
				break;
			if (!Check(Army->Order == EArmyOrder::Attack, TEXT("Army is engaging before Retreat")))
				return true;
			FCommandService::IssueOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EArmyOrder::Retreat, Army->GetCenter());
			if (!Check(Army->Order == EArmyOrder::Retreat && !Army->AttackTarget,
					TEXT("Retreat replaces combat and clears the group target")))
				return true;
			for (AArmyUnit* Unit : Army->GetUnits())
				if (!Check(!Unit->Target, TEXT("Retreat clears every unit's combat target")))
					return true;
			RetreatAttacks = TotalAttacks();
			SetStage(5, Now);
			break;
		case 5:
			if (Now - StageStarted < 3.5)
				break; // Longer than even the siege attack interval.
			if (!Check(TotalAttacks() == RetreatAttacks && Army->Order == EArmyOrder::Retreat,
					TEXT("Retreat prevents all combat attacks even with an enemy in range")))
				return true;
			for (AArmyUnit* Unit : Army->GetUnits())
				if (!Check(!Unit->Target, TEXT("No retreating member reacquires a target")))
					return true;
			// Kill a live enemy via the same server damage entry point as a weapon.
			Victim = Enemy->GetUnits().Last();
			if (!Check(Victim.IsValid() && Victim->IsAlive(), TEXT("A live opposing unit is available for death check")))
				return true;
			Victim->SetActorLocation(Army->GetHomeLocation() + FVector(850.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			if (!Check(FCommandService::IssueAttack(Army->GetOwningPlayerState(), Army.Get(), Army->GetHomeLocation() + FVector(400.f, 0.f, 0.f), Victim.Get()).IsAccepted()
						&& Army->AttackTarget == Victim.Get(),
					TEXT("Targeted Attack records a live enemy")))
				return true;
			{
				AArmyUnit* Shooter = Army->GetUnits()[0];
				Victim->ReceiveAttack(Victim->GetHealth(), Shooter);
				if (!Check(!Victim->IsAlive() && Victim->GetHealth() == 0 && !Enemy->GetUnits().Contains(Victim.Get()),
						TEXT("Server lethal damage removes the dead unit from its group")))
					return true;
			}
			SetStage(6, Now);
			break;
		case 6:
			if (Now - StageStarted < .6)
				break;
			if (!Check(!Army->AttackTarget, TEXT("Dead explicit target is invalidated on the next acquisition")))
				return true;
			for (AArmyUnit* Unit : Army->GetUnits())
				if (!Check(Unit->Target != Victim.Get(), TEXT("No unit retains a dead target")))
					return true;
			FCommandService::IssueOrder(Army->GetOwningPlayerState(), Army.Get(), EArmyOrder::Hold, Army->GetCenter());
			if (!RejectUnregisteredTargets())
				return true;
			Test->AddInfo(TEXT("Live combat passed: role-specific effective range, server damage/death, target invalidation, bounded Attack, Move and Hold boundaries, Retreat override and stale-intent replacement."));
			return true;
		default:
			break;
		}
		return false;
	}

private:
	bool Check(bool Condition, const TCHAR* Message)
	{
		if (!Condition)
			Test->AddError(Message);
		return Condition;
	}

	void SetStage(int32 Next, double Now)
	{
		Stage = Next;
		StageStarted = Now;
	}

	uint32 TotalAttacks() const
	{
		uint32 Count = 0;
		for (const AArmyUnit* Unit : Army->GetUnits())
			Count += Unit->AttackCount;
		return Count;
	}
	bool RejectUnregisteredTargets()
	{
		ACommandGameState* State = Army->GetWorld()->GetGameState<ACommandGameState>();
		const uint32 Serial = Army->OrderSerial;
		const FVector Destination = Army->Destination;
		const auto Rejected = [&](AActor* Target) {
			const FCommandResult Result = FCommandService::IssueAttack(Army->GetOwningPlayerState(), Army.Get(), Target->GetActorLocation(), Target);
			return !Result.IsAccepted() && Army->OrderSerial == Serial && Army->Order == EArmyOrder::Hold
				&& Army->Destination == Destination && !Army->AttackTarget;
		};
		AArmyUnit* Detached = nullptr;
		for (AArmyUnit* Unit : Enemy->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive())
			{
				Detached = Unit;
				break;
			}
		if (!Check(Detached != nullptr, TEXT("A living hostile is available for membership validation")))
			return false;
		Enemy->OnMemberDied(Detached); // Membership fixture: health and the back-pointer remain live.
		const bool bUnitRejected = Rejected(Detached);
		Detached->Destroy();
		if (!Check(bUnitRejected, TEXT("A live unit absent from its group rejects Attack without changing Hold")))
			return false;
		AHeadquarters* HQ = State->EnemyHeadquarters;
		State->EnemyHeadquarters = nullptr;
		const bool bHQRejected = Rejected(HQ);
		State->EnemyHeadquarters = HQ;
		if (!Check(bHQRejected, TEXT("An unregistered live hostile HQ rejects Attack without changing Hold")))
			return false;
		const FTransform Transform(State->EnemyHeadquarters->GetActorLocation() + FVector(900.f, 0.f, 65.f));
		ACommandBuilding* Building = Army->GetWorld()->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(),
			Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Check(Building != nullptr, TEXT("A hostile building registration fixture spawns")))
			return false;
		Building->BuildingIndex = ArmyTestSetup::WorkshopIndex;
		Building->TeamIndex = 5;
		Building->OwningPlayerState = State->EnemyCommander;
		Building->FinishSpawning(Transform);
		State->Buildings.Remove(Building);
		const bool bBuildingRejected = Building->IsAlive() && Rejected(Building);
		Building->Destroy();
		return Check(bBuildingRejected, TEXT("An unregistered live hostile building rejects Attack without changing Hold"));
	}

	bool Begin(double Now)
	{
		if (Now - Started < 3.)
			return false; // Dynamic navmesh and AI controllers initialize asynchronously.
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetNetMode() == NM_Standalone)
			{
				World = Context.World();
				break;
			}
		if (!World)
			return false;
		const ACommandGameState* State = World->GetGameState<ACommandGameState>();
		Controller = ArmyTestSetup::Controller(World);
		if (!ArmyTestSetup::MapReady(State) || !Controller.IsValid()
			|| !Controller->GetPlayerState<ACommandPlayerState>()
			|| Controller->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0)
			return false;
		// Stop paid reinforcements and remove pre-existing hostiles before the
		// fixture spawns; Hold alone would still let nearby members fire.
		for (TActorIterator<ACommandBuilding> It(World); It; ++It)
			if (It->TeamIndex == 5 && It->IsProducer())
				FCommandService::ConfigureProduction(State->EnemyCommander, *It,
					It->bForceConfigured ? It->ProductionRole : static_cast<EUnitRole>(255), false);
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			if (It->GetTeamIndex() == 5)
				It->Destroy();
		// CombatActors treats any produced hostile force as present. Own both
		// fixtures explicitly so a partially assembled paid force cannot replace one.
		Army = ArmyTestSetup::SpawnGroup(World, Controller.Get(), 0,
			ArmyTestSetup::FromFriendlyHQ(State, 1700.f, 600.f, 100.f));
		Enemy = ArmyTestSetup::SpawnGroup(World, nullptr, -1, ArmyTestSetup::HostileStaging(State));
		if (!Check(Army.IsValid() && Enemy.IsValid()
					&& Army->GetUnits().Num() == 6 && Enemy->GetUnits().Num() == 6
					&& Army->GetTeamIndex() != Enemy->GetTeamIndex(),
				TEXT("Explicit combat fixtures have two opposed six-unit armies")))
			return true;
		AArmyUnit* Front = Army->GetUnits()[0];
		AArmyUnit* Ranged = Army->GetUnits()[2];
		AArmyUnit* Siege = Army->GetUnits()[4];
		if (!Check(Front->GetUnitRole() == EUnitRole::Frontline && Ranged->GetUnitRole() == EUnitRole::Ranged && Siege->GetUnitRole() == EUnitRole::Siege
					&& Front->WeaponRange() < Ranged->WeaponRange() && Ranged->WeaponRange() < Siege->WeaponRange(),
				TEXT("Frontline, ranged, siege spawn with strictly increasing weapon ranges")))
			return true;
		Victim = Enemy->GetUnits()[0];
		const FVector OriginalPosition = Victim->GetActorLocation();
		// FireAt is the authoritative weapon path. Probe just outside and inside each
		// role's effective range on a living hostile; one update prevents AI ticks
		// or cooldowns from contaminating the boundary observations.
		for (AArmyUnit* Shooter : { Front, Ranged, Siege })
		{
			const int32 Health = Victim->GetHealth();
			const uint32 Shots = Shooter->AttackCount;
			Victim->SetActorLocation(Shooter->GetActorLocation() + FVector(Shooter->WeaponRange() + 100.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			Shooter->FireAt(Victim.Get());
			if (!Check(Victim->GetHealth() == Health && Shooter->AttackCount == Shots,
					TEXT("Weapon cannot hit beyond its own role range")))
				return true;
			Victim->SetActorLocation(Shooter->GetActorLocation() + FVector(Shooter->WeaponRange() - 75.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			FVector SplashPositions[3];
			int32 SplashHealth[3];
			const FVector FriendlyPosition = Front->GetActorLocation();
			const int32 FriendlyHealth = Front->GetHealth();
			if (Shooter == Siege)
			{
				for (int32 Index = 0; Index < 3; ++Index)
				{
					AArmyUnit* Neighbour = Enemy->GetUnits()[Index + 2];
					SplashPositions[Index] = Neighbour->GetActorLocation();
					SplashHealth[Index] = Neighbour->GetHealth();
					const float Distance = Index == 0 ? 100.f : Index == 1 ? 200.f
																		   : 201.f;
					Neighbour->SetActorLocation(Victim->GetActorLocation() + FVector(0.f, Distance, 0.f),
						false, nullptr, ETeleportType::TeleportPhysics);
				}
				Front->SetActorLocation(Victim->GetActorLocation() + FVector(0.f, -100.f, 0.f),
					false, nullptr, ETeleportType::TeleportPhysics);
			}
			Shooter->FireAt(Victim.Get());
			const int32 ExpectedDamage = Shooter == Ranged
				? Shooter->GetDefinition()->AttackDamage * 3 / 2
				: Shooter->GetDefinition()->AttackDamage;
			if (!Check(Victim->GetHealth() == Health - ExpectedDamage && Shooter->AttackCount == Shots + 1,
					TEXT("Heavy target takes Piercing bonus and neutral Kinetic/Demolition damage within range")))
				return true;
			if (Shooter == Siege)
			{
				const int32 SplashExpected[] = {
					Shooter->GetDefinition()->AttackDamage * 3 / 4,
					Shooter->GetDefinition()->AttackDamage / 2,
					0
				};
				for (int32 Index = 0; Index < 3; ++Index)
				{
					AArmyUnit* Neighbour = Enemy->GetUnits()[Index + 2];
					if (!Check(Neighbour->GetHealth() == SplashHealth[Index] - SplashExpected[Index],
							TEXT("Artillery hits hostile neighbours with falloff, includes the edge and excludes outside")))
						return true;
					Neighbour->SetActorLocation(SplashPositions[Index], false, nullptr, ETeleportType::TeleportPhysics);
				}
				if (!Check(Front->GetHealth() == FriendlyHealth, TEXT("Artillery splash never damages an ally inside the blast")))
					return true;
				Front->SetActorLocation(FriendlyPosition, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
		Victim->SetActorLocation(OriginalPosition, false, nullptr, ETeleportType::TeleportPhysics);
		FCommandService::IssueOrder(Army->GetOwningPlayerState(), Army.Get(), EArmyOrder::Hold, Army->GetCenter()); // Reset target acquired by the direct range probes.
		if (!CheckCounterAcquisition(State))
			return true;
		Victim = Enemy->GetUnits()[1]; // Fresh defender: range probes do not pre-damage the encounter target.
		// Bring one frontline defender into the friendly HQ-derived approach,
		// leaving the other defenders at their enemy HQ-derived fixture spawn.
		Victim->SetActorLocation(Army->GetHomeLocation() + FVector(850.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		SetStage(7, Now); // A rejected request while dynamic navigation starts is safe to retry.
		return false;
	}

	bool CheckCounterAcquisition(const ACommandGameState* State)
	{
		// Run real acquisition without advancing cooldowns or hurting these fixtures.
		// Positions come from the map's HQ, and are restored before the encounter.
		FVector FriendlyPositions[6];
		FVector HostilePositions[6];
		for (int32 Index = 0; Index < 6; ++Index)
		{
			FriendlyPositions[Index] = Army->GetUnits()[Index]->GetActorLocation();
			HostilePositions[Index] = Enemy->GetUnits()[Index]->GetActorLocation();
			Army->GetUnits()[Index]->NextAttackTime = TNumericLimits<float>::Max();
		}
		AHeadquarters* HQ = State->EnemyHeadquarters;
		const FVector Anchor = HQ->GetActorLocation() + FVector(900.f, 0.f, 0.f);
		for (AArmyUnit* Unit : Army->GetUnits())
			Unit->SetActorLocation(Anchor, false, nullptr, ETeleportType::TeleportPhysics);
		FCommandService::IssueOrder(Army->GetOwningPlayerState(), Army.Get(), EArmyOrder::Hold, Army->GetCenter());
		Enemy->GetUnits()[0]->SetActorLocation(Anchor + FVector(50.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		Enemy->GetUnits()[2]->SetActorLocation(Anchor + FVector(100.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		Enemy->GetUnits()[4]->SetActorLocation(Anchor + FVector(150.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		static_cast<AActor*>(Army.Get())->Tick(.25f);
		bool bOk = Check(Army->GetUnits()[0]->Target == Enemy->GetUnits()[2]
				&& Army->GetUnits()[2]->Target == Enemy->GetUnits()[0]
				&& Army->GetUnits()[4]->Target
				&& CombatTarget::ArmorClass(Army->GetUnits()[4]->Target) == EArmorClass::Structure,
			TEXT("Live acquisition prefers nearest Light for Kinetic, Heavy for Piercing, and Structure for Demolition"));
		Enemy->GetUnits()[4]->SetActorLocation(Anchor + FVector(75.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		static_cast<AActor*>(Army.Get())->Tick(.25f);
		bOk &= Check(Army->GetUnits()[0]->Target == Enemy->GetUnits()[2],
			TEXT("A live in-range target stays selected when a nearer matching-class enemy appears"));
		// Explicit player intent overrides a retained automatic counter target.
		Army->AttackTarget = Enemy->GetUnits()[0];
		static_cast<AActor*>(Army.Get())->Tick(.25f);
		bOk &= Check(Army->GetUnits()[0]->Target == Enemy->GetUnits()[0],
			TEXT("Explicit AttackTarget outranks a retained target and automatic counter preference"));
		Army->AttackTarget = nullptr;
		static_cast<AActor*>(Army.Get())->Tick(.25f);
		bOk &= Check(Army->GetUnits()[0]->Target == Enemy->GetUnits()[0],
			TEXT("An eligible non-counter target is retained even when a counter target is available"));
		Enemy->GetUnits()[0]->SetActorLocation(Anchor + FVector(400.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		static_cast<AActor*>(Army.Get())->Tick(.25f);
		bOk &= Check(Army->GetUnits()[0]->Target == Enemy->GetUnits()[4],
			TEXT("A target leaving weapon range triggers fresh nearest-counter acquisition"));
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Army->GetUnits()[Index]->SetActorLocation(FriendlyPositions[Index], false, nullptr, ETeleportType::TeleportPhysics);
			Enemy->GetUnits()[Index]->SetActorLocation(HostilePositions[Index], false, nullptr, ETeleportType::TeleportPhysics);
			Army->GetUnits()[Index]->NextAttackTime = 0.f;
		}
		FCommandService::IssueOrder(Army->GetOwningPlayerState(), Army.Get(), EArmyOrder::Hold, Army->GetCenter());
		return bOk;
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
