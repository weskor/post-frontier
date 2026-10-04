#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/EconomyPolicy.h"

// Pure rule tests: no world, no actors. Values are arbitrary; assertions are invariants.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyIncomeTest, "CoopRTS.Rules.Economy.IncomeAndSaturation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyJevScalingTest, "CoopRTS.Rules.Economy.JevPlayerCount",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyExtractorTest, "CoopRTS.Rules.Economy.ExtractorDepletionAndOwner",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyRefundTest, "CoopRTS.Rules.Economy.Refund",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyAffordabilityTest, "CoopRTS.Rules.Economy.Affordability",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FEconomyJevScalingTest::RunTest(const FString& Parameters)
{
	const double Expected[] = { 1., 1.3, 1.6, 1.9, 2.2 };
	for (int32 Count = 1; Count <= 5; ++Count)
		TestTrue(FString::Printf(TEXT("%d human commanders use the specified JEV factor"), Count),
			FMath::IsNearlyEqual(EconomyPolicy::JevPlayerCountFactor(Count), Expected[Count - 1], 1.e-12));
	for (const int32 Count : { MIN_int32, -1, 0 })
		TestEqual(TEXT("Invalid counts use the solo factor"), EconomyPolicy::JevPlayerCountFactor(Count), 1.);
	return true;
}

bool FEconomyIncomeTest::RunTest(const FString& Parameters)
{
	constexpr int32 Income = 19;
	TestEqual(TEXT("Normal addition exact"), EconomyPolicy::AddResources(19, Income), 19 + Income);
	TestEqual(TEXT("Exact cap reachable"), EconomyPolicy::AddResources(MAX_int32 - Income, Income), MAX_int32);
	TestEqual(TEXT("Addition saturates past cap"), EconomyPolicy::AddResources(MAX_int32 - Income + 1, Income), MAX_int32);
	TestEqual(TEXT("Capped wallet stays capped"), EconomyPolicy::AddResources(MAX_int32, MAX_int32), MAX_int32);
	TestEqual(TEXT("Zero addition preserves balance"), EconomyPolicy::AddResources(19, 0), 19);
	TestEqual(TEXT("Negative addition does not debit"), EconomyPolicy::AddResources(19, -1), 19);
	return true;
}

bool FEconomyExtractorTest::RunTest(const FString& Parameters)
{
	FExtractorPaymentInput In{ 4, EconomyPolicy::NormalDepositAmount, 2, 0, true, true, true };
	FExtractorPayment Payment = EconomyPolicy::ExtractorPayment(In);
	TestEqual(TEXT("Normal extractor pays eight per two seconds"), Payment.Amount, 8);
	TestEqual(TEXT("Normal extraction subtracts exactly payment"), Payment.Remaining, EconomyPolicy::NormalDepositAmount - 8);
	TestEqual(TEXT("Normal reserve is halved"), EconomyPolicy::NormalDepositAmount, 1200);
	TestEqual(TEXT("Rich reserve is halved"), EconomyPolicy::RichDepositAmount, 1500);
	In.RatePerSecond = 6;
	In.Remaining = EconomyPolicy::RichDepositAmount;
	Payment = EconomyPolicy::ExtractorPayment(In);
	TestEqual(TEXT("Rich extractor pays twelve per two seconds"), Payment.Amount, 12);
	TestEqual(TEXT("Rich extraction subtracts exactly payment"), Payment.Remaining, EconomyPolicy::RichDepositAmount - 12);
	In.Remaining = 5;
	Payment = EconomyPolicy::ExtractorPayment(In);
	TestEqual(TEXT("Final payment is capped by remaining deposit"), Payment.Amount, 5);
	TestEqual(TEXT("Final payment depletes deposit to zero"), Payment.Remaining, 0);
	In.Remaining = Payment.Remaining;
	TestEqual(TEXT("Empty deposit stops income"), EconomyPolicy::ExtractorPayment(In).Amount, 0);
	In.Remaining = 23;
	for (const bool bAlive : { false, true })
		for (const bool bComplete : { false, true })
			for (const bool bConnected : { false, true })
			{
				In.bAlive = bAlive;
				In.bComplete = bComplete;
				In.bConnected = bConnected;
				const bool bPays = bAlive && bComplete && bConnected;
				Payment = EconomyPolicy::ExtractorPayment(In);
				TestEqual(TEXT("Only completed, living, connected extractors pay"), Payment.Amount, bPays ? 12 : 0);
				TestEqual(TEXT("Unpaid extractors never consume deposit"), Payment.Remaining, bPays ? 11 : 23);
			}
	In.bAlive = In.bComplete = In.bConnected = true;
	In.Team = 5;
	TestEqual(TEXT("Enemy extractor pays"), EconomyPolicy::ExtractorPayment(In).Amount, 12);
	for (const int32 Team : { -1, 1, 4, 6 })
	{
		In.Team = Team;
		TestEqual(TEXT("Only the two sides' extractors pay"), EconomyPolicy::ExtractorPayment(In).Amount, 0);
	}
	In.Team = 0;
	In.RatePerSecond = MAX_int32;
	In.TickSeconds = MAX_int32;
	In.Remaining = MAX_int32;
	TestEqual(TEXT("Large extraction multiply cannot overflow"), EconomyPolicy::ExtractorPayment(In).Amount, MAX_int32);
	In.TickSeconds = 0;
	TestEqual(TEXT("Zero-duration extraction consumes nothing"), EconomyPolicy::ExtractorPayment(In).Remaining, MAX_int32);
	return true;
}

bool FEconomyRefundTest::RunTest(const FString& Parameters)
{
	constexpr int32 Cost = 19;
	TestEqual(TEXT("No progress refunds full cost"), EconomyPolicy::CancellationRefund(Cost, 0.f), Cost);
	TestEqual(TEXT("Completed work has no refund"), EconomyPolicy::CancellationRefund(Cost, 1.f), 0);
	TestEqual(TEXT("Fractional remainder rounds down, not nearest"), EconomyPolicy::CancellationRefund(Cost, .5f), Cost / 2);
	TestTrue(TEXT("Further progress cannot increase refund"),
		EconomyPolicy::CancellationRefund(Cost, .75f) <= EconomyPolicy::CancellationRefund(Cost, .5f));
	TestEqual(TEXT("Zero-cost cancellation returns nothing"), EconomyPolicy::CancellationRefund(0, .5f), 0);
	return true;
}

bool FEconomyAffordabilityTest::RunTest(const FString& Parameters)
{
	constexpr int32 Cost = 19;
	TestFalse(TEXT("One short cannot spend"), EconomyPolicy::CanAfford(Cost - 1, Cost));
	TestTrue(TEXT("Exact balance can spend"), EconomyPolicy::CanAfford(Cost, Cost));
	TestTrue(TEXT("Surplus can spend"), EconomyPolicy::CanAfford(Cost + 1, Cost));
	TestFalse(TEXT("Zero cost is rejected by wallet"), EconomyPolicy::CanAfford(Cost, 0));
	TestFalse(TEXT("Negative cost cannot mint resources"), EconomyPolicy::CanAfford(Cost, -1));
	return true;
}

#endif
