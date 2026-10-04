#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArenaBounds.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "HAL/PlatformTime.h"
#include "NavigationSystem.h"

// World fixtures for shields, the pulse and region traits. Units are real authoritative
// actors built from test-only definitions appended to the match content; groups never tick,
// so nothing moves or fires except what a test does on purpose.
namespace CombatTraitFixture
{
struct FUnitSpec
{
	EUnitRole Role = EUnitRole::Frontline;
	EArmorClass Armor = EArmorClass::Heavy;
	EDamageType Type = EDamageType::Kinetic;
	int32 MaxHealth = 100;
	int32 MaxShield = 0;
	int32 Damage = 0;
	float Range = 500.f;
	float MoveSpeed = 400.f;
	float PulseInterval = 0.f;
	float PulseRadius = 0.f;
	float PulseStun = 0.f;
	// Seconds a producer needs for one unit when the test builds with this definition.
	float UnitDuration = 1.f;
};

class FArena
{
public:
	~FArena();
	// False until the world, map, navigation and wallets are ready; isolates the world from JEV once.
	bool Prepare();
	int32 AddUnit(const FUnitSpec& Spec);
	// Spawns a one-unit group on navigable NavGround, then teleports the unit to Where (a point at unit height).
	AArmyUnit* Spawn(bool bHostile, int32 UnitIndex, const FVector& NavGround, const FVector& Where);
	// Teleports a unit and re-reads its region, as a move across a border would on the next refresh.
	void Place(AArmyUnit* Unit, const FVector& Location) const;
	// Two roomy, navigable non-main regions whose polygons hold a 1200 cm strip around the anchor.
	bool PickRegions(AMapRegion*& First, AMapRegion*& Second);
	bool Ground(const AMapRegion& Region, FVector& Out) const;
	bool Roomy(const AMapRegion& Region) const;
	void SetTrait(AMapRegion& Region, ERegionTrait Trait);
	double Now() const { return World.IsValid() ? World->GetTimeSeconds() : 0.; }

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ACommandGameState> State;
	TWeakObjectPtr<ACommandPlayerController> Owner;
	TWeakObjectPtr<ACommandPlayerState> Wallet;

private:
	bool bIsolated = false;
	TArray<UArmyUnitDefinition*> Definitions;
	TArray<TWeakObjectPtr<AArmyGroup>> Groups;
	TArray<TPair<TWeakObjectPtr<AMapRegion>, ERegionTrait>> Traits;
};

inline FArena::~FArena()
{
	for (const TWeakObjectPtr<AArmyGroup>& Group : Groups)
		if (Group.IsValid())
			Group->Destroy();
	for (const TPair<TWeakObjectPtr<AMapRegion>, ERegionTrait>& Original : Traits)
		if (Original.Key.IsValid())
			Original.Key->Trait = Original.Value;
	if (State.IsValid() && IsValid(State->Content))
		State->Content->Units.RemoveAll([this](const UArmyUnitDefinition* Unit) { return Definitions.Contains(Unit); });
}

inline bool FArena::Prepare()
{
	UWorld* Current = ArmyTestSetup::World();
	if (!Current)
		return false;
	// Remove planning immediately, before waiting for dynamic navigation.
	for (TActorIterator<AEnemyCommander> It(Current); It; ++It)
		It->Destroy();
	ACommandGameState* Game = Current->GetGameState<ACommandGameState>();
	ACommandPlayerController* Controller = ArmyTestSetup::Controller(Current);
	if (!ArmyTestSetup::MapReady(Game) || !Controller || Current->GetTimeSeconds() < 3. || !ArmyTestSetup::NavigationReady(Current))
		return false;
	ACommandPlayerState* Commander = Controller->GetPlayerState<ACommandPlayerState>();
	if (!Commander || Commander->CommanderIndex < 0 || !IsValid(Game->EnemyCommander) || !IsValid(Game->Content))
		return false;
	World = Current;
	State = Game;
	Owner = Controller;
	Wallet = Commander;
	if (!bIsolated)
	{
		// Stop paid JEV reinforcements and remove its starting forces so only the fixture fights.
		for (TActorIterator<ACommandBuilding> It(Current); It; ++It)
			if (It->TeamIndex == 5 && It->IsProducer())
				FCommandService::ConfigureProduction(Game->EnemyCommander, *It,
					It->bForceConfigured ? It->ProductionRole : static_cast<EUnitRole>(255), false);
		for (TActorIterator<AArmyGroup> It(Current); It; ++It)
			if (It->GetTeamIndex() == 5)
				It->Destroy();
		bIsolated = true;
	}
	return true;
}

inline int32 FArena::AddUnit(const FUnitSpec& Spec)
{
	UArmyUnitDefinition* Definition = NewObject<UArmyUnitDefinition>(State->Content);
	Definition->Id = FName(*FString::Printf(TEXT("fixture-%d"), State->Content->Units.Num()));
	Definition->Role = Spec.Role;
	Definition->ArmorClass = Spec.Armor;
	Definition->DamageType = Spec.Type;
	Definition->MaxHealth = Spec.MaxHealth;
	Definition->MaxShield = Spec.MaxShield;
	Definition->AttackDamage = Spec.Damage;
	Definition->Range = Spec.Range;
	// Fixtures fire by hand; the interval only has to outlast a test.
	Definition->Interval = 1000.f;
	Definition->MoveSpeed = Spec.MoveSpeed;
	Definition->PulseInterval = Spec.PulseInterval;
	Definition->PulseRadius = Spec.PulseRadius;
	Definition->PulseBuildingStunSeconds = Spec.PulseStun;
	Definition->UnitCost = 1;
	Definition->Capacity = 6;
	Definition->UnitDuration = Spec.UnitDuration;
	Definitions.Add(Definition);
	return State->Content->Units.Add(Definition);
}

inline AArmyUnit* FArena::Spawn(bool bHostile, int32 UnitIndex, const FVector& NavGround, const FVector& Where)
{
	ACommandPlayerState* Commander = bHostile ? State->EnemyCommander.Get() : Wallet.Get();
	const FTransform Transform(NavGround);
	AArmyGroup* Group = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
		bHostile ? nullptr : Owner.Get(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Group)
		return nullptr;
	Group->Initialize({ bHostile ? 5 : 0, Commander, bHostile ? -1 : 0, nullptr, NavGround });
	Group->FinishSpawning(Transform);
	Group->SetActorTickEnabled(false);
	Groups.Add(Group);
	AArmyUnit* Unit = Group->SpawnMember(UnitIndex, NavGround, 0);
	if (!Unit)
		Group->Destroy();
	else
		Place(Unit, Where);
	return Unit;
}

inline void FArena::Place(AArmyUnit* Unit, const FVector& Location) const
{
	Unit->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	Unit->RefreshRegion();
}

inline bool FArena::Ground(const AMapRegion& Region, FVector& Out) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
	FNavLocation Result;
	const FVector Anchor = State->GetRegionAnchor(Region.RegionIndex);
	if (!Nav || !Nav->ProjectPointToNavigation(Anchor, Result, FVector(35.f, 35.f, 300.f)) || FVector::Dist2D(Anchor, Result.Location) > 35.f)
		return false;
	Out = Result.Location;
	return true;
}

