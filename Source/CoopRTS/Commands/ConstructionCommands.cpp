#include "CommandService.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "DepositSite.h"
#include "Content/MatchContent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Rules/EconomyPolicy.h"

namespace
{
bool IsBuildingOwner(const ACommandGameState& State, const ACommandPlayerState* Commander, int32 Team)
{
	if (!IsValid(Commander) || Commander->GetWorld() != State.GetWorld() || Commander->TeamIndex != Team)
		return false;
	if (Team == 0)
		return Commander->CommanderIndex >= 0 && Commander->CommanderIndex < 5
			&& State.PlayerArray.ContainsByPredicate([Commander](const TObjectPtr<APlayerState>& Player) { return Player.Get() == Commander; });
	return Team == 5 && Commander == State.EnemyCommander;
}

ADepositSite* FindFreeDepositAt(const ACommandGameState& State, const FVector& Location)
{
	for (ADepositSite* Candidate : State.Deposits)
	{
		if (!IsValid(Candidate) || IsValid(Candidate->Extractor))
			continue;
		const FVector Position = Candidate->GetActorLocation();
		if (Position.X == Location.X && Position.Y == Location.Y)
			return Candidate;
	}
	return nullptr;
}

void ReleaseDeposit(ADepositSite* Deposit, const ACommandBuilding* Building)
{
	if (IsValid(Deposit) && Deposit->Extractor == Building)
	{
		Deposit->Extractor = nullptr;
		Deposit->ForceNetUpdate();
	}
}

// Deferred spawn on the navmesh ground under Location; the deposit is reserved before BeginPlay so concurrent
// construction cannot claim it. Null with OutReason on failure.
ACommandBuilding* SpawnConstruction(UWorld* World, int32 BuildingIndex, int32 Team, ACommandPlayerState* Commander,
	ADepositSite* Deposit, const FVector& Location, FString& OutReason)
{
	FNavLocation Ground;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Navigation || !Navigation->ProjectPointToNavigation(Location, Ground, FVector(45.f, 45.f, 200.f)))
	{
		OutReason = TEXT("Navigation unavailable");
		return nullptr;
	}
	// Navigation supplies height only: keep the resolved grid or deposit XY exact.
	Ground.Location.X = Location.X;
	Ground.Location.Y = Location.Y;
	const FTransform Transform(Ground.Location + FVector(0.f, 0.f, 65.f));
	ACommandBuilding* Building = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(),
		Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Building)
	{
		OutReason = TEXT("Building spawn failed");
		return nullptr;
	}
	Building->BuildingIndex = BuildingIndex;
	Building->TeamIndex = Team;
	Building->OwningPlayerState = Commander;
	Building->Deposit = Deposit;
	if (Deposit)
	{
		Deposit->Extractor = Building;
		Deposit->ForceNetUpdate();
	}
	Building->Health = Building->MaxHealth();
	Building->FinishSpawning(Transform);
	if (!IsValid(Building) || !Building->IsAlive())
	{
		ReleaseDeposit(Deposit, Building);
		if (IsValid(Building))
			Building->Destroy();
		OutReason = TEXT("Building spawn failed");
		return nullptr;
	}
	return Building;
}
}

ACommandBuilding* ACommandGameState::ApplyPlacement(int32 BuildingIndex, const FVector& RequestedLocation,
	ACommandPlayerState* Commander, int32 Team, FString& OutReason)
{
	OutReason.Reset();
	if (!HasAuthority())
	{
		OutReason = TEXT("Server authority required");
		return nullptr;
	}
	if (!ValidateBuildingPlacement(BuildingIndex, Team, RequestedLocation, OutReason))
		return nullptr;
	const FVector Location = ResolveBuildingLocation(BuildingIndex, RequestedLocation, Team);
	const UBuildingDefinition& Definition = *Content->Building(BuildingIndex);
	if (!IsBuildingOwner(*this, Commander, Team))
	{
		OutReason = TEXT("Invalid building owner");
		return nullptr;
	}
	const int32 Cost = ACommandBuilding::GetBuildCost(Definition);
	if (Commander->Resources < Cost)
	{
		OutReason = TEXT("Insufficient resources");
		return nullptr;
	}
	ADepositSite* Deposit = Definition.bRequiresDeposit ? FindFreeDepositAt(*this, Location) : nullptr;
	if (Definition.bRequiresDeposit && !Deposit)
	{
		OutReason = TEXT("Deposit unavailable");
		return nullptr;
	}
	ACommandBuilding* Building = SpawnConstruction(GetWorld(), BuildingIndex, Team, Commander, Deposit, Location, OutReason);
	if (!Building)
		return nullptr;
	if (!Commander->TrySpend(Cost))
	{
		ReleaseDeposit(Deposit, Building);
		Building->Destroy();
		OutReason = TEXT("Insufficient resources");
		return nullptr;
	}
	Building->NotifyPlacementCommitted();
	OutReason = TEXT("Construction started");
	return Building;
}

bool ACommandBuilding::ApplyCancellation()
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || IsActorBeingDestroyed() || !IsAlive() || IsComplete() || !State || State->MatchResult != EMatchResult::Ongoing)
		return false;
	const UBuildingDefinition* Definition = GetDefinition();
	const int32 Refund = Definition ? EconomyPolicy::CancellationRefund(GetBuildCost(*Definition), ConstructionProgress) : 0;
	if (IsValid(OwningPlayerState))
		OwningPlayerState->AddResources(Refund);
	ReleaseDeposit();
	FCommandBuildingTerminalSnapshot Snapshot;
	Snapshot.bCancelled = true;
	Snapshot.ConstructionProgress = ConstructionProgress;
	Snapshot.TeamIndex = TeamIndex;
	MulticastTerminalState(Snapshot);
	Destroy();
	return true;
}

bool ACommandBuilding::ApplyResearch(EArmyDoctrine Choice)
{
	const UBuildingDefinition* Definition = GetDefinition();
	if (!Definition || !Definition->bOffersResearch || !IsComplete()
		|| (Choice != EArmyDoctrine::SiegeOptics && Choice != EArmyDoctrine::FieldRepairs
			&& Choice != EArmyDoctrine::EntrenchedFrontline))
		return false;
	if (!IsValid(OwningPlayerState) || OwningPlayerState->Doctrine != EArmyDoctrine::None)
		return false;
	if (!TrySpend(ResearchCost))
		return false;
	if (!OwningPlayerState->TryChooseDoctrine(Choice))
	{
		OwningPlayerState->AddResources(ResearchCost);
		return false;
	}
	++ResearchCount;
	OnRep_ResearchCount();
	ForceNetUpdate();
	return true;
}
