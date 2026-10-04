#include "ArmyUnit.h"

#include "CommandGameState.h"
#include "Engine/World.h"
#include "MapRegion.h"

namespace
{
// A unit re-reads its region on this clock; hits, range checks and speed read the cache.
constexpr float RegionRefreshSeconds = .25f;
}

void AArmyUnit::RefreshRegion()
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!State)
		return;
	const FVector Location = GetActorLocation();
	const AMapRegion* Region = CurrentRegion.Get();
	if (!Region || !Region->Contains(Location))
		CurrentRegion = State->FindRegionAt(Location);
}

ERegionTrait AArmyUnit::GetRegionTrait() const
{
	const AMapRegion* Region = CurrentRegion.Get();
	return Region ? Region->GetTrait() : ERegionTrait::None;
}

void AArmyUnit::TickRegion(float DeltaSeconds)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!IsAlive() || !State || State->MatchResult != EMatchResult::Ongoing)
		return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextRegionRefreshTime)
	{
		NextRegionRefreshTime = Now + RegionRefreshSeconds;
		RefreshRegion();
	}
	const int32 Ticks = RegionTraitPolicy::AdvanceHazard(GetRegionTrait(), HazardInsideSeconds, DeltaSeconds);
	for (int32 Tick = 0; Tick < Ticks && IsAlive(); ++Tick)
		ReceiveEnvironmentalDamage(RegionTraitPolicy::HazardDamagePerTick);
}
