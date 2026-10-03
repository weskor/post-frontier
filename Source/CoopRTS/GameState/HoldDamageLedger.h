#pragma once

#include "CoreMinimal.h"

class AActor;
class AArmyUnit;
class ACommandGameState;

struct FHoldDamageSource
{
	TWeakObjectPtr<AArmyUnit> Attacker;
	TWeakObjectPtr<AActor> Victim;
	int32 Team = INDEX_NONE;
	int32 Region = INDEX_NONE;
	double Expires = 0.;
};

// Units currently damaging a region's occupants. A source stays current for one weapon cycle;
// further hits renew the same entry instead of adding a second threat.
class FHoldDamageLedger
{
public:
	void Record(AArmyUnit* Attacker, AActor* Victim, int32 VictimTeam, int32 RegionIndex, double Expires);
	void Prune(double Now);
	bool IsDamaging(const ACommandGameState& State, const AArmyUnit& Attacker, int32 RegionIndex,
		int32 DefendingTeam, double Now) const;
	// Victim of the first live source that matches, or null.
	AActor* FindVictim(const AArmyUnit* Attacker, int32 RegionIndex, int32 Team) const;

private:
	TArray<FHoldDamageSource> Sources;
};
