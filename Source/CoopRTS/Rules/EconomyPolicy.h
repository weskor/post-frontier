#pragma once

#include "CoreMinimal.h"
#include "Rules/GameplayConstants.h"

// A deposit pays once per tick if it is alive, finished and connected to its team's main.
struct FExtractorPaymentInput
{
	int32 RatePerSecond, Remaining, TickSeconds;
	int32 Team;
	bool bAlive, bComplete, bConnected;
};

struct FExtractorPayment
{
	int32 Amount, Remaining;
};

// A price in both resources. Either part may be zero; at least one must be positive.
struct FResourceCost
{
	int32 Power = 0;
	int32 Data = 0;
};

// One commander's share of a pool payment: whole units to credit and the new carry.
struct FPoolShare
{
	int32 Whole, Carry;
};

enum class EGiftVerdict : uint8
{
	Accepted,
	SenderNotInRoster,
	OtherTeam,
	RecipientNotInRoster,
	ToSelf,
	AmountNotPositive,
	Overdraft,
	RecipientFull
};

struct FGiftInput
{
	bool bSenderInRoster, bRecipientInRoster, bSameTeam;
	int32 SenderSlot, RecipientSlot;
	int32 Amount, SenderBalance, RecipientBalance;
};

namespace EconomyPolicy
{
constexpr int32 HumanBaselineIncome = GameplayConstants::HumanBaselineIncome;
constexpr int32 JevBaselineIncome = GameplayConstants::JevBaselineIncome;
constexpr int32 RewardRegionDataRate = GameplayConstants::RewardRegionDataRate;
constexpr int32 StructureKillData = GameplayConstants::StructureKillData;
constexpr int32 NormalDepositRate = GameplayConstants::NormalDepositRate;
constexpr int32 RichDepositRate = GameplayConstants::RichDepositRate;
constexpr int32 NormalDepositAmount = GameplayConstants::NormalDepositAmount;
constexpr int32 RichDepositAmount = GameplayConstants::RichDepositAmount;
// The team log keeps this many of the latest accepted gifts.
constexpr int32 GiftLogLimit = 20;
// Wallet fractions are kept in sixtieths so every roster size from 1 to 5 divides a payment exactly.
constexpr int32 CarryDenominator = 60;
constexpr int32 MaxRecipients = 5;

// Counts below one use solo scaling; each additional human commander adds 30%.
double JevPlayerCountFactor(int32 HumanCommanders);
// JEV's baseline per second: its own constant scaled by the roster, never derived from the human baseline.
double JevBaselineRate(int32 JevBaseline, int32 HumanCommanders);
// Bit per region the team's main reaches through regions it controls; Controllers holds each region's controlling
// team by index (-1 for none). A contested region keeps its controller, so it still counts. This is
// ForceOrders::ConnectedMask, the one connectivity rule, fed by controllers.
uint64 ConnectedRegions(const uint64* Neighbours, TConstArrayView<int32> Controllers, int32 Home, int32 Team);
// Extraction consumes only what a living, finished, connected extractor pays.
FExtractorPayment ExtractorPayment(const FExtractorPaymentInput& In);
// Data one reward region adds to the pool per tick: the per-commander rate for every recipient.
int32 RewardRegionPoolData(int32 Recipients, int32 TickSeconds);
// Split Total evenly among Recipients (1 to 5) and add the commander's carry in sixtieths.
// Over any number of payments the credits plus carries equal the totals paid, exactly.
FPoolShare SplitShare(int32 Total, int32 Recipients, int32 Carry);
// Positive credits saturate at MAX_int32; non-positive credits leave the wallet unchanged.
int32 AddResources(int32 Balance, int32 Amount);
// Preserve raw construction progress and floor the unspent cost; no extra clamping.
int32 CancellationRefund(int32 Cost, float Progress);
// Spending requires a positive price and includes the exact-balance boundary.
bool CanAfford(int32 Balance, int32 Cost);
// A mixed price needs both balances; nothing is affordable when any part is negative or both are zero.
bool CanAfford(int32 PowerBalance, int32 DataBalance, const FResourceCost& Cost);
// Why a gift is accepted or refused; the first failing rule wins in declaration order.
EGiftVerdict GiftVerdict(const FGiftInput& In);
}
