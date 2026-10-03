#include "ForceCapState.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Rules/ForceCap.h"

CommandForceCap::FOccupancy CommandForceCap::Read(const ACommandGameState& State, const ACommandPlayerState& Commander)
{
	int32 Humans = 0;
	for (const APlayerState* Player : State.PlayerArray)
		if (const ACommandPlayerState* Human = Cast<ACommandPlayerState>(Player))
			if (IsValid(Human) && Human->TeamIndex == 0 && Human->CommanderIndex >= 0 && Human->CommanderIndex < 5)
				++Humans;
	FOccupancy Result;
	Result.Limit = Commander.TeamIndex == 0 ? ForceCap::Limit(Humans) : 0;
	for (const ACommandBuilding* Building : State.Buildings)
		if (IsValid(Building) && !Building->IsActorBeingDestroyed()
			&& ForceCap::Counts(Building->IsAlive(), Building->IsProducer(),
				Building->OwningPlayerState == &Commander && Building->TeamIndex == Commander.TeamIndex))
			++Result.Count;
	return Result;
}

FString CommandForceCap::BlockReason(const FOccupancy& Occupancy)
{
	return Occupancy.IsFull()
		? FString::Printf(TEXT("Force cap reached (%d/%d). A production building must be gone before adding another."), Occupancy.Count, Occupancy.Limit)
		: FString();
}
