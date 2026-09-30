#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "EnemyCommander.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Headquarters.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDoctrineSiegeTest, "CoopRTS.Doctrine.SiegeOptics",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDoctrineRepairsTest, "CoopRTS.Doctrine.FieldRepairs",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDoctrineFrontlineTest, "CoopRTS.Doctrine.EntrenchedFrontline",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDoctrineRestartTest, "CoopRTS.Doctrine.Restart",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
UWorld* StandaloneWorld()
{
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (UWorld* World = Context.World())
			if (World->IsGameWorld() && World->GetNetMode() == NM_Standalone) return World;
	return nullptr;
}

// Each test has its own Boot process. Do not replace the production attack, damage,
// movement, ownership or restart paths with mocks; only position the encounter.
struct FDoctrineActors
{
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<ACommandPlayerState> Wallet;
	TWeakObjectPtr<ACommandGameState> State;
	TWeakObjectPtr<AArmyGroup> Armies[2];
	TWeakObjectPtr<AArmyGroup> Enemy;

	bool Find(UWorld* InWorld)
	{
		World = InWorld;
		for (TActorIterator<ACommandPlayerController> It(InWorld); It; ++It)
			if (It->IsLocalController()) { Controller = *It; break; }
		if (!Controller.IsValid()) return false;
		if (!ArmyTestSetup::CombatActors(InWorld)) return false;
		for (TActorIterator<AArmyGroup> It(InWorld); It; ++It)
		{
			if (It->bOpposingArmy) Enemy = *It;
			else if (It->GetOwner() == Controller.Get() && It->ArmyIndex >= 0 && It->ArmyIndex < 2)
				Armies[It->ArmyIndex] = *It;
		}
		State = InWorld->GetGameState<ACommandGameState>();
		Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		return State.IsValid() && Wallet.IsValid() && Enemy.IsValid()
			&& Armies[0].IsValid() && Armies[1].IsValid()
			&& Armies[0]->Units.Num() == 6 && Armies[1]->Units.Num() == 6 && Enemy->Units.Num() == 6;
	}

	void IsolatePlanner() const
	{
		for (TActorIterator<AEnemyCommander> It(World.Get()); It; ++It) It->Destroy();
		Enemy->IssueHold();
	}
};

class FDoctrineScenario : public IAutomationLatentCommand
{
public:
	explicit FDoctrineScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		UWorld* World = StandaloneWorld();
		if (!World) return false;
		if (!bReady)
		{
			if (Now - Started < 3. || !Actors.Find(World)) return false;
			if (!Check(Actors.State->MatchResult == EMatchResult::Ongoing
				&& Actors.Wallet->Doctrine == EArmyDoctrine::None,
				TEXT("Fresh authoritative Boot begins ongoing with no selected doctrine"))) return true;
			if (!RestartCase()) Actors.IsolatePlanner();
			bReady = true;
			StageStarted = Now;
		}
		return Step(Now);
	}

