#include "TargetingPolicy.h"

void FTargetSelection::Consider(EDamageType Type, int32 CandidateIndex, EArmorClass CandidateArmor, double CandidateDistanceSquared)
{
	const bool bCandidatePreferred = CombatPolicy::IsStrongAgainst(Type, CandidateArmor);
	const bool bCurrentPreferred = CombatPolicy::IsStrongAgainst(Type, Armor);
	if (Index == INDEX_NONE || (bCandidatePreferred && !bCurrentPreferred)
		|| (bCandidatePreferred == bCurrentPreferred && CandidateDistanceSquared < DistanceSquared))
	{
		Index = CandidateIndex;
		Armor = CandidateArmor;
		DistanceSquared = CandidateDistanceSquared;
	}
}
