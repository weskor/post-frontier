#include "AnnouncerPolicy.h"

namespace AnnouncerPolicy
{
namespace
{
const FDefinition Entries[] = {
	{ TEXT("own_hq_under_attack"), TEXT("Hardline is taking fire."), false, true },
	{ TEXT("own_hq_half"), TEXT("Hardline at half strength."), true, true },
	{ TEXT("own_hq_critical"), TEXT("Hardline critical. Twenty-five percent remaining."), true, true },
	{ TEXT("own_hq_offline"), TEXT("Hardline is offline."), true, true },
	{ TEXT("enemy_hq_under_attack"), TEXT("The Lattice is taking fire."), false, true },
	{ TEXT("enemy_hq_half"), TEXT("The Lattice at half strength."), true, true },
	{ TEXT("enemy_hq_critical"), TEXT("The Lattice critical. Twenty-five percent remaining."), true, true },
	{ TEXT("enemy_hq_offline"), TEXT("The Lattice is offline."), true, true },
	{ TEXT("region_captured"), TEXT("Region secured."), true, false },
	{ TEXT("region_lost"), TEXT("Region lost."), true, false },
	{ TEXT("drill_rig_lost"), TEXT("Drill Rig lost."), true, false },
	{ TEXT("ping_look_here"), TEXT("Look here."), true, false, true },
	{ TEXT("ping_need_help"), TEXT("Need help here."), true, false, true },
	{ TEXT("region_defenders_responding"), TEXT("Region defenders responding."), true, false, false },
	{ TEXT("fortify_cast"), TEXT("Fortify active."), true, false, false },
	{ TEXT("own_node_lost"), TEXT("Hardline Failover Node lost."), true, false, false },
	{ TEXT("enemy_node_lost"), TEXT("The Lattice Failover Node lost."), true, false, false },
	{ TEXT("own_emergency"), TEXT("Hardline HQ offline. Emergency forces deployed."), true, false, false },
	{ TEXT("enemy_emergency"), TEXT("The Lattice HQ offline. Emergency forces deployed."), true, false, false },
	{ TEXT("own_hq_online"), TEXT("Hardline HQ back online."), true, false, false },
	{ TEXT("enemy_hq_online"), TEXT("The Lattice HQ back online."), true, false, false },
	{ TEXT("split_brain_cut"), TEXT("Split-Brain Cut in thirty seconds. Hold your supply necks."), true, false, false }
};
}

TConstArrayView<FDefinition> Definitions()
{
	return Entries;
}

const FDefinition* Find(FName Id)
{
	static const struct FNames
	{
		FName Values[UE_ARRAY_COUNT(Entries)];
		FNames()
		{
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Entries); ++Index)
				Values[Index] = FName(Entries[Index].Id);
		}
	} Names;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Entries); ++Index)
		if (Id == Names.Values[Index])
			return &Entries[Index];
	return nullptr;
}

bool FThrottle::Accept(FName Id, uint32 StructureId, int32 CommanderIndex, int32 ForceNumber, double Now)
{
	const FDefinition* Definition = Find(Id);
	if (!Definition)
		return false;
	if (!Definition->bDamage)
		return true;
	FAttackEpisode* Episode = AttackEpisodes.FindByPredicate([StructureId](const FAttackEpisode& Candidate) {
		return Candidate.StructureId == StructureId;
	});
	if (!Episode)
	{
		Episode = &AttackEpisodes.AddDefaulted_GetRef();
		Episode->StructureId = StructureId;
	}
	else if (Now - Episode->LastDamage >= RepeatSeconds)
		Episode->AnnouncedForces.Reset();
	// Suppressed damage still keeps the siege episode alive.
	Episode->LastDamage = Now;
	const bool bNewForce = !Episode->AnnouncedForces.ContainsByPredicate([CommanderIndex, ForceNumber](const FAttackingForce& Force) {
		return Force.CommanderIndex == CommanderIndex && Force.ForceNumber == ForceNumber;
	});
	if (bNewForce)
		Episode->AnnouncedForces.Add({ CommanderIndex, ForceNumber });
	return Definition->bStateChange || bNewForce;
}
}
