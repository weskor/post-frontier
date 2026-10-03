#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

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
			if (World->IsGameWorld() && World->GetNetMode() == NM_Standalone)
				return World;
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
			if (It->IsLocalController())
			{
				Controller = *It;
				break;
			}
		if (!Controller.IsValid())
			return false;
		State = InWorld->GetGameState<ACommandGameState>();
		Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		if (!Wallet.IsValid() || Wallet->CommanderIndex < 0 || !ArmyTestSetup::MapReady(State.Get()))
			return false;
		// Arrange combat actors locally: CombatActors also destroys the planner,
		// but Restart must exercise travel from a world with live enemy planning.
		for (TActorIterator<AArmyGroup> It(InWorld); It; ++It)
		{
			// A produced force keeps its positive number even after producer death.
			if (It->IsOpposingArmy() && It->ForceNumber == 0 && !It->GetProductionBuilding())
				Enemy = *It;
			if (It->GetOwner() == Controller.Get() && It->GetArmyIndex() >= 0 && It->GetArmyIndex() < 2)
				Armies[It->GetArmyIndex()] = *It;
		}
		for (int32 Index = 0; Index < 2; ++Index)
			if (!Armies[Index].IsValid())
				Armies[Index] = ArmyTestSetup::SpawnGroup(InWorld, Controller.Get(), Index,
					ArmyTestSetup::FromFriendlyHQ(State.Get(), 1700.f - Index * 1000.f, 600.f, 100.f));
		// Paid enemy production is homogeneous, not the mixed-role effect fixture.
		if (!Enemy.IsValid())
			Enemy = ArmyTestSetup::SpawnGroup(InWorld, nullptr, -1,
				ArmyTestSetup::HostileStaging(State.Get()));
		return State.IsValid() && Wallet.IsValid() && Enemy.IsValid()
			&& Armies[0].IsValid() && Armies[1].IsValid()
			&& Armies[0]->GetUnits().Num() == 6 && Armies[1]->GetUnits().Num() == 6 && Enemy->GetUnits().Num() == 6;
	}

	void IsolatePlanner() const
	{
		for (TActorIterator<AEnemyCommander> It(World.Get()); It; ++It)
			It->Destroy();
		for (TActorIterator<AArmyGroup> It(World.Get()); It; ++It)
			if (It->IsOpposingArmy())
				FCommandService::IssueForceOrder(It->GetOwningPlayerState(), *It, EForceVerb::MoveHold, ArmyTestSetup::CurrentRegion(*It));
		for (TActorIterator<ACommandBuilding> It(World.Get()); It; ++It)
			if (It->TeamIndex == 5 && It->IsProducer())
				FCommandService::ConfigureProduction(State->EnemyCommander, *It,
					It->bForceConfigured ? It->ProductionRole : static_cast<EUnitRole>(255), false);
	}
};

