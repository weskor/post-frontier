#include "AnnouncerPolicy.h"

#include "Containers/StaticArray.h"

namespace AnnouncerPolicy
{
namespace
{
const FDefinition Entries[] = {
	{ TEXT("own_hq_under_attack"), TEXT("Hardline is taking fire."), false },
	{ TEXT("own_hq_half"), TEXT("Hardline at half strength."), true },
	{ TEXT("own_hq_critical"), TEXT("Hardline critical. Twenty-five percent remaining."), true },
	{ TEXT("own_hq_offline"), TEXT("Hardline is offline."), true },
	{ TEXT("enemy_hq_under_attack"), TEXT("The Lattice is taking fire."), false },
	{ TEXT("enemy_hq_half"), TEXT("The Lattice at half strength."), true },
	{ TEXT("enemy_hq_critical"), TEXT("The Lattice critical. Twenty-five percent remaining."), true },
	{ TEXT("enemy_hq_offline"), TEXT("The Lattice is offline."), true },
	{ TEXT("region_captured"), TEXT("Region secured."), true },
	{ TEXT("region_lost"), TEXT("Region lost."), true },
	{ TEXT("drill_rig_lost"), TEXT("Drill Rig lost."), true }
};
}

TConstArrayView<FDefinition> Definitions()
{
	return Entries;
}

const FDefinition* Find(FName Id)
{
	static const TStaticArray<FName, UE_ARRAY_COUNT(Entries)> Names = []
	{
		TStaticArray<FName, UE_ARRAY_COUNT(Entries)> Result;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Entries); ++Index)
			Result[Index] = FName(Entries[Index].Id);
		return Result;
	}();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Entries); ++Index)
		if (Id == Names[Index])
			return &Entries[Index];
	return nullptr;
}

bool FThrottle::Accept(FName Id, uint32 StructureId, int32 CommanderIndex, int32 ForceNumber, int32 DamageTier, double Now)
{
	const FDefinition* Definition = Find(Id);
	if (!Definition)
		return false;
	if (Definition->bStateChange)
		return true;
	FAttackEpisode* Episode = AttackEpisodes.FindByPredicate([StructureId](const FAttackEpisode& Candidate)
	{
		return Candidate.StructureId == StructureId;
	});
	if (!Episode)
	{
		Episode = &AttackEpisodes.AddDefaulted_GetRef();
		Episode->StructureId = StructureId;
	}
	else if (Now - Episode->LastDamage >= RepeatSeconds)
	{
		Episode->AnnouncedForces.Reset();
		Episode->AnnouncedTiers.Reset();
	}
	// Suppressed damage still keeps the siege episode alive.
	Episode->LastDamage = Now;
	const bool bNewForce = !Episode->AnnouncedForces.ContainsByPredicate([CommanderIndex, ForceNumber](const FAttackingForce& Force)
	{
		return Force.CommanderIndex == CommanderIndex && Force.ForceNumber == ForceNumber;
	});
	const bool bNewTier = !Episode->AnnouncedTiers.Contains(DamageTier);
	if (bNewForce)
		Episode->AnnouncedForces.Add({ CommanderIndex, ForceNumber });
	if (bNewTier)
		Episode->AnnouncedTiers.Add(DamageTier);
	return bNewForce || bNewTier;
}
}
