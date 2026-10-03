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
		if (!Check(Army.IsValid() && Enemy.IsValid() && Controller.IsValid(), TEXT("Encounter groups and owner remain valid")))
			return true;
		if (Stage == 1)
		{
			if (Now - StageStarted < 1.)
				return false;
			if (!Check(TotalAttacks() > EncounterAttacks, TEXT("Region Attack acquires a nearby hostile and fires a real weapon")))
				return true;
			Victim = Enemy->GetUnits().Last();
			const int32 Count = Enemy->GetUnits().Num();
			Victim->ReceiveAttack(Victim->GetHealth(), Army->GetUnits()[0]);
			if (!Check(!Victim->IsAlive() && Victim->GetHealth() == 0 && Enemy->GetUnits().Num() == Count - 1,
					TEXT("Server lethal damage removes a dead member from its force")))
				return true;
			static_cast<AActor*>(Army.Get())->Tick(.25f);
			for (const AArmyUnit* Unit : Army->GetUnits())
				if (!Check(Unit->Target != Victim.Get(), TEXT("Acquisition never retains a dead hostile")))
					return true;
			const ACommandGameState* State = Army->GetWorld()->GetGameState<ACommandGameState>();
			const FVector RetreatStart = State->GetRegionAnchor(ArmyTestSetup::RegionAt(State, State->EnemyHeadquarters->GetActorLocation()));
			for (AArmyUnit* Unit : Army->GetUnits())
				Unit->SetActorLocation(RetreatStart + FVector(1200.f, Unit->GetCompositionSlot() * 100.f, 0.f),
					false, nullptr, ETeleportType::TeleportPhysics);
			if (!Check(FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::Retreat).IsAccepted(),
					TEXT("Retreat replaces the region attack")))
				return true;
			for (const AArmyUnit* Unit : Army->GetUnits())
				if (!Check(!Unit->Target, TEXT("Retreat immediately clears each combat target")))
					return true;
			if (!Check(Army->Verb == EForceVerb::Retreat && Army->Status == EForceStatus::Retreating,
					TEXT("Distant orphan begins active Retreat motion")))
				return true;
			RetreatCenter = Army->GetCenter();
			for (AArmyUnit* Unit : Army->GetUnits())
				Unit->NextAttackTime = 0.f;
			for (AArmyUnit* Unit : Enemy->GetUnits())
			{
				Unit->NextAttackTime = TNumericLimits<float>::Max();
				Unit->SetActorLocation(Army->GetUnits()[0]->GetActorLocation() + FVector(100.f, 0.f, 0.f),
					false, nullptr, ETeleportType::TeleportPhysics);
			}
			RetreatAttacks = TotalAttacks();
			SetStage(2, Now);
			return false;
		}
		if (Army->Verb == EForceVerb::Retreat && Army->Status == EForceStatus::Retreating)
		{
			if (!Check(TotalAttacks() == RetreatAttacks,
					TEXT("Active Retreat suppresses weapon fire while a living hostile remains in range")))
				return true;
			bObservedRetreatMotion |= FVector::Dist2D(RetreatCenter, Army->GetCenter()) > 200.f;
			// Follow the retreat with living hostiles, without extending suppression
			// into the completed MoveHold's normal defensive combat.
			for (AArmyUnit* Unit : Enemy->GetUnits())
				Unit->SetActorLocation(Army->GetUnits()[0]->GetActorLocation() + FVector(100.f, 0.f, 0.f),
					false, nullptr, ETeleportType::TeleportPhysics);
			if (Now - StageStarted < 3.5 || !bObservedRetreatMotion)
				return false;
		}
		else
		{
			if (Army->Verb == EForceVerb::MoveHold && Army->Status == EForceStatus::Marching)
				return false; // Completion can still assemble the safe-region formation.
			const ACommandGameState* State = Army->GetWorld()->GetGameState<ACommandGameState>();
			if (!Check(bObservedRetreatMotion && Army->Verb == EForceVerb::MoveHold
						&& Army->Status == EForceStatus::Holding
						&& FVector::Dist2D(Army->GetCenter(), State->GetRegionAnchor(Army->TargetRegionIndex)) < 500.f,
					TEXT("Physically moving orphan Retreat completes into MoveHold at its safe region")))
				return true;
		}
		if (!RejectUnregisteredTargets())
			return true;
		Test->AddInfo(TEXT("Live combat passed: role-specific weapon range/damage, automatic counter acquisition, region Attack, server death and Retreat fire suppression."));
		return true;
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
			const FCommandResult Result = FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::Attack, INDEX_NONE, Target);
			return !Result.IsAccepted() && Army->OrderSerial == Serial
				&& Army->Destination == Destination && !Army->TargetStructure;
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
		if (!Check(bUnitRejected, TEXT("A live unit absent from its group rejects Attack without changing accepted intent")))
			return false;
		AHeadquarters* HQ = State->EnemyHeadquarters;
		State->EnemyHeadquarters = nullptr;
		const bool bHQRejected = Rejected(HQ);
		State->EnemyHeadquarters = HQ;
		if (!Check(bHQRejected, TEXT("An unregistered live hostile HQ rejects Attack without changing accepted intent")))
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
		return Check(bBuildingRejected, TEXT("An unregistered live hostile building rejects Attack without changing accepted intent"));
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
		AHeadquarters* HQ = State->EnemyHeadquarters;
		if (!Check(IsValid(HQ) && HQ->IsAlive(), TEXT("A live hostile HQ is available for non-primary splash")))
			return true;
		const FVector SiegePosition = Siege->GetActorLocation();
		Victim->SetActorLocation(HQ->GetActorLocation() + FVector(0.f, 100.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		Siege->SetActorLocation(Victim->GetActorLocation() + FVector(600.f, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		const int32 HQHealth = HQ->Health;
		Siege->NextAttackTime = 0.f;
		Siege->FireAt(Victim.Get());
		Siege->SetActorLocation(SiegePosition, false, nullptr, ETeleportType::TeleportPhysics);
		const int32 StructureSplash = (Siege->GetDefinition()->AttackDamage * 3 / 2) * 3 / 4;
		if (!Check(HQ->Health == HQHealth - StructureSplash && HQ->IsAlive(),
				TEXT("A non-primary hostile HQ receives its Structure bonus then midpoint splash falloff")))
			return true;
		Victim->SetActorLocation(OriginalPosition, false, nullptr, ETeleportType::TeleportPhysics);
		FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::MoveHold, ArmyTestSetup::CurrentRegion(Army.Get()));
		if (!CheckCounterAcquisition(State))
			return true;
		Victim = Enemy->GetUnits()[1];
		const int32 Region = ArmyTestSetup::TravelRegion(Army.Get(), State->EnemyHeadquarters->GetActorLocation());
		if (!Check(FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::Attack, Region).IsAccepted(),
				TEXT("An owned region attack is accepted")))
			return true;
		const FVector Approach = (Army->Destination - Army->GetCenter()).GetSafeNormal2D();
		for (AArmyUnit* Unit : Enemy->GetUnits())
			Unit->SetActorLocation(Army->GetUnits()[0]->GetActorLocation() + Approach * 100.f,
				false, nullptr, ETeleportType::TeleportPhysics);
		EncounterAttacks = TotalAttacks();
		SetStage(1, Now);
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
		FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::Attack, ArmyTestSetup::RegionAt(State, Anchor));
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
		Enemy->GetUnits()[2]->SetActorLocation(Anchor + FVector(400.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		static_cast<AActor*>(Army.Get())->Tick(.25f);
		bOk &= Check(Army->GetUnits()[0]->Target == Enemy->GetUnits()[4],
			TEXT("A target leaving weapon range triggers fresh nearest-counter acquisition"));
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Army->GetUnits()[Index]->SetActorLocation(FriendlyPositions[Index], false, nullptr, ETeleportType::TeleportPhysics);
			Enemy->GetUnits()[Index]->SetActorLocation(HostilePositions[Index], false, nullptr, ETeleportType::TeleportPhysics);
			Army->GetUnits()[Index]->NextAttackTime = 0.f;
		}
		FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::MoveHold, ArmyTestSetup::CurrentRegion(Army.Get()));
		return bOk;
	}

	FAutomationTestBase* Test;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Army;
	TWeakObjectPtr<AArmyGroup> Enemy;
	TWeakObjectPtr<AArmyUnit> Victim;
	uint32 EncounterAttacks = 0;
	uint32 RetreatAttacks = 0;
	FVector RetreatCenter = FVector::ZeroVector;
	bool bObservedRetreatMotion = false;
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
