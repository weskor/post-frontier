#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevPlannerWorldFixture.h"
#include "CapturePoint.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevEscalationWorldTest, "CoopRTS.Enemy.Planner.Escalation",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FJevPlannerWorldScenario::PrepareInvasion(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC)
{
	InvadedRegion = ArmyTestSetup::CurrentRegion(Forces[0].Get());
	if (!Check(InvadedRegion != INDEX_NONE && InvadedRegion != ArmyTestSetup::CurrentRegion(Forces[1].Get()),
			TEXT("Source invasion fixture must isolate one force's actual polygon")))
		return true;
	if (!Check(State->GetRegionController(InvadedRegion) == (Proof == EJevWorldProof::Escalation ? 5 : 0),
			TEXT("Defense invasion uses JEV control; foreign Attack invasion uses player control")))
		return true;
	Intruder = ArmyTestSetup::SpawnGroup(World, PC, 43,
		State->GetRegionAnchor(InvadedRegion) + FVector(0.f, 350.f, 100.f));
	if (!Intruder.IsValid())
		return Fail(TEXT("Real source-region invasion must spawn"));
	Park(*Intruder);
	if (!Check(State->IsRegionContested(InvadedRegion, 5),
			TEXT("Actual hostile members must occupy the force's source region")))
		return true;
	return false;
}

bool FJevPlannerWorldScenario::Escalation(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now, const FJevPublishedPlan* Changed)
{
	if (!Check(Changed->TicketNumber == Initial[0].TicketNumber
				&& Changed->CommittedUntil == Initial[0].CommittedUntil && Changed->bEscalated
				&& Changed->Verb == EForceVerb::MoveHold && Changed->SourceRegionIndex == InvadedRegion
				&& Changed->TargetRegionIndex == InvadedRegion && !Changed->TargetStructure
				&& Changed->Memo.Contains(TEXT("Escalated: defending ") + RegionName(State, InvadedRegion))
				&& Forces[0]->OrderSerial > InitialSerial[0],
			TEXT("Own-region invasion must accept a visibly defending MoveHold with the same ticket and deadline")))
		return true;
	if (Now - AcceptedAt < 5.f)
		return false; // Exercise repeated evaluations while the intrusion remains real.
	return ReEscalate(World, State, PC, Now, Changed);
}

bool FJevPlannerWorldScenario::ReEscalate(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now, const FJevPublishedPlan* Changed)
{
	const int32 FirstRegion = InvadedRegion;
	const uint32 FirstDefenseSerial = Forces[0]->OrderSerial;
	if (!DefenseHistory(State, Changed->TicketNumber, FirstRegion))
		return true;
	Park(*Forces[1]);
	int32 IndependentOffset = 0;
	for (AArmyUnit* Unit : Forces[1]->GetUnits())
		Unit->SetActorLocation(State->GetRegionAnchor(Initial[1].SourceRegionIndex)
			+ FVector(60.f * IndependentOffset++, 0.f, 100.f));
	AMapRegion* NextRegion = nullptr;
	for (AMapRegion* Region : State->Regions)
		if (IsValid(Region) && IsValid(Region->Anchor) && Region->RegionRole != ERegionRole::Main
			&& Region->RegionIndex != FirstRegion && Region->RegionIndex != Initial[1].SourceRegionIndex
			&& Region->RegionIndex != Initial[1].TargetRegionIndex
			&& Region->RegionIndex != ArmyTestSetup::CurrentRegion(Forces[1].Get()))
		{
			NextRegion = Region;
			break;
		}
	if (!NextRegion)
		return Fail(TEXT("Re-escalation needs a separate anchored region away from the independent force"));
	NextRegion->Anchor->ControllingTeam = 5;
	Park(*Forces[0]);
	int32 Offset = 0;
	for (AArmyUnit* Unit : Forces[0]->GetUnits())
		Unit->SetActorLocation(State->GetRegionAnchor(NextRegion->RegionIndex)
			+ FVector(60.f * Offset++, 0.f, 100.f));
	Intruder = ArmyTestSetup::SpawnGroup(World, PC, 45,
		State->GetRegionAnchor(NextRegion->RegionIndex) + FVector(0.f, 350.f, 100.f));
	if (!Intruder.IsValid())
		return Fail(TEXT("Second attacked-region intrusion fixture must spawn"));
	Park(*Intruder);
	if (!Check(ArmyTestSetup::CurrentRegion(Forces[0].Get()) == NextRegion->RegionIndex
				&& State->IsRegionContested(NextRegion->RegionIndex, 5),
			TEXT("Already-escalated force and real hostile members occupy a different attacked JEV region")))
		return true;
	return FinishEscalation(State, Now, FirstRegion, FirstDefenseSerial, NextRegion);
}

bool FJevPlannerWorldScenario::FinishEscalation(ACommandGameState* State, float Now, int32 FirstRegion, uint32 FirstDefenseSerial, AMapRegion* NextRegion)
{
	Planner->EvaluatePlan();
	const FJevPublishedPlan* ReEscalated = Plan(State, 0);
	if (!PublishedMatches(State, Now) || !Unchanged(State, 1)
		|| !Check(Now < Initial[0].CommittedUntil && ReEscalated->bEscalated
				&& ReEscalated->TicketNumber == Initial[0].TicketNumber
				&& ReEscalated->CommittedUntil == Initial[0].CommittedUntil
				&& ReEscalated->SourceRegionIndex == NextRegion->RegionIndex
				&& ReEscalated->TargetRegionIndex == NextRegion->RegionIndex
				&& Forces[0]->OrderSerial > FirstDefenseSerial,
			TEXT("Re-escalation accepts defense of the new source while preserving the ticket and deadline"))
		|| !DefenseHistory(State, ReEscalated->TicketNumber, NextRegion->RegionIndex))
		return true;
	const uint32 ReEscalatedSerial = Forces[0]->OrderSerial;
	Planner->EvaluatePlan();
	if (!Check(Forces[0]->OrderSerial == ReEscalatedSerial,
			TEXT("Repeated same-region defense must not issue another command"))
		|| !DefenseHistory(State, Initial[0].TicketNumber, FirstRegion)
		|| !DefenseHistory(State, Initial[0].TicketNumber, NextRegion->RegionIndex))
		return true;
	Test->AddInfo(TEXT("JEV escalation: own-source defense, stable commitment, re-escalation in a different JEV region with one history event per defended region and no repeated command/event."));
	return true;
}

bool FJevEscalationWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::Escalation));
	return true;
}
#endif
