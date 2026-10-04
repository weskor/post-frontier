#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/EconomyPolicy.h"

// Pure rule tests for the shared team pool, connected territory, mixed prices and gifts.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPoolSplitTest, "CoopRTS.Rules.Economy.TeamPoolSplit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPoolRosterChangeTest, "CoopRTS.Rules.Economy.TeamPoolRosterChange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamConnectivityTest, "CoopRTS.Rules.Economy.TeamConnectivity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMixedPriceTest, "CoopRTS.Rules.Economy.MixedPrice",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGiftVerdictTest, "CoopRTS.Rules.Economy.GiftVerdict",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevBaselineRateTest, "CoopRTS.Rules.Economy.JevBaselineRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
// A wallet's total in sixtieths: whole units and the carried fraction, so exactness is checkable.
struct FLedger
{
	int64 Whole = 0;
	int32 Carry = 0;
	int64 Sixtieths() const { return Whole * EconomyPolicy::CarryDenominator + Carry; }
	void Pay(int32 Total, int32 Recipients)
	{
		const FPoolShare Share = EconomyPolicy::SplitShare(Total, Recipients, Carry);
		Whole += Share.Whole;
		Carry = Share.Carry;
	}
};
}

bool FTeamPoolSplitTest::RunTest(const FString&)
{
	// 4/s for 2 s split three ways: 2 + 2/3 each per tick, nothing lost over thirty ticks.
	FLedger Wallets[3];
	for (int32 Tick = 0; Tick < 30; ++Tick)
	{
		for (FLedger& Wallet : Wallets)
			Wallet.Pay(8, 3);
		int64 Paid = 0;
		for (const FLedger& Wallet : Wallets)
			Paid += Wallet.Sixtieths();
		TestEqual(TEXT("Credits plus carries equal the pool paid so far"), Paid, int64(8) * (Tick + 1) * EconomyPolicy::CarryDenominator);
	}
	for (const FLedger& Wallet : Wallets)
	{
		TestEqual(TEXT("Thirty ticks leave each commander exactly 80"), Wallet.Whole, int64(80));
		TestEqual(TEXT("No fraction is left behind"), Wallet.Carry, 0);
	}
	for (int32 Recipients = 1; Recipients <= EconomyPolicy::MaxRecipients; ++Recipients)
	{
		FLedger Wallet;
		for (int32 Tick = 0; Tick < 60; ++Tick)
			Wallet.Pay(7, Recipients);
		TestEqual(TEXT("Every roster size pays its exact share"), Wallet.Sixtieths(), int64(7) * 60 * EconomyPolicy::CarryDenominator / Recipients);
		TestTrue(TEXT("The carry stays below one unit"), Wallet.Carry >= 0 && Wallet.Carry < EconomyPolicy::CarryDenominator);
	}
	TestEqual(TEXT("No recipients pay nothing"), EconomyPolicy::SplitShare(8, 0, 17).Whole, 0);
	TestEqual(TEXT("No recipients keep the carry"), EconomyPolicy::SplitShare(8, 0, 17).Carry, 17);
	TestEqual(TEXT("A sixth recipient is not a roster size"), EconomyPolicy::SplitShare(8, 6, 0).Whole, 0);
	TestEqual(TEXT("An empty pool pays nothing"), EconomyPolicy::SplitShare(0, 3, 40).Whole, 0);
	TestEqual(TEXT("Reward Data is one per second per commander per region"), EconomyPolicy::RewardRegionPoolData(3, 2), 6);
	TestEqual(TEXT("No recipients earn no reward Data"), EconomyPolicy::RewardRegionPoolData(0, 2), 0);
	return true;
}

bool FTeamPoolRosterChangeTest::RunTest(const FString&)
{
	FLedger Stayer, Leaver;
	Stayer.Pay(8, 2);
	Leaver.Pay(8, 2);
	Stayer.Pay(8, 3);
	Leaver.Pay(8, 3);
	TestEqual(TEXT("A fraction accumulates under three-way splits"), Stayer.Carry, 40);
	// The leaver's wallet and carry vanish; the stayer keeps theirs and is paid in full by the smaller roster.
	const int64 Before = Stayer.Sixtieths();
	Stayer.Pay(8, 1);
	TestEqual(TEXT("The remaining commander takes the whole pool"), Stayer.Sixtieths(), Before + int64(8) * EconomyPolicy::CarryDenominator);
	FLedger Joiner;
	Joiner.Pay(8, 2);
	TestEqual(TEXT("A joiner starts with an empty carry and an exact share"), Joiner.Sixtieths(), int64(4) * EconomyPolicy::CarryDenominator);
	return true;
}