class FDoctrineScenario : public IAutomationLatentCommand
{
public:
	explicit FDoctrineScenario(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		UWorld* World = StandaloneWorld();
		if (!World)
			return false;
		// Repair timers and navigation advance in game time, not wall time.
		// Game time diverges from wall time in headless runs.
		const double Now = World->GetTimeSeconds();
		if (!bReady)
		{
			if (!bActorsArranged)
			{
				if (Now < 3. || !Actors.Find(World))
					return false;
				if (!RestartCase())
					Actors.IsolatePlanner();
				// Give each fixture its own region intent; holding then assembles at
				// that region's assigned shared post, not at the capture anchor.
				const int32 Home = ArmyTestSetup::CurrentRegion(Actors.Armies[0].Get());
				const int32 Other = ArmyTestSetup::TravelRegion(Actors.Armies[0].Get(),
					Actors.State->FriendlyHeadquarters->GetActorLocation());
				if (!Check(Home != INDEX_NONE && Other != INDEX_NONE && Home != Other
							&& FCommandService::IssueForceOrder(Actors.Wallet.Get(), Actors.Armies[0].Get(), EForceVerb::MoveHold, Home).IsAccepted()
							&& FCommandService::IssueForceOrder(Actors.Wallet.Get(), Actors.Armies[1].Get(), EForceVerb::MoveHold, Other).IsAccepted(),
						TEXT("Doctrine fixtures accept separate physical region holds")))
					return true;
				bActorsArranged = true;
			}
			if (!Check(Actors.State->MatchResult == EMatchResult::Ongoing
						&& Actors.Wallet->Doctrine == EArmyDoctrine::None,
					TEXT("Fresh authoritative Boot begins ongoing with no selected doctrine")))
				return true;
			for (const TWeakObjectPtr<AArmyGroup>& Force : Actors.Armies)
			{
				if (!Settled(Force.Get()))
				{
					SettledSince = Now;
					return false;
				}
			}
			if (!RestartCase() && !Settled(Actors.Enemy.Get()))
			{
				SettledSince = Now;
				return false;
			}
			if (Now - SettledSince < .5)
				return false;
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
		if (!Condition)
			Test->AddError(Message);
		return Condition;
	}
	void Next(int32 NewStage, double Now)
	{
		Stage = NewStage;
		StageStarted = Now;
	}
	bool After(double Now, double Seconds) const { return Now - StageStarted >= Seconds; }

	bool Settled(const AArmyGroup* Force) const
	{
		// Holding is also the spawn default; require accepted region intent,
		// controlled ground and physical assembly at the assigned shared post.
		if (!Force || Force->Orders.IsEmpty() || !Force->IsHoldingRegion()
			|| Force->HoldPostIndex == INDEX_NONE || Force->bHoldResponding
			|| Force->HoldRegionIndex != Force->TargetRegionIndex
			|| Force->WaypointRegionIndex != Force->TargetRegionIndex
			|| Actors.State->GetRegionController(Force->TargetRegionIndex) != Force->GetTeamIndex()
			|| FVector::Dist2D(Force->GetCenter(), Force->HoldPostLocation) > 170.f)
			return false;
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (Unit->GetVelocity().Size2D() > 1.f)
				return false;
		return true;
	}

	// An actual hostile weapon shot, not a stat-getter or synthetic health subtraction.
	int32 WeaponHit(AArmyUnit* Shooter, AArmyUnit* Target) const
	{
		const FVector Old = Shooter->GetActorLocation();
		Shooter->SetActorLocation(Target->GetActorLocation() + FVector(90.f, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		Shooter->NextAttackTime = 0.f;
		const int32 Before = Target->GetHealth();
		Shooter->FireAt(Target);
		Shooter->SetActorLocation(Old, false, nullptr, ETeleportType::TeleportPhysics);
		return Before - Target->GetHealth();
	}

	int32 WeaponHitFresh(AArmyUnit* Shooter, const AArmyUnit* Equivalent) const
	{
		const AArmyGroup* Source = Equivalent->GetGroup();
		// A fresh equivalent must earn Holding through physical anchor arrival;
		// spawning at another holder's post is not arrival for its new command.
		const FVector SourceCenter = Source->GetCenter();
		const FVector FixtureCenter = Source->IsHoldingRegion()
			? Actors.State->GetRegionAnchor(Source->TargetRegionIndex)
			: SourceCenter;
		AArmyGroup* Fixture = ArmyTestSetup::SpawnGroup(Actors.World.Get(),
			Cast<ACommandPlayerController>(Source->GetOwner()), Source->GetArmyIndex(), FixtureCenter);
		if (!Fixture)
			return 0;
		// Preserve the settled formation geometry while translating the whole
		// fixture to its own command's physical arrival point.
		for (AArmyUnit* Member : Fixture->GetUnits())
			for (const AArmyUnit* Original : Source->GetUnits())
				if (Member->GetCompositionSlot() == Original->GetCompositionSlot())
				{
					Member->SetActorLocation(FixtureCenter + Original->GetActorLocation() - SourceCenter,
						false, nullptr, ETeleportType::TeleportPhysics);
					break;
				}
		if (!FCommandService::IssueForceOrder(Fixture->GetOwningPlayerState(), Fixture, Source->Verb,
				Source->Verb == EForceVerb::Retreat ? INDEX_NONE : Source->TargetRegionIndex, Source->TargetStructure)
				.IsAccepted())
		{
			Fixture->Destroy();
			return 0;
		}
		Fixture->TickOrders();
		if (!Check(!Source->IsHoldingRegion() || Fixture->IsHoldingRegion(),
				TEXT("Fresh stationary damage fixture physically earns its own commanded Holding phase")))
		{
			Fixture->Destroy();
			return 0;
		}
		AArmyUnit* Target = Fixture->GetUnits()[Equivalent->GetCompositionSlot()];
		const int32 Damage = Target->GetDefinition() == Equivalent->GetDefinition() ? WeaponHit(Shooter, Target) : 0;
		Fixture->Destroy(); // EndPlay destroys every fixture member and its AI controller.
		return Damage;
	}

	FAutomationTestBase* Test;
	FDoctrineActors Actors;
	double StageStarted = 0.;
	int32 Stage = 0;
	bool bReady = false;
	bool bActorsArranged = false;
	double SettledSince = 0.;
};

class FSiegeScenario final : public FDoctrineScenario
{
public:
	using FDoctrineScenario::FDoctrineScenario;
private:
	bool Step(double Now) override
	{
		AArmyUnit* Siege = Actors.Armies[0]->GetUnits()[4];
		AArmyUnit* OtherSiege = Actors.Armies[1]->GetUnits()[4];
		AArmyUnit* EnemySiege = Actors.Enemy->GetUnits()[4];
		AArmyUnit* Victim = Actors.Enemy->GetUnits()[0];
		const float BaseRange = Siege->GetDefinition()->Range;
		if (!Check(Siege->GetDefinition() == OtherSiege->GetDefinition()
					&& Siege->GetDefinition() == EnemySiege->GetDefinition(),
				TEXT("All siege roles share the unmodified base asset")))
			return true;
		const FVector Original = Victim->GetActorLocation();
		const int32 WalletBefore = Actors.Wallet->Resources;
		const float ProbeRange = BaseRange * 1.125f;
		Victim->SetActorLocation(Siege->GetActorLocation() + FVector(ProbeRange, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		const int32 Outside = Victim->GetHealth();
		const uint32 BeforeShots = Siege->AttackCount;
		Siege->NextAttackTime = 0.f;
		Siege->FireAt(Victim);
		if (!Check(Victim->GetHealth() == Outside && Siege->AttackCount == BeforeShots,
				TEXT("Unchosen siege cannot hit at the future optics-only range")))
			return true;
		Victim->SetActorLocation(Original, false, nullptr, ETeleportType::TeleportPhysics);
		const int32 BaseDamage = WeaponHitFresh(Siege, Victim);
		if (!Check(BaseDamage > 0, TEXT("Unchosen siege inflicts actual weapon damage")))
			return true;
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::None);
		ArmyTestSetup::Research(Actors.Controller.Get(), static_cast<EArmyDoctrine>(255));
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::None && Actors.Wallet->Resources == WalletBefore,
				TEXT("None and invalid enum requests reject without spending or selecting")))
			return true;
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::SiegeOptics);
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics && Actors.Wallet->Resources == WalletBefore - ACommandBuilding::ResearchCost,
				TEXT("Owned workshop purchase selects SiegeOptics and pays once")))
			return true;
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::FieldRepairs);
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics
					&& Actors.Wallet->Resources == WalletBefore - ACommandBuilding::ResearchCost,
				TEXT("A purchased specialization cannot be replaced or charged twice")))
			return true;
		if (!Check(FMath::IsNearlyEqual(Siege->WeaponRange(), BaseRange * 1.25f)
					&& FMath::IsNearlyEqual(OtherSiege->WeaponRange(), BaseRange * 1.25f)
					&& FMath::IsNearlyEqual(EnemySiege->WeaponRange(), BaseRange)
					&& FMath::IsNearlyEqual(Siege->GetDefinition()->Range, BaseRange),
				TEXT("Both owned armies inherit optics; hostile siege and shared asset remain unchanged")))
			return true;
		// A second controller has its own PlayerState and an actual six-unit group.
		// Sharing team and DataAsset must not share this player's doctrine.
		ACommandPlayerController* Teammate = Actors.World->SpawnActor<ACommandPlayerController>();
		ACommandPlayerState* TeammateWallet = Actors.World->SpawnActor<ACommandPlayerState>();
		if (!Check(Teammate && TeammateWallet, TEXT("Second player controller and wallet spawn")))
			return true;
		Teammate->SetPlayerState(TeammateWallet);
		Actors.State->AddPlayerState(TeammateWallet);
		TeammateWallet->CommanderIndex = 1;
		const FTransform AllyTransform(FRotator::ZeroRotator,
			ArmyTestSetup::FromFriendlyHQ(Actors.State.Get(), 1700.f, -600.f, 100.f));
		AArmyGroup* Ally = Actors.World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(),
			AllyTransform, Teammate, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Check(Ally, TEXT("Second player's owned group spawns")))
			return true;
		Ally->Initialize({ 0, TeammateWallet, 0, nullptr, AllyTransform.GetLocation() });
		Ally->FinishSpawning(AllyTransform);
		if (!Check(Ally->SpawnUnits() && Ally->GetUnits().Num() == 6 && TeammateWallet->Doctrine == EArmyDoctrine::None
					&& FMath::IsNearlyEqual(Ally->GetUnits()[4]->WeaponRange(), BaseRange),
				TEXT("Other player's same-team siege does not inherit optics")))
			return true;
		const int32 TeammateBalance = TeammateWallet->Resources;
		ArmyTestSetup::Research(Teammate, EArmyDoctrine::FieldRepairs);
		if (!Check(TeammateWallet->Doctrine == EArmyDoctrine::FieldRepairs
					&& Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics
					&& TeammateWallet->Resources == TeammateBalance - ACommandBuilding::ResearchCost
					&& Actors.Wallet->Resources == WalletBefore - ACommandBuilding::ResearchCost
					&& FMath::IsNearlyEqual(Ally->GetUnits()[4]->WeaponRange(), BaseRange)
					&& FMath::IsNearlyEqual(Siege->WeaponRange(), BaseRange * 1.25f),
				TEXT("Another player's choice and wallet remain independent on a shared team")))
			return true;
		Victim->SetActorLocation(Ally->GetUnits()[4]->GetActorLocation() + FVector(ProbeRange, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		const uint32 AllyShots = Ally->GetUnits()[4]->AttackCount;
		Ally->GetUnits()[4]->NextAttackTime = 0.f;
		Ally->GetUnits()[4]->FireAt(Victim);
		if (!Check(Ally->GetUnits()[4]->AttackCount == AllyShots,
				TEXT("Unchosen teammate cannot land an optics-only siege hit")))
			return true;
		Victim->SetActorLocation(Original, false, nullptr, ETeleportType::TeleportPhysics);
		Victim->SetActorLocation(Siege->GetActorLocation() + FVector(ProbeRange, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		AArmyUnit* SplashVictim = Actors.Enemy->GetUnits()[2];
		if (!Check(IsValid(SplashVictim) && SplashVictim->IsAlive(), TEXT("A live hostile Light neighbour is available for optics splash")))
			return true;
		const FVector SplashPosition = SplashVictim->GetActorLocation();
		const int32 SplashHealth = SplashVictim->GetHealth();
		// At 144 cm, base damage 40 truncates to 25, then optics gives 18.
		// Applying optics first would instead truncate 30 * .64 to 19.
		SplashVictim->SetActorLocation(Victim->GetActorLocation() + FVector(0.f, 144.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		Siege->NextAttackTime = 0.f;
		const uint32 Shots = Siege->AttackCount;
		const int32 Before = Victim->GetHealth();
		Siege->FireAt(Victim);
		SplashVictim->SetActorLocation(SplashPosition, false, nullptr, ETeleportType::TeleportPhysics);
		const int32 SplashOpticsDamage = static_cast<int32>(BaseDamage * .64f) * 3 / 4;
		if (!Check(SplashVictim->GetHealth() == SplashHealth - SplashOpticsDamage,
				TEXT("Optics splash truncates distance falloff before applying the outgoing Workshop modifier")))
			return true;
		if (!Check(Siege->AttackCount == Shots + 1 && Victim->GetHealth() < Before,
				TEXT("Optics siege lands a real hit beyond its former range")))
			return true;
		Victim->SetActorLocation(Original, false, nullptr, ETeleportType::TeleportPhysics);
		const int32 OpticsDamage = WeaponHitFresh(Siege, Victim);
		if (!Check(OpticsDamage == BaseDamage * 3 / 4 && OpticsDamage > 0,
				TEXT("Optics trades actual outgoing siege weapon damage for range")))
			return true;
		if (!Check(WeaponHitFresh(OtherSiege, Victim) == OpticsDamage,
				TEXT("Second owned army applies the same outgoing damage tradeoff")))
			return true;
		AHeadquarters* HQ = Actors.State->EnemyHeadquarters;
		if (!Check(IsValid(HQ) && HQ->IsAlive(), TEXT("Live hostile HQ exists for optics hit")))
			return true;
		const FVector SiegeHome = Siege->GetActorLocation();
		Siege->SetActorLocation(HQ->GetActorLocation() + FVector(ProbeRange, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		Siege->NextAttackTime = 0.f;
		const int32 HQBefore = HQ->Health;
		Siege->NextAttackTime = 0.f;
		Siege->FireAt(HQ);
		Siege->SetActorLocation(SiegeHome, false, nullptr, ETeleportType::TeleportPhysics);
		const int32 StructureOpticsDamage = (Siege->GetDefinition()->AttackDamage * 3 / 2) * 3 / 4;
		if (!Check(HQ->Health == HQBefore - StructureOpticsDamage && HQ->IsAlive(),
				TEXT("Optics siege composes the Structure bonus with reduced damage at extended HQ range")))
			return true;
		AArmyGroup* Later = ArmyTestSetup::SpawnGroup(Actors.World.Get(), Actors.Controller.Get(), 2,
			ArmyTestSetup::FromFriendlyHQ(Actors.State.Get(), 1700.f, 1700.f, 100.f));
		AArmyUnit* Replacement = Later ? Later->GetUnits()[4].Get() : nullptr;
		if (!Check(Replacement && Replacement->IsAlive()
					&& FMath::IsNearlyEqual(Replacement->WeaponRange(), BaseRange * 1.25f),
				TEXT("Units created after research inherit the owner effect without changing the asset")))
			return true;
		if (!Check(WeaponHitFresh(Replacement, Victim) == OpticsDamage,
				TEXT("Replacement siege fires with the same reduced real weapon damage")))
			return true;
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
		AArmyUnit* Patient = ClampPatient.IsValid() ? ClampPatient.Get() : Actors.Armies[0]->GetUnits()[0].Get();
		AArmyUnit* Other = Actors.Armies[1]->GetUnits()[0];
		AArmyUnit* Attacker = Actors.Enemy->GetUnits()[0];
		if (Stage == 0)
		{
			ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::FieldRepairs);
			if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::FieldRepairs,
					TEXT("FieldRepairs choice accepted for the owning player")))
				return true;
			Patient->ReceiveAttack(55, Attacker);
			Other->ReceiveAttack(55, Attacker);
			// Damage is an interrupt and the unselected second owned army also inherits healing.
			if (!Check(WeaponHit(Attacker, Patient) > 0 && WeaponHit(Attacker, Other) > 0,
					TEXT("Actual hostile weapon damages both owned army members")))
				return true;
			Expected = Patient->GetHealth();
			OtherExpected = Other->GetHealth();
			Attacker->ReceiveAttack(20, Patient);
			EnemyExpected = Attacker->GetHealth();
			Next(1, Now);
			return false;
		}
		if (Stage == 1)
		{
			if (!After(Now, 4.7))
				return false;
			if (!Check(Patient->GetHealth() == Expected && Other->GetHealth() == OtherExpected,
					TEXT("Both stationary armies wait five uninterrupted seconds before healing")))
				return true;
			Next(2, Now);
			return false;
		}
		if (Stage == 2)
		{
			if (!After(Now, 1.2))
				return false;
			if (!Check(Patient->GetHealth() > Expected && Other->GetHealth() > OtherExpected
						&& Patient->GetHealth() <= Patient->MaxHealth() && Other->GetHealth() <= Other->MaxHealth(),
					TEXT("Both owned armies gain real bounded health after the stationary delay")))
				return true;
			if (!Check(Attacker->GetHealth() == EnemyExpected && EnemyExpected < Attacker->MaxHealth(),
					TEXT("Stationary hostile member does not inherit the owner's FieldRepairs")))
				return true;
			Expected = Patient->GetHealth();
			if (!Check(WeaponHit(Attacker, Patient) > 0,
					TEXT("Hostile weapon interrupts ongoing repairs")))
				return true;
			Expected = Patient->GetHealth();
			Next(3, Now);
			return false;
		}
		if (Stage == 3)
		{
			if (!After(Now, 3.0))
				return false;
			if (!Check(Patient->GetHealth() == Expected,
					TEXT("Taking damage resets repairs and cannot spend banked healing")))
				return true;
			// Firing a real frontline weapon is a separate interrupt.
			const int32 EnemyBefore = Attacker->GetHealth();
			if (!Check(WeaponHit(Patient, Attacker) > 0 && Attacker->GetHealth() < EnemyBefore,
					TEXT("Repairing frontline fires an actual hostile-damaging shot")))
				return true;
			Next(4, Now);
			return false;
		}
		if (Stage == 4)
		{
			if (!After(Now, 4.7))
				return false;
			if (!Check(Patient->GetHealth() == Expected,
					TEXT("Firing also restarts the full five-second quiet interval")))
				return true;
			Next(5, Now);
			return false;
		}
		if (Stage == 5)
		{
			if (!After(Now, 1.3))
				return false;
			if (!Check(Patient->GetHealth() > Expected, TEXT("Repairs resume after five seconds without fire")))
				return true;
			Patient->ReceiveAttack(20, Attacker);
			Expected = Patient->GetHealth();
			StartPosition = Patient->GetActorLocation();
			FCommandService::IssueForceOrder(Actors.Wallet.Get(), Actors.Armies[0].Get(), EForceVerb::MoveHold,
				ArmyTestSetup::TravelRegion(Actors.Armies[0].Get(), Actors.State->EnemyHeadquarters->GetActorLocation()));
			if (!Check(Actors.Armies[0]->Status == EForceStatus::Marching,
					TEXT("Owned MoveHold begins an actual navigation interruption")))
				return true;
			Next(6, Now);
			return false;
		}
		if (Stage == 6)
		{
			if (!After(Now, 2.))
				return false;
			if (!Check(FVector::Dist2D(Patient->GetActorLocation(), StartPosition) > 100.f
						&& Patient->GetVelocity().SizeSquared2D() > FMath::Square(1.f)
						&& Patient->GetHealth() == Expected,
					TEXT("Navigating member remains in motion without healing before Hold")))
				return true;
			FCommandService::IssueForceOrder(Actors.Wallet.Get(), Actors.Armies[0].Get(), EForceVerb::MoveHold,
				ArmyTestSetup::RegionAt(Actors.State.Get(), Actors.State->FriendlyHeadquarters->GetActorLocation()));
			Next(7, Now);
			return false;
		}
		if (Stage == 7)
		{
			if (Actors.Armies[0]->Status != EForceStatus::Holding
				|| Patient->GetVelocity().SizeSquared2D() > FMath::Square(1.f))
			{
				StageStarted = Now;
				return false;
			}
			if (!After(Now, 4.7))
				return false;
			if (!Check(Patient->GetHealth() == Expected,
					TEXT("Movement interruption cannot bank healing across the subsequent Hold")))
				return true;
			Next(8, Now);
			return false;
		}
		if (Stage == 8)
		{
			if (!After(Now, 1.4))
				return false;
			if (!Check(Patient->GetHealth() > Expected && Patient->GetHealth() <= Patient->MaxHealth(),
					TEXT("Stationary survivor recovers after the new complete delay")))
				return true;
			// Use an equivalent untouched group member for the near-max clamp probe.
			Patient = Actors.Armies[0]->GetUnits()[1];
			ClampPatient = Patient;
			Patient->ReceiveAttack(1, Attacker);
			WeaponHit(Attacker, Patient);
			Next(9, Now);
			return false;
		}
		if (Stage == 9)
		{
			if (!After(Now, 8.))
				return false;
			if (!Check(Patient->GetHealth() == Patient->MaxHealth()
						&& Other->GetHealth() == Other->MaxHealth(),
					TEXT("Continuous repairs clamp both living units to max health, never overflow")))
				return true;
			Patient->ReceiveAttack(Patient->GetHealth(), Attacker);
			if (!Check(!Patient->IsAlive() && !Actors.Armies[0]->GetUnits().Contains(Patient),
					TEXT("Lethal damage removes a patient; FieldRepairs cannot resurrect it")))
				return true;
			Other->ReceiveAttack(20, Attacker);
			AHeadquarters* HQ = Actors.State->EnemyHeadquarters;
			AArmyUnit* Siege = Actors.Armies[1]->GetUnits()[4];
			if (!Check(IsValid(HQ) && Siege->IsAlive(),
					TEXT("Live HQ and allied siege are available for terminal healing check")))
				return true;
			HQ->Health = 1;
			Siege->SetActorLocation(HQ->GetActorLocation() + FVector(150.f, 0.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			Siege->NextAttackTime = 0.f;
			const uint32 BeforeShot = Siege->AttackCount;
			Siege->FireAt(HQ);
			if (!Check(Siege->AttackCount == BeforeShot + 1 && HQ->Health == 0,
					TEXT("Real weapon destroys HQ before terminal repair observation")))
				return true;
			Next(10, Now);
			return false;
		}
		if (Stage == 10)
		{
			if (Actors.State->MatchResult == EMatchResult::Ongoing)
				return false;
			if (!Check(Actors.State->MatchResult == EMatchResult::Victory,
					TEXT("HQ weapon hit publishes terminal Victory")))
				return true;
			OtherExpected = Other->GetHealth();
			Next(11, Now);
			return false;
		}
		if (Stage == 11)
		{
			if (!After(Now, 6.))
				return false;
			if (!Check(Other->GetHealth() == OtherExpected && OtherExpected < Other->MaxHealth(),
					TEXT("Terminal match blocks healing even after an uninterrupted six seconds")))
				return true;
			Test->AddInfo(TEXT("Repairs: five-second delay, health gain in both armies, damage/fire/movement resets, no banking, max cap, no resurrection or terminal healing."));
			return true;
		}
		return true;
	}
	int32 Expected = 0;
	int32 OtherExpected = 0;
	int32 EnemyExpected = 0;
	FVector StartPosition = FVector::ZeroVector;
	TWeakObjectPtr<AArmyUnit> ClampPatient;
};

class FFrontlineScenario final : public FDoctrineScenario
{
public:
	using FDoctrineScenario::FDoctrineScenario;
private:
	bool Step(double Now) override
	{
		AArmyUnit* Held = Actors.Armies[0]->GetUnits()[0];
		AArmyUnit* Moving = Actors.Armies[1]->GetUnits()[0];
		AArmyUnit* Enemy = Actors.Enemy->GetUnits()[0];
		if (Stage == 0)
		{
			const int32 Baseline = WeaponHitFresh(Enemy, Held);
			if (!Check(Baseline > 0, TEXT("Hostile real weapon establishes unchosen frontline damage")))
				return true;
			ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::EntrenchedFrontline);
			if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::EntrenchedFrontline,
					TEXT("EntrenchedFrontline is the player's irreversible choice")))
				return true;
			for (const TWeakObjectPtr<AArmyGroup>& Group : Actors.Armies)
				if (!Check(Group->Verb == EForceVerb::MoveHold && Group->Status == EForceStatus::Holding,
						TEXT("Owned armies physically hold their generated region")))
					return true;
			const int32 Protected = WeaponHit(Enemy, Held);
			if (!Check(Protected == Baseline * 3 / 4 && Actors.Armies[0]->Status == EForceStatus::Holding
						&& Held->GetCharacterMovement()->Velocity.Size2D() <= 1.f,
					TEXT("Stationary held frontline takes exactly 25 percent less real weapon damage")))
				return true;
			const int32 OtherProtected = WeaponHitFresh(Enemy, Moving);
			if (!Check(OtherProtected == Protected && Actors.Armies[1]->Verb == EForceVerb::MoveHold,
					TEXT("Second owned held army gains identical protection")))
				return true;
			AArmyUnit* Piercing = Actors.Enemy->GetUnits()[2];
			const int32 PiercingProtected = (Piercing->GetDefinition()->AttackDamage * 3 / 2) * 3 / 4;
			if (!Check(WeaponHitFresh(Piercing, Held) == PiercingProtected,
					TEXT("Stationary held frontline mitigation composes with the Piercing bonus against Heavy")))
				return true;
			AArmyUnit* Ranged = Actors.Armies[0]->GetUnits()[2];
			if (!Check(Ranged->GetUnitRole() == EUnitRole::Ranged
						&& WeaponHit(Enemy, Ranged) == Baseline * 3 / 2,
					TEXT("Stationary held Light ranged takes full Kinetic bonus without frontline-only mitigation")))
				return true;
			Start = Moving->GetActorLocation();
			RetreatOrigin = Actors.Armies[1]->HoldPostLocation;
			RetreatRegion = Actors.Armies[1]->HoldRegionIndex;
			if (!Check(FCommandService::IssueForceOrder(Actors.Wallet.Get(), Actors.Armies[1].Get(), EForceVerb::MoveHold,
						   ArmyTestSetup::TravelRegion(Actors.Armies[1].Get(), Actors.State->EnemyHeadquarters->GetActorLocation()))
						   .IsAccepted(),
					TEXT("Second army can travel to a neighbouring region")))
				return true;
			BaseDamage = Baseline;
			Next(1, Now);
			return false;
		}
		if (Stage == 1)
		{
			if (!After(Now, .7) || ArmyTestSetup::CurrentRegion(Actors.Armies[1].Get()) == RetreatRegion
				|| FVector::Dist2D(Actors.Armies[1]->GetCenter(), RetreatOrigin) < 500.f)
				return false;
			if (!Check(FVector::Dist2D(Moving->GetActorLocation(), Start) > 30.f,
					TEXT("Frontline traveling to its MoveHold region actually changes position")))
				return true;
			if (!Check(WeaponHit(Enemy, Moving) == BaseDamage,
					TEXT("Moving frontline takes full real weapon damage despite chosen doctrine")))
				return true;
			if (!Check(FCommandService::IssueForceOrder(Actors.Wallet.Get(), Actors.Armies[1].Get(), EForceVerb::Retreat).IsAccepted(),
					TEXT("Retreat replaces the travelling MoveHold order")))
				return true;
			Start = Moving->GetActorLocation();
			Next(2, Now);
			return false;
		}
		if (Stage == 2)
		{
			if (!Check(Actors.Armies[1]->Status == EForceStatus::Retreating,
					TEXT("Retreat encounter must remain en route before its weapon comparison")))
				return true;
			if (FVector::Dist2D(Moving->GetActorLocation(), Start) <= 30.f)
				return false;
			if (!Check(WeaponHit(Enemy, Moving) == BaseDamage,
					TEXT("Retreat cannot obtain stationary MoveHold protection")))
				return true;
			int32 LaterRegion = INDEX_NONE;
			float Closest = TNumericLimits<float>::Max();
			for (const AMapRegion* Region : Actors.State->Regions)
			{
				if (!IsValid(Region) || Region->RegionIndex == Actors.Armies[0]->TargetRegionIndex
					|| Region->RegionIndex == Actors.Armies[1]->WaypointRegionIndex
					|| (Region->RegionRole == ERegionRole::Main && Region->HomeTeam != Actors.Wallet->TeamIndex)
					|| Actors.State->IsRegionContested(Region->RegionIndex, Actors.Wallet->TeamIndex))
					continue;
				const float Distance = FVector::DistSquared2D(Actors.State->GetRegionAnchor(Region->RegionIndex),
					Actors.State->FriendlyHeadquarters->GetActorLocation());
				if (Distance < Closest)
				{
					Closest = Distance;
					LaterRegion = Region->RegionIndex;
				}
			}
			if (!Check(LaterRegion != INDEX_NONE, TEXT("Later frontline has a vacant nonhostile region anchor")))
				return true;
			AArmyGroup* Later = ArmyTestSetup::SpawnGroup(Actors.World.Get(), Actors.Controller.Get(), 2,
				Actors.State->GetRegionAnchor(LaterRegion) + FVector(0.f, 0.f, 100.f));
			LaterFrontline = Later ? Later->GetUnits()[0].Get() : nullptr;
			if (!Check(LaterFrontline.IsValid(), TEXT("New frontline exists after research")))
				return true;
			if (!Check(FCommandService::IssueForceOrder(Actors.Wallet.Get(), Later, EForceVerb::MoveHold, LaterRegion).IsAccepted(),
					TEXT("New squad adopts MoveHold after research")))
				return true;
			Next(3, Now);
			return false;
		}
		if (Stage == 3)
		{
			if (!Settled(LaterFrontline->GetGroup()))
			{
				StageStarted = Now;
				return false;
			}
			if (!After(Now, .5))
				return false;
			AArmyUnit* Replacement = LaterFrontline.Get();
			if (!Check(Replacement && WeaponHitFresh(Enemy, Replacement) == BaseDamage * 3 / 4,
					TEXT("New stationary held frontline inherits protection against a real shot")))
				return true;
			if (!Check(FCommandService::IssueForceOrder(Actors.Wallet.Get(), Replacement->GetGroup(), EForceVerb::Attack, INDEX_NONE, Actors.State->EnemyHeadquarters).IsAccepted()
						&& WeaponHit(Enemy, Replacement) == BaseDamage,
					TEXT("Attack frontline does not receive stationary Hold mitigation")))
				return true;
			Test->AddInfo(TEXT("Entrenched: stationary Holding mitigates real hits; traveling, retreating and Attack members do not; later units inherit."));
			return true;
		}
		return true;
	}
	int32 BaseDamage = 0;
	int32 RetreatRegion = INDEX_NONE;
	FVector Start = FVector::ZeroVector;
	FVector RetreatOrigin = FVector::ZeroVector;
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
					TEXT("Old match owns a selected doctrine before the HQ assault")))
				return true;
			AHeadquarters* HQ = Actors.State->EnemyHeadquarters;
			AArmyUnit* Siege = Actors.Armies[0]->GetUnits()[4];
			if (!Check(IsValid(HQ) && Siege->IsAlive(), TEXT("Real siege and enemy HQ are available")))
				return true;
			HQ->Health = 1; // Only the weapon, never the setup, delivers the lethal hit.
			Siege->SetActorLocation(HQ->GetActorLocation() + FVector(150.f, 0.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			Siege->NextAttackTime = 0.f;
			const uint32 Before = Siege->AttackCount;
			Siege->FireAt(HQ);
			if (!Check(Siege->AttackCount == Before + 1 && HQ->Health == 0,
					TEXT("A real doctrine-bearing siege shot destroys the enemy HQ")))
				return true;
			Next(1, Now);
			return false;
		}
		if (Stage == 1)
		{
			if (Actors.State->MatchResult == EMatchResult::Ongoing)
				return false;
			if (!Check(Actors.State->MatchResult == EMatchResult::Victory,
					TEXT("Weapon-caused HQ destruction ends the match")))
				return true;
			const int32 TerminalBalance = Actors.Wallet->Resources;
			ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::EntrenchedFrontline);
			if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics && Actors.Wallet->Resources == TerminalBalance,
					TEXT("Terminal research RPC neither replaces the purchase nor charges again")))
				return true;
			OldWorld = Actors.World;
			OldState = Actors.State;
			FCommandService::Restart(Actors.Controller.Get());
			Next(2, Now);
			return false;
		}
		if (Stage == 2)
		{
			UWorld* FreshWorld = StandaloneWorld();
			if (!FreshWorld || FreshWorld == OldWorld.Get())
				return false;
			ACommandPlayerController* FreshController = ArmyTestSetup::Controller(FreshWorld);
			ACommandGameState* FreshState = FreshWorld->GetGameState<ACommandGameState>();
			ACommandPlayerState* FreshWallet = FreshController ? FreshController->GetPlayerState<ACommandPlayerState>() : nullptr;
			// Seamless travel passes through a transition world that still carries the old GameState.
			if (!FreshState || FreshState == OldState.Get() || !FreshWallet
				|| FreshWallet->CommanderIndex < 0 || !ArmyTestSetup::MapReady(FreshState))
				return false;
			if (!Check(FreshState->MatchResult == EMatchResult::Ongoing && FreshWallet->Doctrine == EArmyDoctrine::None,
					*FString::Printf(TEXT("Fresh world clears the purchased research (result=%d doctrine=%d)"),
						static_cast<int32>(FreshState->MatchResult), static_cast<int32>(FreshWallet->Doctrine))))
				return true;
			for (TActorIterator<AArmyGroup> It(FreshWorld); It; ++It)
				if (!Check(It->GetOwningPlayerState() != FreshWallet, TEXT("Restart does not recreate fixed player armies")))
					return true;
			ArmyTestSetup::Research(FreshController, EArmyDoctrine::FieldRepairs);
			if (!Check(FreshWallet->Doctrine == EArmyDoctrine::FieldRepairs,
					TEXT("Fresh match permits a different paid workshop specialization")))
				return true;
			Test->AddInfo(TEXT("Real siege HQ kill, terminal doctrine rejection, fresh-world None reset, independent new selection."));
			return true;
		}
		return true;
	}
	TWeakObjectPtr<UWorld> OldWorld;
	TWeakObjectPtr<ACommandGameState> OldState;
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
