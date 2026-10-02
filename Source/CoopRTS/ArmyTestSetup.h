#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Content/MatchContent.h"
#include "EnemyCommander.h"
#include "Headquarters.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace ArmyTestSetup
{
// Building definition indices in DA_MatchContent (contract-pinned order).
constexpr int32 BarracksIndex = 0, ExtractorIndex = 1, WorkshopIndex = 2;
// Unit definition index for a legacy role in DA_MatchContent; -1 when absent.
inline int32 UnitIndex(const ACommandGameState* State, EUnitRole Role)
{
	return State && IsValid(State->Content) ? State->Content->UnitIndexForRole(Role) : -1;
}
inline bool MapReady(const ACommandGameState* State)
{
	return State && IsValid(State->FriendlyHeadquarters) && IsValid(State->EnemyHeadquarters) && IsValid(State->Arena)
		&& !State->Regions.IsEmpty() && !State->Deposits.IsEmpty();
}
// Fixture positions are offsets from the placed HQ actors; the level, not the
// test, decides where the map is. Z is the caller's spawn height.
inline FVector FromFriendlyHQ(const ACommandGameState* State, float OffsetX, float OffsetY, float Z)
{
	const FVector HQ = State->FriendlyHeadquarters->GetActorLocation();
	return FVector(HQ.X + OffsetX, HQ.Y + OffsetY, Z);
}
inline FVector FromEnemyHQ(const ACommandGameState* State, float OffsetX, float OffsetY, float Z)
{
	const FVector HQ = State->EnemyHeadquarters->GetActorLocation();
	return FVector(HQ.X + OffsetX, HQ.Y + OffsetY, Z);
}
// Hostile fixtures stage ahead of the enemy HQ, outside friendly territory.
inline FVector HostileStaging(const ACommandGameState* State) { return FromEnemyHQ(State, -1400.f, 0.f, 100.f); }
// A point no placement rule can accept: twice the arena half extent.
inline FVector OutsideArena(const ACommandGameState* State)
{
	return FVector(State->Arena->HalfExtent.X * 2.f, State->Arena->HalfExtent.Y * 2.f, 5.f);
}
inline UWorld* World()
{
	if (GEngine)
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (UWorld* Candidate = Context.World())
				if (Candidate->IsGameWorld() && Candidate->GetNetMode() == NM_Standalone) return Candidate;
	return nullptr;
}
inline ACommandPlayerController* Controller(UWorld* World)
{
	for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
		if (It->IsLocalController()) return *It;
	return nullptr;
}
inline AArmyGroup* SpawnGroup(UWorld* World, ACommandPlayerController* Owner, int32 Index, const FVector& Home)
{
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	ACommandPlayerState* Wallet = Owner ? Owner->GetPlayerState<ACommandPlayerState>()
		: State ? State->EnemyCommander.Get() : nullptr;
	if (!IsValid(Wallet)) return nullptr;
	const FTransform Transform(Home);
	AArmyGroup* Group = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
		Owner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Group) return nullptr;
	Group->Initialize({Owner ? 0 : 5, Wallet, Index, nullptr, Home});
	Group->FinishSpawning(Transform);
	if (!Group->SpawnUnits()) { Group->Destroy(); return nullptr; }
	return Group;
}
// Combat/order tests explicitly arrange mixed-role opponents. Normal matches
// start without armies; these fixtures do not prove barracks production.
inline bool CombatActors(UWorld* World)
{
	if (!World) return false;
	ACommandPlayerController* Owner = Controller(World);
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	if (!Owner || !Owner->GetPlayerState<ACommandPlayerState>()
		|| Owner->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0 || !MapReady(State)) return false;
	bool Present[3] = {};
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
	{
		if (It->GetTeamIndex() == 5) Present[2] = true;
		else if (It->GetOwner() == Owner && It->GetArmyIndex() >= 0 && It->GetArmyIndex() < 2) Present[It->GetArmyIndex()] = true;
	}
	for (TActorIterator<AEnemyCommander> It(World); It; ++It) It->Destroy();
	for (int32 Index = 0; Index < 2; ++Index)
		if (!Present[Index] && !SpawnGroup(World, Owner, Index, FromFriendlyHQ(State, 1700.f - Index * 1000.f, 600.f, 100.f))) return false;
	if (!Present[2] && !SpawnGroup(World, nullptr, -1, HostileStaging(State))) return false;
	return true;
}
// A completed workshop is an explicit fixture for effect tests. Construction
// and production are exercised separately through their real paid lifecycle.
inline void Research(ACommandPlayerController* Owner, EArmyDoctrine Choice)
{
	UWorld* World = Owner->GetWorld();
	ACommandPlayerState* Wallet = Owner->GetPlayerState<ACommandPlayerState>();
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	ACommandBuilding* Workshop = nullptr;
	for (TActorIterator<ACommandBuilding> It(World); It; ++It)
		if (It->Kind == EBuildingKind::Workshop && It->OwningPlayerState == Wallet) { Workshop = *It; break; }
	if (!Workshop)
	{
		if (!MapReady(State)) return;
		// Behind the friendly HQ, one row per commander so fixtures never overlap.
		const FTransform Transform(FromFriendlyHQ(State, -300.f, -800.f - Wallet->CommanderIndex * 350.f, 5.f));
		Workshop = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
			Owner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Workshop) return;
		Workshop->BuildingIndex = WorkshopIndex; // Kind derives from the definition on BeginPlay.
		Workshop->OwningPlayerState = Wallet;
		Workshop->ConstructionProgress = 1.f;
		Workshop->FinishSpawning(Transform);
	}
	Owner->ServerResearch(Workshop, Choice);
}
}
#endif
