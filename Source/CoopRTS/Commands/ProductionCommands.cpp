#include "CommandService.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Content/MatchContent.h"
#include "MapRegion.h"
#include "Engine/World.h"
#include "Engine/Level.h"

bool ACommandBuilding::ApplyProduction(int32 UnitIndex, bool bEnabled)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const UArmyUnitDefinition* Definition = State && State->Content ? State->Content->Unit(UnitIndex) : nullptr;
	if (!HasAuthority() || IsActorBeingDestroyed() || !State || State->MatchResult != EMatchResult::Ongoing
		|| !IsProducer() || !IsAlive() || !IsComplete() || !Definition || GetForceCapacity(*Definition) == 0
		|| !IsValid(OwningPlayerState) || (bForceConfigured && ProductionUnitIndex != UnitIndex))
		return false;
	const int32 Balance = OwningPlayerState->Resources;
	InitializeRallyPoint();
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
		Group->Initialize({ TeamIndex, OwningPlayerState.Get(), AArmyGroup::NextArmyIndex(*GetWorld()), this, Assembly });
		Group->FinishSpawning(Transform);
		bool bAcceptedOrder = false;
		if (IsValid(Group))
		{
			Group->ForceCapacity = Definition->Capacity;
			Group->bProducedGroup = true;
			do
			{
				Group->SetAssemblyLocation(Assembly);
				bAcceptedOrder = Group->IssueTravel(EArmyOrder::Move, State->GetRegionAnchor(RallyRegionIndex), false);
			}
			while (!bAcceptedOrder && FindProductionExit(Assembly, ExitCursor));
		}
		if (!bAcceptedOrder || (ConfigurationCost > 0 && !TrySpend(ConfigurationCost)))
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
	if (IsValid(ForceGroup))
		ForceGroup->TickOrders();
	ForceNetUpdate();
	return true;
}

void ACommandBuilding::InitializeRallyPoint()
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || !State || !IsProducer() || RallyRegionIndex != INDEX_NONE)
		return;
	if (const AMapRegion* Region = State->FindRegionAt(GetActorLocation()))
	{
		RallyRegionIndex = Region->RegionIndex;
		ForceNetUpdate();
	}
}
