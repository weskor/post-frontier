#include "EconomyPolicy.h"

double EconomyPolicy::JevPlayerCountFactor(int32 HumanCommanders)
{
	return 1. + 0.3 * (FMath::Max(1, HumanCommanders) - 1);
}

FExtractorPayment EconomyPolicy::ExtractorPayment(const FExtractorPaymentInput& In)
{
	if (!In.bAlive || !In.bComplete || In.OwnerTeam != In.RecipientTeam
		|| In.OwnerCommander != In.RecipientCommander || (In.OwnerTeam != 0 && In.OwnerTeam != 5)
		|| (In.OwnerTeam == 0 && (In.OwnerCommander < 0 || In.OwnerCommander >= 5))
		|| (In.OwnerTeam == 5 && In.OwnerCommander != -1)
		|| In.RatePerSecond <= 0 || In.Remaining <= 0 || In.TickSeconds <= 0)
		return { 0, In.Remaining };
	const int32 Amount = static_cast<int32>(FMath::Min<int64>(In.Remaining,
		static_cast<int64>(In.RatePerSecond) * In.TickSeconds));
	return { Amount, In.Remaining - Amount };
}

int32 EconomyPolicy::AddResources(int32 Balance, int32 Amount)
{
	return Amount > 0 ? static_cast<int32>(FMath::Min<int64>(MAX_int32, static_cast<int64>(Balance) + Amount)) : Balance;
}

int32 EconomyPolicy::CancellationRefund(int32 Cost, float Progress)
{
	return FMath::FloorToInt(Cost * (1.f - Progress));
}

bool EconomyPolicy::CanAfford(int32 Balance, int32 Cost)
{
	return Cost > 0 && Balance >= Cost;
}
