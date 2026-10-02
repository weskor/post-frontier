#include "AnnouncerPolicy.h"

#include "Containers/StaticArray.h"

namespace AnnouncerPolicy
{
namespace
{
const FDefinition Entries[] = {
	{ TEXT("own_hq_under_attack"), TEXT("Our HQ is under attack"), false },
	{ TEXT("own_hq_half"), TEXT("Our HQ is at half health"), true },
	{ TEXT("own_hq_critical"), TEXT("Our HQ is critical"), true },
	{ TEXT("own_hq_offline"), TEXT("Our HQ is offline"), true },
	{ TEXT("enemy_hq_under_attack"), TEXT("Enemy HQ is under attack"), false },
	{ TEXT("enemy_hq_half"), TEXT("Enemy HQ is at half health"), true },
	{ TEXT("enemy_hq_critical"), TEXT("Enemy HQ is critical"), true },
	{ TEXT("enemy_hq_offline"), TEXT("Enemy HQ is offline"), true },
	{ TEXT("region_captured"), TEXT("Region captured"), true },
	{ TEXT("region_lost"), TEXT("Region lost"), true },
	{ TEXT("drill_rig_lost"), TEXT("Drill Rig lost"), true }
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

bool FThrottle::Accept(FName Id, int32 AffectedTeam, int32 CommanderIndex, int32 ForceNumber, int32 DamageTier, double Now)
{
	const FDefinition* Definition = Find(Id);
	if (!Definition)
		return false;
	if (Definition->bStateChange)
		return true;
	for (FLastAttack& Last : LastAttacks)
	{
		if (Last.Id != Id || Last.AffectedTeam != AffectedTeam)
			continue;
		if ((Last.CommanderIndex == CommanderIndex && Last.ForceNumber == ForceNumber && Last.DamageTier == DamageTier)
			|| Now - Last.Time < RepeatSeconds)
			return false;
		Last = { Id, AffectedTeam, CommanderIndex, ForceNumber, DamageTier, Now };
		return true;
	}
	LastAttacks.Add({ Id, AffectedTeam, CommanderIndex, ForceNumber, DamageTier, Now });
	return true;
}
}