inline bool FArena::Roomy(const AMapRegion& Region) const
{
	const FVector Anchor = State->GetRegionAnchor(Region.RegionIndex);
	FVector Unused;
	if (Region.RegionRole == ERegionRole::Main || !Ground(Region, Unused))
		return false;
	for (const FVector& Offset : { FVector(-600.f, 0.f, 0.f), FVector(600.f, 0.f, 0.f), FVector(0.f, 300.f, 0.f), FVector(0.f, -300.f, 0.f) })
		if (!Region.Contains(Anchor + Offset))
			return false;
	return true;
}

inline bool FArena::PickRegions(AMapRegion*& First, AMapRegion*& Second)
{
	First = Second = nullptr;
	for (AMapRegion* Region : State->Regions)
		if (IsValid(Region) && Roomy(*Region))
			(First ? Second : First) = Region;
	return First && Second;
}

inline void FArena::SetTrait(AMapRegion& Region, ERegionTrait Trait)
{
	Traits.Emplace(&Region, Region.Trait);
	Region.Trait = Trait;
}

// One latent scenario: waits for the arena, runs Setup once, then Step each frame until it returns true.
class FScenario : public IAutomationLatentCommand
{
public:
	explicit FScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 150.)
		{
			Test->AddError(FString::Printf(TEXT("Scenario timed out at stage %d"), Stage));
			return true;
		}
		if (!bSetUp)
		{
			if (!Arena.Prepare())
				return false;
			bSetUp = true;
			if (!Setup())
				return true;
			StageStarted = Arena.Now();
		}
		return Step(Arena.Now());
	}

protected:
	virtual bool Setup() = 0;
	virtual bool Step(double Now) = 0;

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
	// One authoritative weapon shot, cooldown cleared, from where the shooter stands.
	static void Shoot(AArmyUnit* Shooter, AArmyUnit* Victim)
	{
		Shooter->NextAttackTime = 0.f;
		Shooter->FireAt(Victim);
	}

	FAutomationTestBase* Test;
	FArena Arena;
	int32 Stage = 0;
	double StageStarted = 0.;

private:
	double Started;
	bool bSetUp = false;
};
}

#endif
