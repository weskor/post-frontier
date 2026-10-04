#include "EconomyPolicy.h"
#include "Rules/ForceOrderPolicy.h"

double EconomyPolicy::JevPlayerCountFactor(int32 HumanCommanders)
{
	return 1. + 0.3 * (FMath::Max(1, HumanCommanders) - 1);
}

double EconomyPolicy::JevBaselineRate(int32 JevBaseline, int32 HumanCommanders)
{
	return JevBaseline * JevPlayerCountFactor(HumanCommanders);
}

uint64 EconomyPolicy::ConnectedRegions(const uint64* Neighbours, TConstArrayView<int32> Controllers, int32 Home, int32 Team)
{
	uint64 Controlled = 0;
	for (int32 Index = 0; Index < Controllers.Num() && Index < ForceOrders::MaxRegions; ++Index)
		if (Controllers[Index] == Team)
			Controlled |= uint64(1) << Index;
	return ForceOrders::ConnectedMask(Neighbours, Controllers.Num(), Home, Controlled);
}

FExtractorPayment EconomyPolicy::ExtractorPayment(const FExtractorPaymentInput& In)
{
	if (!In.bAlive || !In.bComplete || !In.bConnected || (In.Team != 0 && In.Team != 5)
		|| In.RatePerSecond <= 0 || In.Remaining <= 0 || In.TickSeconds <= 0)
		return { 0, In.Remaining };
	const int32 Amount = static_cast<int32>(FMath::Min<int64>(In.Remaining,
		static_cast<int64>(In.RatePerSecond) * In.TickSeconds));
	return { Amount, In.Remaining - Amount };
}

int32 EconomyPolicy::RewardRegionPoolData(int32 Recipients, int32 TickSeconds)
{
	return Recipients < 1 || TickSeconds < 1 ? 0 : RewardRegionDataRate * Recipients * TickSeconds;
}

FPoolShare EconomyPolicy::SplitShare(int32 Total, int32 Recipients, int32 Carry)
{
	const int32 SafeCarry = FMath::Clamp(Carry, 0, CarryDenominator - 1);
	if (Total <= 0 || Recipients < 1 || Recipients > MaxRecipients)
		return { 0, SafeCarry };
	// Recipients divides 60, so the share in sixtieths is an integer.
	const int64 Sixtieths = static_cast<int64>(Total) * (CarryDenominator / Recipients) + SafeCarry;
	return { static_cast<int32>(FMath::Min<int64>(MAX_int32, Sixtieths / CarryDenominator)),
		static_cast<int32>(Sixtieths % CarryDenominator) };
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

bool EconomyPolicy::CanAfford(int32 PowerBalance, int32 DataBalance, const FResourceCost& Cost)
{
	return Cost.Power >= 0 && Cost.Data >= 0 && (Cost.Power > 0 || Cost.Data > 0)
		&& PowerBalance >= Cost.Power && DataBalance >= Cost.Data;
}

EGiftVerdict EconomyPolicy::GiftVerdict(const FGiftInput& In)
{
	if (!In.bSenderInRoster)
		return EGiftVerdict::SenderNotInRoster;
	if (!In.bSameTeam)
		return EGiftVerdict::OtherTeam;
	if (!In.bRecipientInRoster)
		return EGiftVerdict::RecipientNotInRoster;
	if (In.SenderSlot == In.RecipientSlot)
		return EGiftVerdict::ToSelf;
	if (In.Amount <= 0)
		return EGiftVerdict::AmountNotPositive;
	if (In.SenderBalance < In.Amount)
		return EGiftVerdict::Overdraft;
	if (static_cast<int64>(In.RecipientBalance) + In.Amount > MAX_int32)
		return EGiftVerdict::RecipientFull;
	return EGiftVerdict::Accepted;
}
