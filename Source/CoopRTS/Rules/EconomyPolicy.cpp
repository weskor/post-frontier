#include "EconomyPolicy.h"

int32 EconomyPolicy::IncomePerTick(int32 Baseline, int32 PerSite, int32 Sites, int32 TickSeconds)
{
	return (Baseline + PerSite * Sites) * TickSeconds;
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
