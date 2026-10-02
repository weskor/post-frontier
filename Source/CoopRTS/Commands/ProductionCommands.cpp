#include "CommandService.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "ArenaBounds.h"
#include "Content/MatchContent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Engine/Level.h"

namespace
{
int32 NextArmyIndex(const UWorld& World)
{
	int32 Index = 0;
	for (const ULevel* Level : World.GetLevels())
	{
		if (!Level)
			continue;
		for (const AActor* Actor : Level->Actors)
			if (const AArmyGroup* Group = Cast<AArmyGroup>(Actor); IsValid(Group))
				Index = FMath::Max(Index, Group->GetArmyIndex() + 1);
	}
	return Index;
}
}

bool ACommandBuilding::ApplyProduction(int32 UnitIndex, bool bEnabled)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const UArmyUnitDefinition* Definition = State && State->Content ? State->Content->Unit(UnitIndex) : nullptr;
	const int32 Balance = OwningPlayerState->Resources;
	if (!HasAuthority() || IsActorBeingDestroyed() || !State || State->MatchResult != EMatchResult::Ongoing
		|| !IsProducer() || !IsAlive() || !IsComplete() || !Definition || GetForceCapacity(*Definition) == 0
		|| (bForceConfigured && ProductionUnitIndex != UnitIndex))
		return false;
	if (bEnabled && !bForceConfigured)
	{
		const int32 ConfigurationCost = GetConfigurationCost(*Definition);
		FVector Assembly;
		int32 ExitCursor = 0;
		if (Balance < ConfigurationCost || !FindProductionExit(Assembly, ExitCursor))
			return false;
		const FTransform Transform(FRotator::ZeroRotator, Assembly);
		AActor* ControllerOwner = TeamIndex == 0 ? OwningPlayerState->GetOwner() : nullptr;
		AArmyGroup* Group = GetWorld()->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
			ControllerOwner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Group)
			return false;
		Group->Initialize({ TeamIndex, OwningPlayerState.Get(), NextArmyIndex(*GetWorld()), this, Assembly });
		Group->FinishSpawning(Transform);
		bool bAcceptedFront = false;
		if (IsValid(Group))
		{
			do
			{
				Group->SetAssemblyLocation(Assembly);
				bAcceptedFront = Group->AssignFront(FrontOrder, bHasConfiguredFront ? FrontLocation : Assembly);
			}
			while (!bAcceptedFront && FindProductionExit(Assembly, ExitCursor));
		}
		if (!bAcceptedFront || (ConfigurationCost > 0 && !TrySpend(ConfigurationCost)))
		{
			if (IsValid(Group))
				Group->Destroy();
			return false;
		}
		Group->SetActorLocation(Assembly);
		ForceGroup = Group;
		bForceConfigured = true;
	}
	else if (bEnabled && (!IsValid(ForceGroup) || ForceGroup->IsActorBeingDestroyed()))
		return false;
	if (ProductionUnitIndex != UnitIndex)
		ProductionProgressSeconds = 0.f;
	ProductionUnitIndex = UnitIndex;
	ProductionRole = Definition->Role;
	bProductionEnabled = bEnabled;
	ProductionCheckAccumulator = 0.f;
	ForceNetUpdate();
	return true;
}

bool ACommandBuilding::ApplyFront(EFrontOrder Order, const FVector& Location)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || IsActorBeingDestroyed() || !State || State->MatchResult != EMatchResult::Ongoing
		|| !IsProducer() || !IsAlive() || !IsComplete()
		|| (Order != EFrontOrder::Secure && Order != EFrontOrder::Defend && Order != EFrontOrder::FallBack)
		|| !AArenaBounds::IsTravelLocation(GetWorld(), Location))
		return false;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (!Navigation || !Navigation->ProjectPointToNavigation(Location, Projected, FVector(75.f, 75.f, 200.f))
		|| !AArenaBounds::IsTravelLocation(GetWorld(), Projected.Location)
		|| FVector::DistSquared2D(Location, Projected.Location) > FMath::Square(75.f)
		|| FMath::Abs(Location.Z - Projected.Location.Z) > 110.f)
		return false;
	if (IsValid(ForceGroup) && !ForceGroup->AssignFront(Order, Projected.Location))
		return false;
	FrontOrder = Order;
	FrontLocation = Projected.Location;
	bHasConfiguredFront = true;
	// Internal AI/fixture fronts remain authoritative until a player submits a new region goal.
	GoalDriver.bEnabled = false;
	ForceNetUpdate();
	return true;
}