bool FTeamConnectivityTest::RunTest(const FString&)
{
	// Regions: 0 main, 1 neck, 2 far, 3 alternate link; edges 0-1, 1-2, 0-3, 3-2.
	uint64 Graph[4] = { 0b1010, 0b0101, 0b1010, 0b0101 };
	const auto Reach = [&](const uint64* Neighbours, std::initializer_list<int32> Controllers, int32 Home, int32 Team) {
		return EconomyPolicy::ConnectedRegions(Neighbours, TConstArrayView<int32>(Controllers.begin(), static_cast<int32>(Controllers.size())), Home, Team);
	};
	const auto Mask = [&](std::initializer_list<int32> Controllers) { return Reach(Graph, Controllers, 0, 0); };
	TestEqual(TEXT("Every region held and linked is connected"), Mask({ 0, 0, 0, 0 }), uint64(0b1111));
	// A contested region keeps its controller, so the team's rows below still count it.
	TestEqual(TEXT("Only the main stands alone"), Mask({ 0, -1, -1, -1 }), uint64(0b0001));
	TestEqual(TEXT("A neutral neck is cut but the alternate path keeps the far region"), Mask({ 0, -1, 0, 0 }), uint64(0b1101));
	TestEqual(TEXT("An enemy neck and an enemy link cut the far region"), Mask({ 0, 5, 0, 5 }), uint64(0b0001));
	TestEqual(TEXT("A held region behind a cut stays unconnected"), Mask({ 0, 5, 0, -1 }), uint64(0b0001));
	TestEqual(TEXT("Retaking the neck reconnects the far region"), Mask({ 0, 0, 0, 5 }), uint64(0b0111));
	TestEqual(TEXT("A lost main connects nothing"), Mask({ 5, 0, 0, 0 }), uint64(0));
	TestEqual(TEXT("A team whose main is elsewhere is not connected from this one"), Reach(Graph, { 0, 5, 5, 5 }, 0, 5), uint64(0));
	TestEqual(TEXT("JEV's chain starts at JEV's own main"), Reach(Graph, { 0, 5, 5, 5 }, 2, 5), uint64(0b1110));
	TestEqual(TEXT("No graph connects nothing"), Reach(nullptr, { 0 }, 0, 0), uint64(0));
	return true;
}

bool FMixedPriceTest::RunTest(const FString&)
{
	const FResourceCost Tier2{ 100, 50 };
	TestTrue(TEXT("Exact balances afford a mixed price"), EconomyPolicy::CanAfford(100, 50, Tier2));
	TestFalse(TEXT("One Power short"), EconomyPolicy::CanAfford(99, 50, Tier2));
	TestFalse(TEXT("One Data short"), EconomyPolicy::CanAfford(100, 49, Tier2));
	TestTrue(TEXT("A Data-only price needs no Power"), EconomyPolicy::CanAfford(0, 40, FResourceCost{ 0, 40 }));
	TestFalse(TEXT("A free price is not a purchase"), EconomyPolicy::CanAfford(10, 10, FResourceCost{}));
	TestFalse(TEXT("A negative part cannot mint resources"), EconomyPolicy::CanAfford(10, 10, FResourceCost{ -1, 5 }));
	return true;
}

bool FGiftVerdictTest::RunTest(const FString&)
{
	const FGiftInput Valid{ true, true, true, 0, 1, 100, 250, 10 };
	const auto Verdict = [&](auto Edit) {
		FGiftInput In = Valid;
		Edit(In);
		return EconomyPolicy::GiftVerdict(In);
	};
	TestEqual(TEXT("A valid gift is accepted"), EconomyPolicy::GiftVerdict(Valid), EGiftVerdict::Accepted);
	TestEqual(TEXT("The exact balance can be gifted"), Verdict([](FGiftInput& In) { In.Amount = In.SenderBalance; }), EGiftVerdict::Accepted);
	TestEqual(TEXT("A sender outside the roster"), Verdict([](FGiftInput& In) { In.bSenderInRoster = false; }), EGiftVerdict::SenderNotInRoster);
	TestEqual(TEXT("An enemy recipient"), Verdict([](FGiftInput& In) { In.bSameTeam = false; In.bRecipientInRoster = false; }), EGiftVerdict::OtherTeam);
	TestEqual(TEXT("A recipient outside the roster"), Verdict([](FGiftInput& In) { In.bRecipientInRoster = false; }), EGiftVerdict::RecipientNotInRoster);
	TestEqual(TEXT("A gift to oneself"), Verdict([](FGiftInput& In) { In.RecipientSlot = In.SenderSlot; }), EGiftVerdict::ToSelf);
	TestEqual(TEXT("Zero is not a gift"), Verdict([](FGiftInput& In) { In.Amount = 0; }), EGiftVerdict::AmountNotPositive);
	TestEqual(TEXT("A negative gift would steal"), Verdict([](FGiftInput& In) { In.Amount = -5; }), EGiftVerdict::AmountNotPositive);
	TestEqual(TEXT("One more than the balance"), Verdict([](FGiftInput& In) { In.Amount = In.SenderBalance + 1; }), EGiftVerdict::Overdraft);
	TestEqual(TEXT("A recipient wallet at the cap"), Verdict([](FGiftInput& In) { In.RecipientBalance = MAX_int32; In.SenderBalance = MAX_int32; }),
		EGiftVerdict::RecipientFull);
	return true;
}

bool FJevBaselineRateTest::RunTest(const FString&)
{
	TestEqual(TEXT("JEV's baseline defaults to 2 per second"), EconomyPolicy::JevBaselineIncome, 2);
	TestEqual(TEXT("The human baseline defaults to 2 per second"), EconomyPolicy::HumanBaselineIncome, 2);
	TestTrue(TEXT("Solo JEV earns its own constant"), FMath::IsNearlyEqual(EconomyPolicy::JevBaselineRate(2, 1), 2., 1.e-12));
	TestTrue(TEXT("Two commanders scale JEV by 1.3"), FMath::IsNearlyEqual(EconomyPolicy::JevBaselineRate(2, 2), 2.6, 1.e-12));
	TestTrue(TEXT("A different JEV constant scales the same way"), FMath::IsNearlyEqual(EconomyPolicy::JevBaselineRate(5, 3), 8., 1.e-12));
	TestEqual(TEXT("Zero is a valid JEV baseline"), EconomyPolicy::JevBaselineRate(0, 4), 0.);
	return true;
}

#endif
