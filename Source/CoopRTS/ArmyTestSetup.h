#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Content/MatchContent.h"
#include "Commands/CommandService.h"
#include "EnemyCommander.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "Rules/ForceOrderPolicy.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"

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
inline int32 RegionAt(const ACommandGameState* State, const FVector& Location)
{
	const AMapRegion* Region = State ? State->FindRegionAt(Location) : nullptr;
	return Region ? Region->RegionIndex : INDEX_NONE;
}
inline int32 CurrentRegion(const AArmyGroup* Force)
{
	return Force ? RegionAt(Force->GetWorld()->GetGameState<ACommandGameState>(), Force->GetCenter()) : INDEX_NONE;
}
// Choose a reachable neighbouring polygon, rather than manufacturing a ground destination.
inline int32 TravelRegion(const AArmyGroup* Force, const FVector& Preferred)
{
	const ACommandGameState* State = Force ? Force->GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const AMapRegion* Current = State ? State->FindRegionAt(Force->GetCenter()) : nullptr;
	if (!Current)
		return INDEX_NONE;
	int32 Best = INDEX_NONE;
	float Distance = TNumericLimits<float>::Max();
	for (const AMapRegion* Region : State->Regions)
	{
		if (!IsValid(Region) || !Current->Neighbours.Contains(Region->RegionIndex)
			|| (Region->RegionRole == ERegionRole::Main && Region->HomeTeam != Force->GetTeamIndex()))
			continue;
		const float Candidate = FVector::DistSquared2D(State->GetRegionAnchor(Region->RegionIndex), Preferred);
		if (Candidate < Distance || (Candidate == Distance && Region->RegionIndex < Best))
		{
			Distance = Candidate;
			Best = Region->RegionIndex;
		}
	}
	return Best;
}
inline UWorld* World()
{
	if (GEngine)
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (UWorld* Candidate = Context.World())
				if (Candidate->IsGameWorld() && Candidate->GetNetMode() == NM_Standalone)
					return Candidate;
	return nullptr;
}
// Scenario stage waits use the world clock. Automation runs on an uncapped fixed
// step (Tools/x/testing.py), so game time outruns wall time; failure deadlines
// stay on FPlatformTime::Seconds to bound real hangs.
inline double GameSeconds(const UWorld* World)
{
	return World ? World->GetTimeSeconds() : 0.;
}
// Async navmesh builds run at wall speed on worker threads, so a fast fixed step
// can pass a game-time warm-up before any tile exists.
inline bool NavigationReady(UWorld* World)
{
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	return Navigation && !Navigation->IsNavigationBuildInProgress();
}
inline ACommandPlayerController* Controller(UWorld* World)
{
	for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
		if (It->IsLocalController())
			return *It;
	return nullptr;
}
inline AArmyGroup* SpawnGroup(UWorld* World, ACommandPlayerController* Owner, int32 Index, const FVector& Home)
{
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	ACommandPlayerState* Wallet = Owner ? Owner->GetPlayerState<ACommandPlayerState>()
		: State                         ? State->EnemyCommander.Get()
										: nullptr;
	if (!IsValid(Wallet))
		return nullptr;
	const FTransform Transform(Home);
	AArmyGroup* Group = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
		Owner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Group)
		return nullptr;
	Group->Initialize({ Owner ? 0 : 5, Wallet, Index, nullptr, Home });
	Group->FinishSpawning(Transform);
	if (!Group->SpawnUnits())
	{
		Group->Destroy();
		return nullptr;
	}
	return Group;
}
// Combat/order tests explicitly arrange mixed-role opponents. Normal matches
// start without armies; these fixtures do not prove barracks production.
inline bool CombatActors(UWorld* World)
{
	if (!World)
		return false;
	ACommandPlayerController* Owner = Controller(World);
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	if (!Owner || !Owner->GetPlayerState<ACommandPlayerState>()
		|| Owner->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0 || !MapReady(State))
		return false;
	bool Present[3] = {};
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
	{
		if (It->GetTeamIndex() == 5)
			Present[2] = true;
		else if (It->GetOwner() == Owner && It->GetArmyIndex() >= 0 && It->GetArmyIndex() < 2)
			Present[It->GetArmyIndex()] = true;
	}
	for (TActorIterator<AEnemyCommander> It(World); It; ++It)
		It->Destroy();
	for (int32 Index = 0; Index < 2; ++Index)
		if (!Present[Index] && !SpawnGroup(World, Owner, Index, FromFriendlyHQ(State, 1700.f - Index * 1000.f, 600.f, 100.f)))
			return false;
	if (!Present[2] && !SpawnGroup(World, nullptr, -1, HostileStaging(State)))
		return false;
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
		if (It->Kind == EBuildingKind::Workshop && It->OwningPlayerState == Wallet)
		{
			Workshop = *It;
			break;
		}
	if (!Workshop)
	{
		if (!MapReady(State))
			return;
		// Behind the friendly HQ, one row per commander so fixtures never overlap.
		const FTransform Transform(FromFriendlyHQ(State, -300.f, -800.f - Wallet->CommanderIndex * 350.f, 5.f));
		Workshop = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
			Owner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Workshop)
			return;
		Workshop->BuildingIndex = WorkshopIndex; // Kind derives from the definition on BeginPlay.
		Workshop->OwningPlayerState = Wallet;
		Workshop->ConstructionProgress = 1.f;
		Workshop->FinishSpawning(Transform);
	}
	FCommandService::Research(Wallet, Workshop, Choice);
}
}
#endif
