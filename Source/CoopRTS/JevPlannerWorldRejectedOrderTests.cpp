#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevPlannerWorldFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevRejectedOrderWorldTest, "CoopRTS.Enemy.Planner.RejectedOrder",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FJevPlannerWorldScenario::RejectedOrder(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now)
{
	if (Now < Initial[0].CommittedUntil)
		return !Unchanged(State, 0) || !Unchanged(State, 1);
	const FJevPublishedPlan* Actual = Plan(State, 0);
	if (!Check(Actual->Verb == Initial[0].Verb && Actual->TargetRegionIndex == Initial[0].TargetRegionIndex
				&& Actual->TargetStructure == Initial[0].TargetStructure && !Actual->bEscalated
				&& Forces[0]->OrderSerial == InitialSerial[0],
			TEXT("Rejected proposals keep the live accepted order published, not the rejected desired order")))
		return true;
	if (Stage == 4)
	{
		RejectedFallback = *Actual;
		if (!Check(Actual->TicketNumber != Initial[0].TicketNumber
					&& FMath::IsNearlyEqual(Actual->CommittedUntil - Now, 25.f, .01f),
				TEXT("All rejected fresh candidates adopt the actual order with a new full commitment")))
			return true;
		Stage = 5;
	}
	if (!Check(Actual->TicketNumber == RejectedFallback.TicketNumber
				&& Actual->CommittedUntil == RejectedFallback.CommittedUntil && Actual->Memo == RejectedFallback.Memo,
			TEXT("Actual-order fallback remains published and committed across repeated evaluations")))
		return true;
	if (Now - RejectedAt < 2.f)
		return false;
	return RejectedDefense(World, State, PC, Now);
}

bool FJevPlannerWorldScenario::RejectedDefense(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now)
{
	AArmyGroup* Force = Forces[0].Get();
	const int32 Number = Force->ForceNumber;
	Force->Initialize({ 5, State->EnemyCommander.Get(), Force->GetArmyIndex(),
		Force->GetProductionBuilding(), Force->GetHomeLocation() });
	Force->ForceNumber = Number;
	const int32 Source = Initial[0].SourceRegionIndex;
	int32 Offset = 0;
	for (AArmyUnit* Unit : Force->GetUnits())
		Unit->SetActorLocation(State->GetRegionAnchor(Source) + FVector(60.f * Offset++, 0.f, 100.f));
	Intruder = ArmyTestSetup::SpawnGroup(World, PC, 44,
		State->GetRegionAnchor(Source) + FVector(0.f, 350.f, 100.f));
	if (!Intruder.IsValid())
		return Fail(TEXT("Rejected-commitment invasion fixture must spawn"));
	Park(*Intruder);
	if (!Check(State->GetRegionController(Source) == 5 && State->IsRegionContested(Source, 5),
			TEXT("Rejected commitment's live source must be an attacked JEV region")))
		return true;
	Planner->EvaluatePlan();
	const FJevPublishedPlan* Defense = Plan(State, 0);
	if (!PublishedMatches(State, Now)
		|| !Check(Now < RejectedFallback.CommittedUntil
				&& Defense->TicketNumber == RejectedFallback.TicketNumber
				&& Defense->CommittedUntil == RejectedFallback.CommittedUntil && Defense->bEscalated
				&& Defense->Verb == EForceVerb::MoveHold && Defense->SourceRegionIndex == Source
				&& Defense->TargetRegionIndex == Source && Force->OrderSerial > InitialSerial[0],
			TEXT("Forced own-region defense overrides the rejected-order shortcut inside its original commitment"))
		|| !DefenseHistory(State, Defense->TicketNumber, Source))
		return true;
	Test->AddInfo(TEXT("JEV rejected order: real rejection preserves actual-order commitment; restored ownership and real own-region invasion force defense before that commitment expires."));
	return true;
}

bool FJevRejectedOrderWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::RejectedOrder));
	return true;
}
#endif
