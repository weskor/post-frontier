#pragma once

#include "CoreMinimal.h"
#include "CombatPolicy.h"

// Streaming selection over eligible candidates; no actor references or allocation.
// Equal-ranked, equal-distance candidates retain the first considered target.
struct FTargetSelection
{
	int32 Index = INDEX_NONE;
	EArmorClass Armor = EArmorClass::Light;
	double DistanceSquared = 0.;

	void Consider(EDamageType Type, int32 CandidateIndex, EArmorClass CandidateArmor, double CandidateDistanceSquared);
};
