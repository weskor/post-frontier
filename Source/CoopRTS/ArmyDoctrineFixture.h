#pragma once

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

namespace ArmyDoctrineFixture
{
inline UWorld* StandaloneWorld()
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
				if (Now < 3. || !ArmyTestSetup::NavigationReady(World) || !Actors.Find(World))
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
}

#endif
