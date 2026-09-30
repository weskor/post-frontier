#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "EnemyCommander.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace ArmyTestSetup
{
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
	const FTransform Transform(Home);
	AArmyGroup* Group = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
		Owner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Group) return nullptr;
	Group->HomeLocation = Home;
	Group->TeamIndex = Owner ? 0 : 5;
	Group->ArmyIndex = Index;
	Group->bOpposingArmy = !Owner;
	Group->OwningPlayerState = Owner ? Owner->GetPlayerState<ACommandPlayerState>() : nullptr;
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
	if (!Owner || !Owner->GetPlayerState<ACommandPlayerState>()
		|| Owner->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0) return false;
	bool Present[3] = {};
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
	{
		if (It->TeamIndex == 5) Present[2] = true;
		else if (It->GetOwner() == Owner && It->ArmyIndex >= 0 && It->ArmyIndex < 2) Present[It->ArmyIndex] = true;
	}
	for (TActorIterator<AEnemyCommander> It(World); It; ++It) It->Destroy();
	for (int32 Index = 0; Index < 2; ++Index)
		if (!Present[Index] && !SpawnGroup(World, Owner, Index, FVector(-1800.f - Index * 1000.f, 0.f, 100.f))) return false;
	if (!Present[2] && !SpawnGroup(World, nullptr, -1, FVector(1800.f, 2300.f, 100.f))) return false;
	return true;
}
// A completed workshop is an explicit fixture for effect tests. Construction
// and production are exercised separately through their real paid lifecycle.
inline void Research(ACommandPlayerController* Owner, EArmyDoctrine Choice)
{
	UWorld* World = Owner->GetWorld();
	ACommandPlayerState* Wallet = Owner->GetPlayerState<ACommandPlayerState>();
	ACommandBuilding* Workshop = nullptr;
	for (TActorIterator<ACommandBuilding> It(World); It; ++It)
		if (It->Kind == EBuildingKind::Workshop && It->OwningPlayerState == Wallet) { Workshop = *It; break; }
	if (!Workshop)
	{
		const FTransform Transform(FVector(-3800.f, -1400.f - Wallet->CommanderIndex * 350.f, 5.f));
		Workshop = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
			Owner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Workshop) return;
		Workshop->Kind = EBuildingKind::Workshop;
		Workshop->OwningPlayerState = Wallet;
		Workshop->ConstructionProgress = 1.f;
		Workshop->FinishSpawning(Transform);
	}
	Owner->ServerResearch(Workshop, Choice);
}
}
#endif
