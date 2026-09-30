#include "CommandBuilding.h"

#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Content/BuildingDefinition.h"
#include "Content/MatchContent.h"
#include "Content/UnitDefinition.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "NavigationSystem.h"

namespace
{
	bool IsArenaLocation(const UWorld* World, const FVector& Location)
	{
		const AArenaBounds* Arena = AArenaBounds::Find(World);
		return Arena && Arena->ContainsTravel(Location);
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

	FProductionInput MakeProductionInput(const ACommandBuilding& Building, const ACommandGameState* State, float DeltaSeconds)
	{
		FProductionInput In{};
		In.bMatchOngoing = State && State->MatchResult == EMatchResult::Ongoing;
		In.bProducer = Building.IsProducer();
		In.bComplete = Building.IsComplete();
		In.bAlive = Building.IsAlive();
		In.bConfigured = Building.bForceConfigured;
		In.bForceValid = IsValid(Building.ForceGroup);
		In.bEnabled = Building.bProductionEnabled;
		Building.GetForceCounts(In.Joined, In.Travelling);
		const UArmyUnitDefinition* Unit = Building.GetProductionDefinition();
		In.Capacity = Unit ? ACommandBuilding::GetForceCapacity(*Unit) : 0;
		In.bWalletValid = State && GetBalance(Building, *State, In.Balance);
		In.UnitCost = Building.GetProductionCost();
		In.Progress = Building.ProductionProgressSeconds;
		In.Duration = Building.GetProductionDuration();
		In.DeltaSeconds = DeltaSeconds;
		return In;
	}

	bool FindExit(const ACommandBuilding& Building, FVector& OutLocation, int32& Cursor)
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Building.GetWorld());
		const UBuildingDefinition* Definition = Building.GetDefinition();
		if (!Navigation || !Definition) return false;
		const float Footprint = ACommandBuilding::GetFootprintRadius(*Definition);
		static const FVector Directions[] = {
			{1, 0, 0}, {.707107, .707107, 0}, {0, 1, 0}, {-.707107, .707107, 0},
			{-1, 0, 0}, {-.707107, -.707107, 0}, {0, -1, 0}, {.707107, -.707107, 0}
		};
		const float Radius = Footprint + 240.f;
		while (Cursor < 24)
		{
			const int32 Candidate = Cursor++;
			const FVector Desired = Building.GetActorLocation()
				+ Directions[Candidate % 8] * (Radius + (Candidate / 8) * 145.f);
			FNavLocation Projected;
			if (!IsArenaLocation(Building.GetWorld(), Desired)
				|| !Navigation->ProjectPointToNavigation(Desired, Projected, FVector(45.f, 45.f, 200.f))
				|| !IsArenaLocation(Building.GetWorld(), Projected.Location)
				|| FVector::DistSquared2D(Desired, Projected.Location) > FMath::Square(45.f)
				|| FMath::Abs(Desired.Z - Projected.Location.Z) > 110.f
				|| FVector::DistSquared2D(Projected.Location, Building.GetActorLocation()) < FMath::Square(Footprint + 75.f)
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

bool ACommandBuilding::SetProduction(int32 UnitIndex, bool bEnabled)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const UArmyUnitDefinition* Definition = State && State->Content ? State->Content->Unit(UnitIndex) : nullptr;
	int32 Balance = 0;
	if (!HasAuthority() || IsActorBeingDestroyed() || !State || State->MatchResult != EMatchResult::Ongoing
		|| !IsProducer() || !IsAlive() || !IsComplete() || !Definition || GetForceCapacity(*Definition) == 0
		|| !GetBalance(*this, *State, Balance) || (bForceConfigured && ProductionUnitIndex != UnitIndex)) return false;
	if (bEnabled && !bForceConfigured)
	{
		const int32 ConfigurationCost = GetConfigurationCost(*Definition);
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
	if (ProductionUnitIndex != UnitIndex) ProductionProgressSeconds = 0.f;
	ProductionUnitIndex = UnitIndex;
	ProductionRole = Definition->Role;
	bProductionEnabled = bEnabled;
	ProductionCheckAccumulator = 0.f;
	ForceNetUpdate();
	return true;
}

bool ACommandBuilding::SetFront(EFrontOrder Order, const FVector& Location)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || IsActorBeingDestroyed() || !State || State->MatchResult != EMatchResult::Ongoing
		|| !IsProducer() || !IsAlive() || !IsComplete()
		|| (Order != EFrontOrder::Secure && Order != EFrontOrder::Defend && Order != EFrontOrder::FallBack)
		|| !IsArenaLocation(GetWorld(), Location)) return false;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (!Navigation || !Navigation->ProjectPointToNavigation(Location, Projected, FVector(75.f, 75.f, 200.f))
		|| !IsArenaLocation(GetWorld(), Projected.Location)
		|| FVector::DistSquared2D(Location, Projected.Location) > FMath::Square(75.f)
		|| FMath::Abs(Location.Z - Projected.Location.Z) > 110.f) return false;
	if (IsValid(ForceGroup) && !ForceGroup->AssignFront(Order, Projected.Location)) return false;
	FrontOrder = Order;
	FrontLocation = Projected.Location;
	bHasConfiguredFront = true;
	ForceNetUpdate();
	return true;
}

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

EProductionState ACommandBuilding::GetProductionState() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return ProductionPolicy::Evaluate(MakeProductionInput(*this, State, 0.f)).State;
}

void ACommandBuilding::TickProduction(float DeltaSeconds)
{
	if (!HasAuthority() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f) return;
	FProductionInput In = MakeProductionInput(*this, GetWorld()->GetGameState<ACommandGameState>(), DeltaSeconds);
	In.bForceValid = In.bForceValid && !ForceGroup->IsActorBeingDestroyed();
	const FProductionDecision Decision = ProductionPolicy::Evaluate(In);
	if (Decision.State != EProductionState::Producing && Decision.State != EProductionState::DeploymentBlocked)
	{
		ProductionCheckAccumulator = 0.f;
		return;
	}
	ProductionCheckAccumulator += DeltaSeconds;
	ProductionProgressSeconds = Decision.NewProgress;
	if (!Decision.bDeploymentDue) return;
	if (ProductionCheckAccumulator < .25f) return;
	ProductionCheckAccumulator = 0.f;
	FVector Exit;
	int32 ExitCursor = 0;
	bool bDeployed = false;
	while (FindExit(*this, Exit, ExitCursor))
	{
		if (ForceGroup->SpawnReinforcement(ProductionUnitIndex, Exit)) { bDeployed = true; break; }
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
