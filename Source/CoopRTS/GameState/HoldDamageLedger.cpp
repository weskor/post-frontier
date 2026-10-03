#include "GameState/HoldDamageLedger.h"

#include "ArmyUnit.h"
#include "CombatTarget.h"
#include "CommandGameState.h"
#include "GameState/GameStateRegistry.h"
#include "MapRegion.h"
#include "Rules/HoldPolicy.h"

void FHoldDamageLedger::Record(AArmyUnit* Attacker, AActor* Victim, int32 VictimTeam, int32 RegionIndex, double Expires)
{
	for (FHoldDamageSource& Source : Sources)
		if (Source.Attacker == Attacker && Source.Team == VictimTeam && Source.Region == RegionIndex)
		{
			Source.Victim = Victim;
			Source.Expires = Expires;
			return;
		}
	Sources.Add({ Attacker, Victim, VictimTeam, RegionIndex, Expires });
}

void FHoldDamageLedger::Prune(double Now)
{
	Sources.RemoveAllSwap([Now](const FHoldDamageSource& Source) {
		return !Source.Attacker.IsValid() || !Source.Attacker->IsAlive() || Source.Expires <= Now;
	},
		EAllowShrinking::No);
}

bool FHoldDamageLedger::IsDamaging(const ACommandGameState& State, const AArmyUnit& Attacker, int32 RegionIndex,
	int32 DefendingTeam, double Now) const
{
	for (const FHoldDamageSource& Source : Sources)
	{
		if (Source.Attacker.Get() != &Attacker || Source.Team != DefendingTeam || Source.Region != RegionIndex || !Source.Victim.IsValid())
			continue;
		const AMapRegion* VictimRegion = GameStateRegistry::FindRegionAt(State, Source.Victim->GetActorLocation());
		if (VictimRegion && VictimRegion->RegionIndex == RegionIndex
			&& HoldPolicy::IsDamageCurrent(Now, Source.Expires,
				CombatTarget::IsAliveHostile(Source.Victim.Get(), Attacker.GetTeamIndex()),
				FVector::DistSquared2D(Attacker.GetActorLocation(), Source.Victim->GetActorLocation()) <= FMath::Square(Attacker.WeaponRange())))
			return true;
	}
	return false;
}

AActor* FHoldDamageLedger::FindVictim(const AArmyUnit* Attacker, int32 RegionIndex, int32 Team) const
{
	for (const FHoldDamageSource& Source : Sources)
		if (Source.Attacker == Attacker && Source.Region == RegionIndex && Source.Team == Team && Source.Victim.IsValid())
			return Source.Victim.Get();
	return nullptr;
}
