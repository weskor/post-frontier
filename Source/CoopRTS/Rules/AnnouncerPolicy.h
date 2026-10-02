#pragma once

#include "CoreMinimal.h"

namespace AnnouncerPolicy
{
struct FDefinition
{
	const TCHAR* Id;
	const TCHAR* Text;
	bool bStateChange;
};
TConstArrayView<FDefinition> Definitions();
const FDefinition* Find(FName Id);
constexpr double RepeatSeconds = 20.;

class FThrottle
{
public:
	bool Accept(FName Id, uint32 StructureId, int32 CommanderIndex, int32 ForceNumber, int32 DamageTier, double Now);
private:
	struct FAttackingForce
	{
		int32 CommanderIndex;
		int32 ForceNumber;
	};
	struct FAttackEpisode
	{
		uint32 StructureId;
		double LastDamage = 0.;
		TArray<FAttackingForce, TInlineAllocator<8>> AnnouncedForces;
		TArray<int32, TInlineAllocator<3>> AnnouncedTiers;
	};
	TArray<FAttackEpisode> AttackEpisodes;
};
}