protected:
	virtual bool Step(double Now) = 0;
	virtual bool RestartCase() const { return false; }
	bool Check(bool Condition, const TCHAR* Message) const
	{
		if (!Condition) Test->AddError(Message);
		return Condition;
	}
	void Next(int32 NewStage, double Now) { Stage = NewStage; StageStarted = Now; }
	bool After(double Now, double Seconds) const { return Now - StageStarted >= Seconds; }

	// An actual hostile weapon shot, not a stat-getter or synthetic health subtraction.
	int32 WeaponHit(AArmyUnit* Shooter, AArmyUnit* Target) const
	{
		const FVector Old = Shooter->GetActorLocation();
		Shooter->SetActorLocation(Target->GetActorLocation() + FVector(90.f, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		Shooter->NextAttackTime = 0.f;
		const int32 Before = Target->Health;
		Shooter->FireAt(Target);
		Shooter->SetActorLocation(Old, false, nullptr, ETeleportType::TeleportPhysics);
		return Before - Target->Health;
	}

	FAutomationTestBase* Test;
	FDoctrineActors Actors;
	const double Started;
	double StageStarted = 0.;
	int32 Stage = 0;
	bool bReady = false;
};

class FSiegeScenario final : public FDoctrineScenario
{
public:
	using FDoctrineScenario::FDoctrineScenario;
private:
	bool Step(double Now) override
	{
		AArmyUnit* Siege = Actors.Armies[0]->Units[4];
		AArmyUnit* OtherSiege = Actors.Armies[1]->Units[4];
		AArmyUnit* EnemySiege = Actors.Enemy->Units[4];
		AArmyUnit* Victim = Actors.Enemy->Units[0];
		const float BaseRange = Siege->Definition->Range;
		if (!Check(Siege->Definition == OtherSiege->Definition
			&& Siege->Definition == EnemySiege->Definition,
			TEXT("All siege roles share the unmodified base asset"))) return true;
		const FVector Original = Victim->GetActorLocation();
		const int32 WalletBefore = Actors.Wallet->Resources;
		const float ProbeRange = BaseRange * 1.125f;
		Victim->SetActorLocation(Siege->GetActorLocation() + FVector(ProbeRange, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		const int32 Outside = Victim->Health;
		const uint32 BeforeShots = Siege->AttackCount;
		Siege->NextAttackTime = 0.f;
		Siege->FireAt(Victim);
		if (!Check(Victim->Health == Outside && Siege->AttackCount == BeforeShots,
			TEXT("Unchosen siege cannot hit at the future optics-only range"))) return true;
		Victim->SetActorLocation(Original, false, nullptr, ETeleportType::TeleportPhysics);
		const int32 BaseDamage = WeaponHit(Siege, Victim);
		if (!Check(BaseDamage > 0, TEXT("Unchosen siege inflicts actual weapon damage"))) return true;
		Victim->Health = Victim->MaxHealth(); // Keep the same live target for comparable shots.
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::None);
		ArmyTestSetup::Research(Actors.Controller.Get(), static_cast<EArmyDoctrine>(255));
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::None && Actors.Wallet->Resources == WalletBefore,
			TEXT("None and invalid enum requests reject without spending or selecting"))) return true;
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::SiegeOptics);
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics && Actors.Wallet->Resources == WalletBefore - ACommandBuilding::ResearchCost,
			TEXT("Owned workshop purchase selects SiegeOptics and pays once"))) return true;
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::FieldRepairs);
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics
			&& Actors.Wallet->Resources == WalletBefore - ACommandBuilding::ResearchCost,
			TEXT("A purchased specialization cannot be replaced or charged twice"))) return true;
		if (!Check(FMath::IsNearlyEqual(Siege->WeaponRange(), BaseRange * 1.25f)
			&& FMath::IsNearlyEqual(OtherSiege->WeaponRange(), BaseRange * 1.25f)
			&& FMath::IsNearlyEqual(EnemySiege->WeaponRange(), BaseRange)
			&& FMath::IsNearlyEqual(Siege->Definition->Range, BaseRange),
			TEXT("Both owned armies inherit optics; hostile siege and shared asset remain unchanged"))) return true;
		// A second controller has its own PlayerState and an actual six-unit group.
		// Sharing team and DataAsset must not share this player's doctrine.
		ACommandPlayerController* Teammate = Actors.World->SpawnActor<ACommandPlayerController>();
		ACommandPlayerState* TeammateWallet = Actors.World->SpawnActor<ACommandPlayerState>();
		if (!Check(Teammate && TeammateWallet, TEXT("Second player controller and wallet spawn"))) return true;
		Teammate->SetPlayerState(TeammateWallet);
		Actors.State->AddPlayerState(TeammateWallet);
		TeammateWallet->CommanderIndex = 1;
		const FTransform AllyTransform(FRotator::ZeroRotator, FVector(-1800.f, 850.f, 100.f));
		AArmyGroup* Ally = Actors.World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(),
			AllyTransform, Teammate, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Check(Ally, TEXT("Second player's owned group spawns"))) return true;
		Ally->HomeLocation = AllyTransform.GetLocation();
		Ally->TeamIndex = 0;
		Ally->OwningPlayerState = TeammateWallet;
		Ally->ArmyIndex = 0;
		Ally->FinishSpawning(AllyTransform);
		if (!Check(Ally->SpawnUnits() && Ally->Units.Num() == 6 && TeammateWallet->Doctrine == EArmyDoctrine::None
			&& FMath::IsNearlyEqual(Ally->Units[4]->WeaponRange(), BaseRange),
			TEXT("Other player's same-team siege does not inherit optics"))) return true;
		const int32 TeammateBalance = TeammateWallet->Resources;
		ArmyTestSetup::Research(Teammate, EArmyDoctrine::FieldRepairs);
		if (!Check(TeammateWallet->Doctrine == EArmyDoctrine::FieldRepairs
			&& Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics
			&& TeammateWallet->Resources == TeammateBalance - ACommandBuilding::ResearchCost
			&& Actors.Wallet->Resources == WalletBefore - ACommandBuilding::ResearchCost
			&& FMath::IsNearlyEqual(Ally->Units[4]->WeaponRange(), BaseRange)
			&& FMath::IsNearlyEqual(Siege->WeaponRange(), BaseRange * 1.25f),
			TEXT("Another player's choice and wallet remain independent on a shared team"))) return true;
		Victim->SetActorLocation(Ally->Units[4]->GetActorLocation() + FVector(ProbeRange, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		const uint32 AllyShots = Ally->Units[4]->AttackCount;
		Ally->Units[4]->NextAttackTime = 0.f;
		Ally->Units[4]->FireAt(Victim);
		if (!Check(Ally->Units[4]->AttackCount == AllyShots,
			TEXT("Unchosen teammate cannot land an optics-only siege hit"))) return true;
		Victim->SetActorLocation(Original, false, nullptr, ETeleportType::TeleportPhysics);
		Victim->SetActorLocation(Siege->GetActorLocation() + FVector(ProbeRange, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		Siege->NextAttackTime = 0.f;
		const uint32 Shots = Siege->AttackCount;
		const int32 Before = Victim->Health;
		Siege->FireAt(Victim);
		if (!Check(Siege->AttackCount == Shots + 1 && Victim->Health < Before,
			TEXT("Optics siege lands a real hit beyond its former range"))) return true;
		Victim->SetActorLocation(Original, false, nullptr, ETeleportType::TeleportPhysics);
		Victim->Health = Victim->MaxHealth();
		const int32 OpticsDamage = WeaponHit(Siege, Victim);
		if (!Check(OpticsDamage > 0 && OpticsDamage < BaseDamage,
			TEXT("Optics trades actual outgoing siege weapon damage for range"))) return true;
		Victim->Health = Victim->MaxHealth();
		if (!Check(WeaponHit(OtherSiege, Victim) == OpticsDamage,
			TEXT("Second owned army applies the same outgoing damage tradeoff"))) return true;
		AHeadquarters* HQ = Actors.State->EnemyHeadquarters;
		if (!Check(IsValid(HQ) && HQ->IsAlive(), TEXT("Live hostile HQ exists for optics hit"))) return true;
		const FVector SiegeHome = Siege->GetActorLocation();
		Siege->SetActorLocation(HQ->GetActorLocation() + FVector(ProbeRange, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		Siege->NextAttackTime = 0.f;
		const int32 HQBefore = HQ->Health;
		Siege->NextAttackTime = 0.f;
		Siege->FireAt(HQ);
		Siege->SetActorLocation(SiegeHome, false, nullptr, ETeleportType::TeleportPhysics);
		if (!Check(HQ->Health == HQBefore - OpticsDamage && HQ->IsAlive(),
			TEXT("Optics siege range and reduced damage also hit a real hostile HQ"))) return true;
		AArmyGroup* Later = ArmyTestSetup::SpawnGroup(Actors.World.Get(), Actors.Controller.Get(), 2, FVector(-3000.f, 1700.f, 100.f));
		AArmyUnit* Replacement = Later ? Later->Units[4].Get() : nullptr;
		if (!Check(Replacement && Replacement->IsAlive()
			&& FMath::IsNearlyEqual(Replacement->WeaponRange(), BaseRange * 1.25f),
			TEXT("Units created after research inherit the owner effect without changing the asset"))) return true;
		Victim->Health = Victim->MaxHealth();
		if (!Check(WeaponHit(Replacement, Victim) == OpticsDamage,
			TEXT("Replacement siege fires with the same reduced real weapon damage"))) return true;
		Test->AddInfo(TEXT("Optics: real extended unit/HQ hits, outgoing damage tradeoff, existing and later units, paid irreversible research, owner isolation."));
		return true;
	}
};

class FRepairsScenario final : public FDoctrineScenario
{
public:
	using FDoctrineScenario::FDoctrineScenario;
private:
	bool Step(double Now) override
	{
		AArmyUnit* Patient = Actors.Armies[0]->Units[0];
		AArmyUnit* Other = Actors.Armies[1]->Units[0];
		AArmyUnit* Attacker = Actors.Enemy->Units[0];
		if (Stage == 0)
		{
			ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::FieldRepairs);
			if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::FieldRepairs,
				TEXT("FieldRepairs choice accepted for the owning player"))) return true;
			Patient->Health = Patient->MaxHealth() - 55;
			Other->Health = Other->MaxHealth() - 55;
			// Damage is an interrupt and the unselected second owned army also inherits healing.
			if (!Check(WeaponHit(Attacker, Patient) > 0 && WeaponHit(Attacker, Other) > 0,
				TEXT("Actual hostile weapon damages both owned army members"))) return true;
			Expected = Patient->Health;
			OtherExpected = Other->Health;
			Next(1, Now);
			return false;
		}
		if (Stage == 1)
		{
			if (!After(Now, 4.7)) return false;
			if (!Check(Patient->Health == Expected && Other->Health == OtherExpected,
				TEXT("Both stationary armies wait five uninterrupted seconds before healing"))) return true;
			Next(2, Now);
			return false;
		}
		if (Stage == 2)
		{
			if (!After(Now, 1.2)) return false;
			if (!Check(Patient->Health > Expected && Other->Health > OtherExpected
				&& Patient->Health <= Patient->MaxHealth() && Other->Health <= Other->MaxHealth(),
				TEXT("Both owned armies gain real bounded health after the stationary delay"))) return true;
			Expected = Patient->Health;
			if (!Check(WeaponHit(Attacker, Patient) > 0,
				TEXT("Hostile weapon interrupts ongoing repairs"))) return true;
			Expected = Patient->Health;
			Next(3, Now);
			return false;
		}
		if (Stage == 3)
		{
			if (!After(Now, 3.0)) return false;
			if (!Check(Patient->Health == Expected,
				TEXT("Taking damage resets repairs and cannot spend banked healing"))) return true;
			// Firing a real frontline weapon is a separate interrupt.
			const int32 EnemyBefore = Attacker->Health;
			if (!Check(WeaponHit(Patient, Attacker) > 0 && Attacker->Health < EnemyBefore,
				TEXT("Repairing frontline fires an actual hostile-damaging shot"))) return true;
			Next(4, Now);
			return false;
		}
		if (Stage == 4)
		{
			if (!After(Now, 4.7)) return false;
			if (!Check(Patient->Health == Expected,
				TEXT("Firing also restarts the full five-second quiet interval"))) return true;
			Next(5, Now);
			return false;
		}
		if (Stage == 5)
		{
			if (!After(Now, 1.3)) return false;
			if (!Check(Patient->Health > Expected, TEXT("Repairs resume after five seconds without fire"))) return true;
			Patient->Health = Patient->MaxHealth() - 20;
			Expected = Patient->Health;
			StartPosition = Patient->GetActorLocation();
			Actors.Controller->ServerIssueOrder(Actors.Armies[0].Get(), EArmyOrder::Move,
				Actors.Armies[0]->HomeLocation + FVector(0.f, -1700.f, 0.f));
			if (!Check(Actors.Armies[0]->Order == EArmyOrder::Move,
				TEXT("Owned Move begins an actual navigation interruption"))) return true;
			Next(6, Now);
			return false;
		}
		if (Stage == 6)
		{
			if (!After(Now, 2.)) return false;
			if (!Check(FVector::Dist2D(Patient->GetActorLocation(), StartPosition) > 100.f
				&& Patient->GetVelocity().SizeSquared2D() > FMath::Square(1.f)
				&& Patient->Health == Expected,
				TEXT("Navigating member remains in motion without healing before Hold"))) return true;
			Actors.Controller->ServerIssueOrder(Actors.Armies[0].Get(), EArmyOrder::Hold, FVector::ZeroVector);
			if (!Check(Actors.Armies[0]->Order == EArmyOrder::Hold,
				TEXT("Hold stops the real movement before repairs resume"))) return true;
			Next(7, Now);
			return false;
		}
		if (Stage == 7)
		{
			if (!After(Now, 4.7)) return false;
			if (!Check(Patient->Health == Expected,
				TEXT("Movement interruption cannot bank healing across the subsequent Hold"))) return true;
			Next(8, Now);
			return false;
		}
		if (Stage == 8)
		{
			if (!After(Now, 1.4)) return false;
			if (!Check(Patient->Health > Expected && Patient->Health <= Patient->MaxHealth(),
				TEXT("Stationary survivor recovers after the new complete delay"))) return true;
			Patient->Health = Patient->MaxHealth() - 1;
			WeaponHit(Attacker, Patient);
			Next(9, Now);
			return false;
		}
		if (Stage == 9)
		{
			if (!After(Now, 8.)) return false;
			if (!Check(Patient->Health == Patient->MaxHealth()
				&& Other->Health == Other->MaxHealth(),
				TEXT("Continuous repairs clamp both living units to max health, never overflow"))) return true;
			Patient->ReceiveAttack(Patient->Health, Attacker);
			if (!Check(!Patient->IsAlive() && !Actors.Armies[0]->Units.Contains(Patient),
				TEXT("Lethal damage removes a patient; FieldRepairs cannot resurrect it"))) return true;
			Other->Health = Other->MaxHealth() - 20;
			AHeadquarters* HQ = Actors.State->EnemyHeadquarters;
			AArmyUnit* Siege = Actors.Armies[1]->Units[4];
			if (!Check(IsValid(HQ) && Siege->IsAlive(),
				TEXT("Live HQ and allied siege are available for terminal healing check"))) return true;
			HQ->Health = 1;
			Siege->SetActorLocation(HQ->GetActorLocation() + FVector(150.f, 0.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			Siege->NextAttackTime = 0.f;
			const uint32 BeforeShot = Siege->AttackCount;
			Siege->FireAt(HQ);
			if (!Check(Siege->AttackCount == BeforeShot + 1 && HQ->Health == 0,
				TEXT("Real weapon destroys HQ before terminal repair observation"))) return true;
			Next(10, Now);
			return false;
		}
		if (Stage == 10)
		{
			if (Actors.State->MatchResult == EMatchResult::Ongoing) return false;
			if (!Check(Actors.State->MatchResult == EMatchResult::Victory,
				TEXT("HQ weapon hit publishes terminal Victory"))) return true;
			OtherExpected = Other->Health;
			Next(11, Now);
			return false;
		}
		if (Stage == 11)
		{
			if (!After(Now, 6.)) return false;
			if (!Check(Other->Health == OtherExpected && OtherExpected < Other->MaxHealth(),
				TEXT("Terminal match blocks healing even after an uninterrupted six seconds"))) return true;
			Test->AddInfo(TEXT("Repairs: five-second delay, health gain in both armies, damage/fire/movement resets, no banking, max cap, no resurrection or terminal healing."));
			return true;
		}
		return true;
	}
	int32 Expected = 0;
	int32 OtherExpected = 0;
	FVector StartPosition = FVector::ZeroVector;
};

class FFrontlineScenario final : public FDoctrineScenario
{
public:
	using FDoctrineScenario::FDoctrineScenario;
private:
	bool Step(double Now) override
	{
		AArmyUnit* Held = Actors.Armies[0]->Units[0];
		AArmyUnit* Moving = Actors.Armies[1]->Units[0];
		AArmyUnit* Enemy = Actors.Enemy->Units[0];
		if (Stage == 0)
		{
			const int32 Baseline = WeaponHit(Enemy, Held);
			if (!Check(Baseline > 0, TEXT("Hostile real weapon establishes unchosen frontline damage"))) return true;
			Held->Health = Held->MaxHealth();
			Moving->Health = Moving->MaxHealth();
			ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::EntrenchedFrontline);
			if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::EntrenchedFrontline,
				TEXT("EntrenchedFrontline is the player's irreversible choice"))) return true;
			for (const TWeakObjectPtr<AArmyGroup>& Group : Actors.Armies)
				if (!Check(Group->AssignFront(EFrontOrder::Defend, Group->GetCenter()),
					TEXT("Owned armies can adopt a Defend front at their current position"))) return true;
			const int32 Protected = WeaponHit(Enemy, Held);
			if (!Check(Protected == Baseline * 3 / 4 && Actors.Armies[0]->bAutomaticFront
				&& Held->GetCharacterMovement()->Velocity.Size2D() <= 1.f,
				TEXT("Stationary Defend frontline takes exactly 25 percent less real weapon damage"))) return true;
			const int32 OtherProtected = WeaponHit(Enemy, Moving);
			if (!Check(OtherProtected == Protected && Actors.Armies[1]->bAutomaticFront,
				TEXT("Second owned Defend army gains identical protection"))) return true;
			AArmyUnit* Ranged = Actors.Armies[0]->Units[2];
			if (!Check(Ranged->UnitRole == EUnitRole::Ranged
				&& WeaponHit(Enemy, Ranged) == Baseline,
				TEXT("Stationary Defend ranged units do not inherit frontline-only mitigation"))) return true;
			Moving->Health = Moving->MaxHealth();
			Start = Moving->GetActorLocation();
			if (!Check(Actors.Armies[1]->AssignFront(EFrontOrder::Defend,
				Actors.Armies[1]->HomeLocation + FVector(0.f, 650.f, 0.f)),
				TEXT("Second army can travel to a new Defend front"))) return true;
			BaseDamage = Baseline;
			Next(1, Now);
			return false;
		}
		if (Stage == 1)
		{
			if (!After(Now, .7)) return false;
			if (!Check(FVector::Dist2D(Moving->GetActorLocation(), Start) > 30.f,
				TEXT("Frontline traveling to its Defend front actually changes position"))) return true;
			if (!Check(WeaponHit(Enemy, Moving) == BaseDamage,
				TEXT("Moving frontline takes full real weapon damage despite chosen doctrine"))) return true;
			if (!Check(Actors.Armies[1]->AssignFront(EFrontOrder::FallBack, Actors.Armies[1]->HomeLocation)
				&& Actors.Armies[1]->Order == EArmyOrder::Retreat,
				TEXT("Fall Back replaces the traveling Defend front"))) return true;
			Start = Moving->GetActorLocation();
			Next(2, Now);
			return false;
		}
		if (Stage == 2)
		{
			if (!After(Now, .7)) return false;
			if (!Check(FVector::Dist2D(Moving->GetActorLocation(), Start) > 30.f,
				TEXT("Retreating frontline really moves toward home"))) return true;
			if (!Check(WeaponHit(Enemy, Moving) == BaseDamage,
				TEXT("Fall Back cannot obtain stationary Defend protection"))) return true;
			AArmyGroup* Later = ArmyTestSetup::SpawnGroup(Actors.World.Get(), Actors.Controller.Get(), 2, FVector(-3000.f, 1700.f, 100.f));
			LaterFrontline = Later ? Later->Units[0].Get() : nullptr;
			if (!Check(LaterFrontline.IsValid(), TEXT("New frontline exists after research"))) return true;
			if (!Check(Later->AssignFront(EFrontOrder::Defend, Later->GetCenter()),
				TEXT("New squad adopts Defend after research"))) return true;
			Next(3, Now);
			return false;
		}
		if (Stage == 3)
		{
			if (!After(Now, .5)) return false;
			AArmyUnit* Replacement = LaterFrontline.Get();
			if (!Check(Replacement && WeaponHit(Enemy, Replacement) == BaseDamage * 3 / 4,
				TEXT("New stationary Defend frontline inherits protection against a real shot"))) return true;
			Replacement->Health = Replacement->MaxHealth();
			if (!Check(Replacement->Group->AssignFront(EFrontOrder::Secure, Replacement->Group->GetCenter())
				&& WeaponHit(Enemy, Replacement) == BaseDamage,
				TEXT("Stationary Secure frontline does not receive Defend mitigation"))) return true;
			Test->AddInfo(TEXT("Entrenched: stationary Defend mitigates real hits; traveling, retreating and Secure members do not; later units inherit."));
			return true;
		}
		return true;
	}
	int32 BaseDamage = 0;
	FVector Start = FVector::ZeroVector;
	TWeakObjectPtr<AArmyUnit> LaterFrontline;
};

