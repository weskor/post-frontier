#include "ArmyUnit.h"

#include "ArmyGroup.h"
#include "CombatTarget.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
// Pulse readiness is cheap; the hostile scan only runs while ready, on this clock.
constexpr float PulseScanSeconds = .2f;
}

void AArmyUnit::TickShield(float DeltaSeconds)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!IsAlive() || MaxShield() <= 0 || !State || State->MatchResult != EMatchResult::Ongoing)
		return;
	Shield += ShieldPolicy::Regenerate(ShieldClock, Shield, MaxShield(), DeltaSeconds);
}

void AArmyUnit::TickPulse()
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!Definition || Definition->PulseInterval <= 0.f || !IsAlive() || bReinforcing || !State
		|| State->MatchResult != EMatchResult::Ongoing || (IsValid(Group) && Group->Status == EForceStatus::Retreating))
		return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!ShieldPolicy::PulseReady(Now, NextPulseReadyAt) || Now < NextPulseScanTime)
		return;
	NextPulseScanTime = Now + PulseScanSeconds;
	CastPulse(*State);
}

void AArmyUnit::CastPulse(const ACommandGameState& State)
{
	const FVector Origin = GetActorLocation();
	TArray<AArmyUnit*, TInlineAllocator<16>> Units;
	TArray<ACommandBuilding*, TInlineAllocator<8>> Buildings;
	bool bTriggered = false;
	for (AArmyUnit* Other : TActorRange<AArmyUnit>(GetWorld()))
	{
		const float Distance = FVector::Dist2D(Origin, Other->GetActorLocation());
		if (!CombatTarget::IsAliveHostile(Other, TeamIndex) || !ShieldPolicy::PulseAffects(Distance, Definition->PulseRadius))
			continue;
		Units.Add(Other);
		bTriggered |= ShieldPolicy::PulseTriggers(ShieldPolicy::EPulseSubject::Unit, Other->GetShield(), Distance, Definition->PulseRadius);
	}
	// Headquarters are never scanned, so they are never a trigger or stunned.
	for (const TObjectPtr<ACommandBuilding>& Building : State.Buildings)
	{
		const UBuildingDefinition* BuildingDefinition = Building ? Building->GetDefinition() : nullptr;
		if (!BuildingDefinition || !CombatTarget::IsAliveHostile(Building, TeamIndex))
			continue;
		// Buildings are solid: measure to the footprint edge, not the centre.
		const float Distance = FMath::Max(0.f,
			FVector::Dist2D(Origin, Building->GetActorLocation()) - ACommandBuilding::GetFootprintRadius(*BuildingDefinition));
		if (!ShieldPolicy::PulseAffects(Distance, Definition->PulseRadius))
			continue;
		Buildings.Add(Building);
		bTriggered |= ShieldPolicy::PulseTriggers(ShieldPolicy::EPulseSubject::Building, 0, Distance, Definition->PulseRadius);
	}
	if (!bTriggered)
		return;
	NextPulseReadyAt = ShieldPolicy::NextPulseReadyAt(GetWorld()->GetTimeSeconds(), Definition->PulseInterval);
	for (AArmyUnit* Other : Units)
		Other->StripShield();
	for (ACommandBuilding* Building : Buildings)
		Building->ApplyStun(Definition->PulseBuildingStunSeconds);
	ResetRepairTimer();
}
