#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

// Shared by the release and chain world scenarios. JEV's own economy is quarantined: its
// buildings and forces are removed, income is paused and its wallet is set by the test, so
// every change in what JEV owns or holds comes from the behaviour under test.
namespace JevWorldKit
{
struct FKit
{
	UWorld* World = nullptr;
	ACommandGameState* State = nullptr;
	ACommandPlayerController* PC = nullptr;
	ACommandPlayerState* Wallet = nullptr;
	AEnemyCommander* Planner = nullptr;
};

// True once the world, map, controller and JEV's planner exist and navigation is built.
inline bool Acquire(FKit& Kit)
{
	Kit.World = ArmyTestSetup::World();
	if (!Kit.World || ArmyTestSetup::GameSeconds(Kit.World) < 3. || !ArmyTestSetup::NavigationReady(Kit.World))
		return false;
	Kit.State = Kit.World->GetGameState<ACommandGameState>();
	Kit.PC = ArmyTestSetup::Controller(Kit.World);
	Kit.Wallet = Kit.PC ? Kit.PC->GetPlayerState<ACommandPlayerState>() : nullptr;
	if (!ArmyTestSetup::MapReady(Kit.State) || !Kit.Wallet || Kit.Wallet->CommanderIndex < 0
		|| !IsValid(Kit.State->EnemyCommander))
		return false;
	Kit.Planner = nullptr;
	for (TActorIterator<AEnemyCommander> It(Kit.World); It && !Kit.Planner; ++It)
		if (It->TeamIndex == 5)
			Kit.Planner = *It;
	return Kit.Planner != nullptr;
}

// Removes JEV's buildings, forces and planner, then starts a fresh planner whose match clock reads
// 5 s. World time before a scenario can start is not under test control (navigation builds at wall
// speed), so the original planner may already have launched waves.
inline bool Quarantine(FKit& Kit, int32 JevPower)
{
	for (TActorIterator<AEnemyCommander> It(Kit.World); It; ++It)
		It->Destroy();
	for (TActorIterator<ACommandBuilding> It(Kit.World); It; ++It)
		if (It->TeamIndex == 5)
			It->Destroy();
	for (TActorIterator<AArmyGroup> It(Kit.World); It; ++It)
		if (It->GetTeamIndex() == 5)
			It->Destroy();
	Kit.State->bVerificationIncomePaused = true;
	Kit.State->EnemyCommander->Resources = JevPower;
	// The humans only need to survive; combat outcomes are not under test.
	Kit.State->FriendlyHeadquarters->Health = 1000000;
	Kit.Planner = Kit.World->SpawnActor<AEnemyCommander>();
	if (!Kit.Planner)
		return false;
	Kit.Planner->SkipClock(5.f - Kit.Planner->GetMatchSeconds());
	return true;
}

inline void Park(AArmyGroup& Force)
{
	Force.SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Force.GetUnits())
		Unit->SetActorTickEnabled(false);
}

// Living JEV forces, in force-number order.
inline TArray<AArmyGroup*> EnemyForces(UWorld* World)
{
	TArray<AArmyGroup*> Forces;
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		if (It->GetTeamIndex() == 5 && It->GetAliveCount() > 0)
			Forces.Add(*It);
	Forces.Sort([](const AArmyGroup& A, const AArmyGroup& B) { return A.ForceNumber < B.ForceNumber; });
	return Forces;
}

inline int32 CountUnits(const TArray<AArmyGroup*>& Forces, int32 UnitIndex)
{
	int32 Count = 0;
	for (const AArmyGroup* Force : Forces)
		for (const AArmyUnit* Unit : Force->GetUnits())
			Count += IsValid(Unit) && Unit->IsAlive() && Unit->GetUnitIndex() == UnitIndex;
	return Count;
}
}

#endif
