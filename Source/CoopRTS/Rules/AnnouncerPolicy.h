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
	bool Accept(FName Id, int32 AffectedTeam, int32 CommanderIndex, int32 ForceNumber, int32 DamageTier, double Now);
private:
	struct FLastAttack
	{
		FName Id;
		int32 AffectedTeam;
		int32 CommanderIndex;
		int32 ForceNumber;
		int32 DamageTier;
		double Time;
	};
	TArray<FLastAttack> LastAttacks;
};
}
