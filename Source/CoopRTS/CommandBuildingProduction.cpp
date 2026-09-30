#include "CommandBuilding.h"

#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "NavigationSystem.h"

namespace
{
	bool IsArenaLocation(const FVector& Location)
	{
		return !Location.ContainsNaN() && FMath::Abs(Location.X) <= 4500.f
			&& FMath::Abs(Location.Y) <= 4500.f && FMath::Abs(Location.Z) <= 1000.f;
	}

	bool GetBalance(const ACommandBuilding& Building, const ACommandGameState& State, int32& Balance)
	{
		if (Building.TeamIndex == 5 && !Building.OwningPlayerState)
		{
			Balance = State.EnemyResources;
			return true;
		}
		const ACommandPlayerState* Owner = Building.OwningPlayerState;
		if (Building.TeamIndex != 0 || !IsValid(Owner) || Owner->GetWorld() != Building.GetWorld()
			|| Owner->CommanderIndex < 0 || Owner->CommanderIndex >= 5) return false;
		Balance = Owner->Resources;
		return true;
	}

	bool FindExit(const ACommandBuilding& Building, FVector& OutLocation, int32& Cursor)
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Building.GetWorld());
		if (!Navigation) return false;
		static const FVector Directions[] = {
			{1, 0, 0}, {.707107, .707107, 0}, {0, 1, 0}, {-.707107, .707107, 0},
			{-1, 0, 0}, {-.707107, -.707107, 0}, {0, -1, 0}, {.707107, -.707107, 0}
		};
		const float Radius = ACommandBuilding::GetFootprintRadius(Building.Kind) + 240.f;
		while (Cursor < 24)
		{
			const int32 Candidate = Cursor++;
			const FVector Desired = Building.GetActorLocation()
				+ Directions[Candidate % 8] * (Radius + (Candidate / 8) * 145.f);
			FNavLocation Projected;
			if (!IsArenaLocation(Desired)
				|| !Navigation->ProjectPointToNavigation(Desired, Projected, FVector(45.f, 45.f, 200.f))
				|| !IsArenaLocation(Projected.Location)
				|| FVector::DistSquared2D(Desired, Projected.Location) > FMath::Square(45.f)
				|| FMath::Abs(Desired.Z - Projected.Location.Z) > 110.f
				|| FVector::DistSquared2D(Projected.Location, Building.GetActorLocation())
					< FMath::Square(ACommandBuilding::GetFootprintRadius(Building.Kind) + 75.f)
				|| Building.GetWorld()->OverlapBlockingTestByChannel(Projected.Location + FVector(0.f, 0.f, 85.f),
					FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(34.f, 60.f))) continue;
			OutLocation = Projected.Location;
			return true;
		}
		return false;
	}

	int32 NextArmyIndex(const UWorld& World)
	{
		int32 Index = 0;
		for (const ULevel* Level : World.GetLevels())
		{
			if (!Level) continue;
			for (const AActor* Actor : Level->Actors)
				if (const AArmyGroup* Group = Cast<AArmyGroup>(Actor); IsValid(Group))
					Index = FMath::Max(Index, Group->ArmyIndex + 1);
		}
		return Index;
	}
}

