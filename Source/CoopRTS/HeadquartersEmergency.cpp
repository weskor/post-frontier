#include "Headquarters.h"

#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
#include "Content/MatchContent.h"
#include "Content/UnitDefinition.h"
#include "EnemyCommander.h"
#include "EngineUtils.h"
#include "GameState/GameStateEconomy.h"

// The emergency force (decision H3): the first time a side's HQ goes offline, a free force spawns at
// that HQ. Humans get one per commander of that commander's Barracks unit type at full squad; JEV gets one
// wave at its current release budget (AEnemyCommander::RequestEmergencyWave).

namespace
{
// Emergency forces assemble this far from the HQ toward the opposing HQ, side by side.
constexpr float AssemblyDistance = 900.f;
constexpr float AssemblySpacing = 700.f;
// Producers hold the low force numbers; free forces take the next unused ones.
constexpr int32 FirstFreeForceNumber = 4;

// The unit type of the commander's Barracks: the lowest-numbered configured, living producer.
int32 BarracksUnit(const ACommandGameState& State, const ACommandPlayerState& Commander)
{
	const ACommandBuilding* Chosen = nullptr;
	for (const ACommandBuilding* Building : State.Buildings)
		if (IsValid(Building) && Building->OwningPlayerState == &Commander && Building->IsAlive() && Building->IsProducer()
			&& Building->bForceConfigured && Building->ProductionUnitIndex >= 0
			&& (!Chosen || Building->ForceNumber < Chosen->ForceNumber))
			Chosen = Building;
	return Chosen ? Chosen->ProductionUnitIndex : State.Content->UnitIndexForRole(EUnitRole::Frontline);
}

int32 FreeForceNumber(const ACommandGameState& State, const ACommandPlayerState& Commander)
{
	for (int32 Number = FirstFreeForceNumber;; ++Number)
	{
		bool bUsed = State.Buildings.ContainsByPredicate([&](const ACommandBuilding* Building) {
			return IsValid(Building) && Building->OwningPlayerState == &Commander && Building->ForceNumber == Number;
		});
		for (TActorIterator<AArmyGroup> It(State.GetWorld()); It && !bUsed; ++It)
			bUsed = It->GetOwningPlayerState() == &Commander && It->ForceNumber == Number && It->GetAliveCount() > 0;
		if (!bUsed)
			return Number;
	}
}
}

void AHeadquarters::DeployEmergencyForces()
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	// Only a match's own HQs deploy: the emergency belongs to the side, not to any actor of this class.
	if (!HasAuthority() || !State || !State->Content || (State->FriendlyHeadquarters != this && State->EnemyHeadquarters != this))
		return;
	Announce(TEXT("own_emergency"), TEXT("enemy_emergency"), 0);
	if (TeamIndex != 5)
	{
		DeployHumanEmergencyForces(*State);
		return;
	}
	for (TActorIterator<AEnemyCommander> It(GetWorld()); It; ++It)
		if (It->TeamIndex == 5)
		{
			It->RequestEmergencyWave();
			return;
		}
}

void AHeadquarters::DeployHumanEmergencyForces(ACommandGameState& State)
{
	const AHeadquarters* Opposing = State.EnemyHeadquarters;
	const FVector Forward = IsValid(Opposing) ? (Opposing->GetActorLocation() - GetActorLocation()).GetSafeNormal2D() : FVector::ForwardVector;
	const FVector Side(-Forward.Y, Forward.X, 0.f);
	const TArray<ACommandPlayerState*> Roster = FGameStateEconomy::Roster(State);
	for (int32 Slot = 0; Slot < Roster.Num(); ++Slot)
	{
		ACommandPlayerState* Commander = Roster[Slot];
		const int32 UnitIndex = BarracksUnit(State, *Commander);
		const UArmyUnitDefinition* Unit = State.Content->Unit(UnitIndex);
		if (!Unit || Unit->Capacity <= 0)
			continue;
		TArray<int32, TInlineAllocator<6>> Squad;
		Squad.Init(UnitIndex, Unit->Capacity);
		const FVector Anchor = GetActorLocation() + Forward * AssemblyDistance + Side * ((Slot - (Roster.Num() - 1) * .5f) * AssemblySpacing);
		AArmyGroup* Force = AArmyGroup::SpawnFreeForce(*GetWorld(), *Commander, Anchor, Squad, FreeForceNumber(State, *Commander), 1.f);
		// A free force has no producer to refill it: it fights to the end.
		if (Force)
			FCommandService::SetRetreatThreshold(Commander, Force, ERetreatThreshold::Never);
	}
}
