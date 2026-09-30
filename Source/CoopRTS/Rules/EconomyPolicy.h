#pragma once

#include "CoreMinimal.h"

namespace EconomyPolicy
{
	// Income uses the match's existing integer rates and whole-second payment interval.
	int32 IncomePerTick(int32 Baseline, int32 PerSite, int32 Sites, int32 TickSeconds);
	// Scale only the enemy baseline by the human roster, with a one-commander minimum.
	int32 EnemyIncomePerSecond(int32 Baseline, int32 PerSite, int32 Sites, int32 HumanCommanders);
	// Positive credits saturate at MAX_int32; non-positive credits leave the wallet unchanged.
	int32 AddResources(int32 Balance, int32 Amount);
	// Preserve raw construction progress and floor the unspent cost; no extra clamping.
	int32 CancellationRefund(int32 Cost, float Progress);
	// Spending requires a positive price and includes the exact-balance boundary.
	bool CanAfford(int32 Balance, int32 Cost);
}
