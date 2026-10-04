#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "JevMemoTemplates.h"
#include "Rules/JevPlanner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevMemoDataTest, "CoopRTS.Jev.MemoData",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FJevMemoDataTest::RunTest(const FString&)
{
	FJevMemoTemplates Templates;
	if (!TestTrue(TEXT("Shipped Game config supplies every verb and escalation template"), Templates.Load()))
		return false;
	JevPlanner::FPlan Plan;
	Plan.Source = Plan.Target = 2;
	Plan.SizeBand = 8;
	Plan.EtaSeconds = 59.01f;
	const FString Region = TEXT("West {ETA} Cut");
	for (const JevPlanner::EVerb Verb : { JevPlanner::EVerb::MoveAndHold, JevPlanner::EVerb::Attack, JevPlanner::EVerb::Retreat })
	{
		Plan.Verb = Verb;
		const FString Memo = Templates.Format(Plan, 4471, Region);
		TestTrue(TEXT("Region text cannot be interpreted as another template token"), Memo.Contains(Region));
		TestTrue(TEXT("ETA rounds across the minute boundary without understating travel"), Memo.Contains(TEXT("1:00")));
	}
	Plan.Verb = JevPlanner::EVerb::Attack;
	Plan.bEscalated = true;
	AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("An Attack plan cannot emit a defending memo"), Templates.Format(Plan, 4471, Region).IsEmpty());
	Plan.Verb = JevPlanner::EVerb::MoveAndHold;
	Plan.Target = 3;
	AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("Defense cannot name a region other than the force's own region"), Templates.Format(Plan, 4471, Region).IsEmpty());
	FString Original;
	GConfig->GetString(TEXT("JevMemos"), TEXT("Attack"), Original, GGameIni);
	for (const FString& Invalid : { Original + TEXT(" {Unknown}"), Original + TEXT(" Retreat:") })
	{
		GConfig->SetString(TEXT("JevMemos"), TEXT("Attack"), *Invalid, GGameIni);
		FJevMemoTemplates InvalidTemplates;
		AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Contains, 1);
		const bool bLoaded = InvalidTemplates.Load();
		GConfig->SetString(TEXT("JevMemos"), TEXT("Attack"), *Original, GGameIni);
		TestFalse(TEXT("Broken or contradictory writer data cannot silently load"), bLoaded);
	}
	// The Split-Brain Cut template is writer data like the others: it names the threat, takes the same values and cannot
	// load broken or claiming another order.
	const FString Cut = Templates.FormatCut(12, 4, 64.2f, Region);
	TestTrue(TEXT("A cut memo names the threat, its ticket, its band, its region and the rounded-up ETA"),
		Cut.Contains(TEXT("Split-Brain Cut")) && Cut.Contains(TEXT("#12")) && Cut.Contains(TEXT("~4 units")) && Cut.Contains(Region)
			&& Cut.Contains(TEXT("1:05")));
	AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("A cut memo with an invalid ETA is refused"), Templates.FormatCut(12, 4, -1.f, Region).IsEmpty());
	FString OriginalCut;
	GConfig->GetString(TEXT("JevMemos"), TEXT("SplitBrainCut"), OriginalCut, GGameIni);
	for (const FString& Invalid : { OriginalCut + TEXT(" {Unknown}"), OriginalCut + TEXT(" Attack:"), FString(TEXT("Split-Brain Cut: {Ticket} {Size} {Region}")) })
	{
		GConfig->SetString(TEXT("JevMemos"), TEXT("SplitBrainCut"), *Invalid, GGameIni);
		FJevMemoTemplates InvalidTemplates;
		AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Contains, 1);
		const bool bLoaded = InvalidTemplates.Load();
		GConfig->SetString(TEXT("JevMemos"), TEXT("SplitBrainCut"), *OriginalCut, GGameIni);
		TestFalse(TEXT("A broken or contradictory cut template cannot silently load"), bLoaded);
	}
	return true;
}
#endif