class FRestartScenario final : public FDoctrineScenario
{
public:
	using FDoctrineScenario::FDoctrineScenario;
private:
	bool RestartCase() const override { return true; }
	bool Step(double Now) override
	{
		if (Stage == 0)
		{
			ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::SiegeOptics);
			if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics,
				TEXT("Old match owns a selected doctrine before the HQ assault"))) return true;
			AHeadquarters* HQ = Actors.State->EnemyHeadquarters;
			AArmyUnit* Siege = Actors.Armies[0]->Units[4];
			if (!Check(IsValid(HQ) && Siege->IsAlive(), TEXT("Real siege and enemy HQ are available"))) return true;
			HQ->Health = 1; // Only the weapon, never the setup, delivers the lethal hit.
			Siege->SetActorLocation(HQ->GetActorLocation() + FVector(150.f, 0.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			Siege->NextAttackTime = 0.f;
			const uint32 Before = Siege->AttackCount;
			Siege->FireAt(HQ);
			if (!Check(Siege->AttackCount == Before + 1 && HQ->Health == 0,
				TEXT("A real doctrine-bearing siege shot destroys the enemy HQ"))) return true;
			Next(1, Now);
			return false;
		}
		if (Stage == 1)
		{
			if (Actors.State->MatchResult == EMatchResult::Ongoing) return false;
			if (!Check(Actors.State->MatchResult == EMatchResult::Victory,
				TEXT("Weapon-caused HQ destruction ends the match"))) return true;
			const int32 TerminalBalance = Actors.Wallet->Resources;
			ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::EntrenchedFrontline);
			if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics && Actors.Wallet->Resources == TerminalBalance,
				TEXT("Terminal research RPC neither replaces the purchase nor charges again"))) return true;
			OldWorld = Actors.World;
			Actors.Controller->ServerRequestRestart();
			Next(2, Now);
			return false;
		}
		if (Stage == 2)
		{
			UWorld* FreshWorld = StandaloneWorld();
			if (!FreshWorld || FreshWorld == OldWorld.Get()) return false;
			ACommandPlayerController* FreshController = ArmyTestSetup::Controller(FreshWorld);
			ACommandGameState* FreshState = FreshWorld->GetGameState<ACommandGameState>();
			ACommandPlayerState* FreshWallet = FreshController ? FreshController->GetPlayerState<ACommandPlayerState>() : nullptr;
			if (!FreshState || !FreshWallet || FreshWallet->CommanderIndex < 0) return false;
			if (!Check(FreshState->MatchResult == EMatchResult::Ongoing && FreshWallet->Doctrine == EArmyDoctrine::None,
				TEXT("Fresh world clears the purchased research"))) return true;
			for (TActorIterator<AArmyGroup> It(FreshWorld); It; ++It)
				if (!Check(It->OwningPlayerState != FreshWallet, TEXT("Restart does not recreate fixed player armies"))) return true;
			ArmyTestSetup::Research(FreshController, EArmyDoctrine::FieldRepairs);
			if (!Check(FreshWallet->Doctrine == EArmyDoctrine::FieldRepairs,
				TEXT("Fresh match permits a different paid workshop specialization"))) return true;
			Test->AddInfo(TEXT("Real siege HQ kill, terminal doctrine rejection, fresh-world None reset, independent new selection."));
			return true;
		}
		return true;
	}
	TWeakObjectPtr<UWorld> OldWorld;
};
}

bool FDoctrineSiegeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSiegeScenario(this));
	return true;
}
bool FDoctrineRepairsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRepairsScenario(this));
	return true;
}
bool FDoctrineFrontlineTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFrontlineScenario(this));
	return true;
}
bool FDoctrineRestartTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRestartScenario(this));
	return true;
}

#endif
