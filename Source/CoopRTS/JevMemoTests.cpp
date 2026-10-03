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
	return true;
}
#endif
