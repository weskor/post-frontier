#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/PlanningPolicy.h"

// Pure rule tests for the planning phase (decision O1): no world, no actors.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningPhaseRulesTest, "CoopRTS.Rules.Planning.Phase",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlanningPhaseRulesTest::RunTest(const FString& Parameters)
{
	using PlanningPolicy::EEnd;
	const double Start = 1000., Deadline = Start + PlanningPolicy::PlanningSeconds;
	TestEqual(TEXT("Planning lasts sixty real seconds"), PlanningPolicy::PlanningSeconds, 60.);
	TestTrue(TEXT("It runs on while a human is not Ready"), PlanningPolicy::Evaluate(2, 1, Start + 10., Deadline) == EEnd::Continue);
	TestTrue(TEXT("It ends the moment every human is Ready"), PlanningPolicy::Evaluate(2, 2, Start + 10., Deadline) == EEnd::AllReady);
	TestTrue(TEXT("An un-Ready commander (one Ready of two) keeps it running"),
		PlanningPolicy::Evaluate(2, 1, Start + 11., Deadline) == EEnd::Continue);
	TestTrue(TEXT("It lasts to the last fraction of the sixtieth second"),
		PlanningPolicy::Evaluate(2, 1, Deadline - .001, Deadline) == EEnd::Continue);
	TestTrue(TEXT("It expires at sixty seconds without everyone Ready"), PlanningPolicy::Evaluate(2, 1, Deadline, Deadline) == EEnd::Expired);
	TestTrue(TEXT("Expiry wins a tie with the last Ready"), PlanningPolicy::Evaluate(2, 2, Deadline, Deadline) == EEnd::Expired);
	TestTrue(TEXT("With nobody on the roster it cannot end early"), PlanningPolicy::Evaluate(0, 0, Start + 1., Deadline) == EEnd::Continue);
	TestTrue(TEXT("but still expires"), PlanningPolicy::Evaluate(0, 0, Deadline + 5., Deadline) == EEnd::Expired);
	TestEqual(TEXT("The countdown never goes negative"), PlanningPolicy::Remaining(Deadline + 3., Deadline), 0.);
	TestEqual(TEXT("and counts real seconds down"), PlanningPolicy::Remaining(Start + 12.5, Deadline), 47.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningRosterRulesTest, "CoopRTS.Rules.Planning.Roster",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlanningRosterRulesTest::RunTest(const FString& Parameters)
{
	const int32 Kits[] = { 0, 2 };
	PlanningPolicy::FRosterChange Change = PlanningPolicy::Reconcile(Kits, TArray<int32>{ 0, 2 });
	TestTrue(TEXT("An unchanged roster changes no kit"), Change.Join.IsEmpty() && Change.Leave.IsEmpty());
	Change = PlanningPolicy::Reconcile(Kits, TArray<int32>{ 0, 1, 2, 4 });
	TestTrue(TEXT("Commanders who join during planning get a kit slot, in slot order"),
		Change.Join.Num() == 2 && Change.Join[0] == 1 && Change.Join[1] == 4 && Change.Leave.IsEmpty());
	Change = PlanningPolicy::Reconcile(Kits, TArray<int32>{ 2 });
	TestTrue(TEXT("A commander who leaves loses their kit"), Change.Leave.Num() == 1 && Change.Leave[0] == 0 && Change.Join.IsEmpty());
	Change = PlanningPolicy::Reconcile(Kits, TArray<int32>{ 1 });
	TestTrue(TEXT("A swap joins and leaves in one pass"),
		Change.Join.Num() == 1 && Change.Join[0] == 1 && Change.Leave.Num() == 2);
	Change = PlanningPolicy::Reconcile(TArray<int32>{ INDEX_NONE, INDEX_NONE, 3 }, TArray<int32>{ 3 });
	TestTrue(TEXT("Kits whose commander is gone (no slot) leave once"), Change.Leave.Num() == 1 && Change.Leave[0] == INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningKitRulesTest, "CoopRTS.Rules.Planning.Kit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlanningKitRulesTest::RunTest(const FString& Parameters)
{
	TestNull(TEXT("A commander who is not Ready may edit during planning"), PlanningPolicy::EditRejection(true, false));
	TestNotNull(TEXT("Ready locks edits"), PlanningPolicy::EditRejection(true, true));
	TestNotNull(TEXT("Nothing is editable once planning is over"), PlanningPolicy::EditRejection(false, false));

	using PlanningPolicy::FRigSite;
	const FRigSite Sites[] = {
		{ FVector(100., 0., 0.), false, true },   // reserved by an extractor
		{ FVector(900., 0., 0.), true, true },    // free, far
		{ FVector(300., 0., 0.), true, false },   // free but outside own territory
		{ FVector(500., 0., 0.), true, true },    // free and near
		{ FVector(-500., 0., 0.), true, true },   // equally near: the earlier site wins
	};
	TestEqual(TEXT("The Rig defaults to the nearest free deposit in own territory"),
		PlanningPolicy::NearestRigSite(Sites, FVector::ZeroVector), 3);
	TestEqual(TEXT("Distance is measured from the given point"), PlanningPolicy::NearestRigSite(Sites, FVector(1000., 0., 0.)), 1);
	TestEqual(TEXT("No free own deposit means no site"), PlanningPolicy::NearestRigSite(MakeArrayView(Sites, 1), FVector::ZeroVector),
		INDEX_NONE);

	PlanningPolicy::FKitFill Need = PlanningPolicy::Fill(false, false, true);
	TestTrue(TEXT("An empty kit gets both pieces at expiry"), Need.bPlaceBarracks && Need.bPlaceRig && !Need.bRefundRig);
	Need = PlanningPolicy::Fill(true, true, true);
	TestTrue(TEXT("A placed kit gets nothing"), !Need.bPlaceBarracks && !Need.bPlaceRig && !Need.bRefundRig);
	Need = PlanningPolicy::Fill(true, false, false);
	TestTrue(TEXT("With no free deposit the Rig becomes its cost in Power"), !Need.bPlaceBarracks && !Need.bPlaceRig && Need.bRefundRig);
	Need = PlanningPolicy::Fill(false, true, false);
	TestTrue(TEXT("A placed Rig is never refunded"), Need.bPlaceBarracks && !Need.bRefundRig);
	return true;
}

#endif