bool ACommandBuilding::SetProduction(EUnitRole Role, bool bEnabled)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	int32 Balance = 0;
	if (!HasAuthority() || IsActorBeingDestroyed() || !State || State->MatchResult != EMatchResult::Ongoing
		|| Kind != EBuildingKind::Barracks || !IsAlive() || !IsComplete() || GetForceCapacity(Role) == 0
		|| !GetBalance(*this, *State, Balance) || (bForceConfigured && ProductionRole != Role)) return false;
	if (bEnabled && !bForceConfigured)
	{
		const int32 ConfigurationCost = GetConfigurationCost(Role);
		FVector Assembly;
		int32 ExitCursor = 0;
		if (Balance < ConfigurationCost || !FindExit(*this, Assembly, ExitCursor)) return false;
		const FTransform Transform(FRotator::ZeroRotator, Assembly);
		AActor* ControllerOwner = TeamIndex == 0 ? OwningPlayerState->GetOwner() : nullptr;
		AArmyGroup* Group = GetWorld()->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
			ControllerOwner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Group) return false;
		Group->TeamIndex = TeamIndex;
		Group->bOpposingArmy = TeamIndex == 5;
		Group->OwningPlayerState = OwningPlayerState;
		Group->ArmyIndex = NextArmyIndex(*GetWorld());
		Group->ProductionBuilding = this;
		Group->HomeLocation = Assembly;
		Group->FinishSpawning(Transform);
		bool bAcceptedFront = false;
		if (IsValid(Group))
		{
			do
			{
				Group->HomeLocation = Assembly;
				bAcceptedFront = Group->AssignFront(FrontOrder, bHasConfiguredFront ? FrontLocation : Assembly);
			}
			while (!bAcceptedFront && FindExit(*this, Assembly, ExitCursor));
		}
		if (!bAcceptedFront || (ConfigurationCost > 0 && !TrySpend(ConfigurationCost)))
		{
			if (IsValid(Group)) Group->Destroy();
			return false;
		}
		Group->SetActorLocation(Assembly);
		ForceGroup = Group;
		bForceConfigured = true;
	}
	else if (bEnabled && (!IsValid(ForceGroup) || ForceGroup->IsActorBeingDestroyed())) return false;
	if (ProductionRole != Role) ProductionProgressSeconds = 0.f;
	ProductionRole = Role;
	bProductionEnabled = bEnabled;
	ProductionCheckAccumulator = 0.f;
	ForceNetUpdate();
	return true;
}

bool ACommandBuilding::SetFront(EFrontOrder Order, const FVector& Location)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || IsActorBeingDestroyed() || !State || State->MatchResult != EMatchResult::Ongoing
		|| Kind != EBuildingKind::Barracks || !IsAlive() || !IsComplete()
		|| (Order != EFrontOrder::Secure && Order != EFrontOrder::Defend && Order != EFrontOrder::FallBack)
		|| !IsArenaLocation(Location)) return false;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (!Navigation || !Navigation->ProjectPointToNavigation(Location, Projected, FVector(75.f, 75.f, 200.f))
		|| !IsArenaLocation(Projected.Location)
		|| FVector::DistSquared2D(Location, Projected.Location) > FMath::Square(75.f)
		|| FMath::Abs(Location.Z - Projected.Location.Z) > 110.f) return false;
	if (IsValid(ForceGroup) && !ForceGroup->AssignFront(Order, Projected.Location)) return false;
	FrontOrder = Order;
	FrontLocation = Projected.Location;
	bHasConfiguredFront = true;
	ForceNetUpdate();
	return true;
}

int32 ACommandBuilding::GetUnitCost(EUnitRole Role)
{
	switch (Role)
	{
	case EUnitRole::Frontline: return 20;
	case EUnitRole::Ranged: return 30;
	case EUnitRole::Siege: return 50;
	default: return 0;
	}
}

float ACommandBuilding::GetUnitDuration(EUnitRole Role)
{
	switch (Role)
	{
	case EUnitRole::Frontline: return 10.f / 3.f;
	case EUnitRole::Ranged: return 13.f / 3.f;
	case EUnitRole::Siege: return 20.f / 3.f;
	default: return 0.f;
	}
}

int32 ACommandBuilding::GetForceCapacity(EUnitRole Role)
{
	switch (Role)
	{
	case EUnitRole::Frontline: return 6;
	case EUnitRole::Ranged: return 4;
	case EUnitRole::Siege: return 2;
	default: return 0;
	}
}

int32 ACommandBuilding::GetConfigurationCost(EUnitRole Role)
{
	return Role == EUnitRole::Siege ? 180 : 0;
}

