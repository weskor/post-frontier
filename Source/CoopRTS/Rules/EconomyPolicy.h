#pragma once

#include "CoreMinimal.h"

struct FExtractorPaymentInput
{
	int32 RatePerSecond, Remaining, TickSeconds;
	int32 OwnerTeam, OwnerCommander, RecipientTeam, RecipientCommander;
	bool bAlive, bComplete;
};

struct FExtractorPayment
{
	int32 Amount, Remaining;
};

namespace EconomyPolicy
{
// Counts below one use solo scaling; each additional human commander adds 30%.
double JevPlayerCountFactor(int32 HumanCommanders);
// Extraction consumes only what the living, completed owner's wallet is paid.
FExtractorPayment ExtractorPayment(const FExtractorPaymentInput& In);
// Positive credits saturate at MAX_int32; non-positive credits leave the wallet unchanged.
int32 AddResources(int32 Balance, int32 Amount);
// Preserve raw construction progress and floor the unspent cost; no extra clamping.
int32 CancellationRefund(int32 Cost, float Progress);
// Spending requires a positive price and includes the exact-balance boundary.
bool CanAfford(int32 Balance, int32 Cost);
}
