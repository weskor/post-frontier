#include "ArenaBounds.h"

#include "CommandGameState.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AArenaBounds::AArenaBounds()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ArenaRoot")));
}

bool AArenaBounds::ContainsTravel(const FVector& Location) const
{
	return !Location.ContainsNaN() && FMath::Abs(Location.X) <= HalfExtent.X
		&& FMath::Abs(Location.Y) <= HalfExtent.Y && FMath::Abs(Location.Z) <= HalfHeight;
}

bool AArenaBounds::ContainsPlacement(const FVector& Location) const
{
	return !Location.ContainsNaN() && FMath::Abs(Location.X) <= HalfExtent.X - PlacementMargin
		&& FMath::Abs(Location.Y) <= HalfExtent.Y - PlacementMargin && FMath::Abs(Location.Z) <= HalfHeight;
}

const AArenaBounds* AArenaBounds::Find(const UWorld* World)
{
	const ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
	return State && IsValid(State->Arena) ? State->Arena.Get() : nullptr;
}

void AArenaBounds::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArenaBounds, HalfExtent);
	DOREPLIFETIME(AArenaBounds, PlacementMargin);
}