int32 ACommandBuilding::GetProductionCost() const { return GetUnitCost(ProductionRole); }
float ACommandBuilding::GetProductionDuration() const { return GetUnitDuration(ProductionRole); }

void ACommandBuilding::GetForceCounts(int32& OutJoined, int32& OutTravelling) const
{
	OutJoined = OutTravelling = 0;
	if (!IsValid(ForceGroup)) return;
	for (const AArmyUnit* Unit : ForceGroup->Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive()) continue;
		if (Unit->bReinforcing) ++OutTravelling;
		else ++OutJoined;
	}
}

FString ACommandBuilding::GetProductionStatus() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!State || State->MatchResult != EMatchResult::Ongoing) return TEXT("MATCH FINISHED");
	if (Kind != EBuildingKind::Barracks || !IsAlive()) return TEXT("NO BARRACKS");
	if (!IsComplete()) return TEXT("UNDER CONSTRUCTION");
	if (!bForceConfigured) return TEXT("UNCONFIGURED");
	if (!IsValid(ForceGroup)) return TEXT("FORCE UNAVAILABLE");
	if (!bProductionEnabled) return TEXT("PAUSED");
	int32 Joined, Travelling;
	GetForceCounts(Joined, Travelling);
	if (Joined + Travelling >= GetForceCapacity(ProductionRole)) return TEXT("FORCE COMPLETE");
	int32 Balance = 0;
	if (!GetBalance(*this, *State, Balance)) return TEXT("WALLET UNAVAILABLE");
	if (Balance < GetProductionCost()) return TEXT("INSUFFICIENT RESOURCES");
	if (ProductionProgressSeconds >= GetProductionDuration()) return TEXT("DEPLOYMENT BLOCKED");
	return TEXT("PRODUCING");
}

void ACommandBuilding::TickProduction(float DeltaSeconds)
{
	if (!HasAuthority() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f) return;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State || State->MatchResult != EMatchResult::Ongoing || Kind != EBuildingKind::Barracks
		|| !IsAlive() || !IsComplete() || !bForceConfigured || !bProductionEnabled
		|| !IsValid(ForceGroup) || ForceGroup->IsActorBeingDestroyed())
	{
		ProductionCheckAccumulator = 0.f;
		return;
	}
	int32 Joined, Travelling, Balance = 0;
	GetForceCounts(Joined, Travelling);
	if (Joined + Travelling >= GetForceCapacity(ProductionRole)
		|| !GetBalance(*this, *State, Balance) || Balance < GetProductionCost())
	{
		ProductionCheckAccumulator = 0.f;
		return;
	}
	const float Duration = GetProductionDuration();
	ProductionCheckAccumulator += DeltaSeconds;
	ProductionProgressSeconds = FMath::Min(Duration, ProductionProgressSeconds + DeltaSeconds);
	if (ProductionProgressSeconds < Duration) return;
	if (ProductionCheckAccumulator < .25f) return;
	ProductionCheckAccumulator = 0.f;
	FVector Exit;
	int32 ExitCursor = 0;
	bool bDeployed = false;
	while (FindExit(*this, Exit, ExitCursor))
	{
		if (ForceGroup->SpawnReinforcement(ProductionRole, Exit)) { bDeployed = true; break; }
	}
	if (!bDeployed) return;
	// Spawn and its accepted complete path precede the debit. A failed debit removes
	// only this new candidate; the persistent force and prior members are untouched.
	if (!TrySpend(GetProductionCost()))
	{
		AArmyUnit* Candidate = ForceGroup->Units.Pop(EAllowShrinking::No);
		if (IsValid(Candidate))
		{
			if (AController* Controller = Candidate->GetController()) Controller->Destroy();
			Candidate->Destroy();
		}
		ForceGroup->ForceNetUpdate();
		return;
	}
	ProductionProgressSeconds = 0.f;
	ForceNetUpdate();
}
