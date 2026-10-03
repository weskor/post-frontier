#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevPlannerWorldFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevTargetDestroyedWorldTest, "CoopRTS.Enemy.Planner.TargetDestroyed",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FJevPlannerWorldScenario::PrepareDestroyed()
{
	ACommandBuilding* Target = Cast<ACommandBuilding>(Initial[0].TargetStructure);
	Target->ReceiveAttack(Target->Health, Forces[0]->GetUnits()[0]);
	if (!Check(!IsValid(Target) || !Target->IsAlive(),
			TEXT("Real lethal damage destroys the concrete hostile structure")))
		return true;
	return false;
}

bool FJevPlannerWorldScenario::TargetDestroyed(ACommandGameState* State, float Now, const FJevPublishedPlan* Changed)
{
	if (!Check(Now < Initial[0].CommittedUntil && Changed->TicketNumber != Initial[0].TicketNumber
				&& Changed->CommittedUntil > Initial[0].CommittedUntil
				&& Changed->TargetStructure != Initial[0].TargetStructure
				&& Forces[0]->OrderSerial > InitialSerial[0],
			TEXT("Concrete target death must accept a fresh independent plan before the old deadline")))
		return true;
	Test->AddInfo(TEXT("JEV target invalidation: real hostile structure death, accepted replacement before expiry, fresh ticket, independent force and faithful publication/memos."));
	return true;
}

bool FJevTargetDestroyedWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::TargetDestroyed));
	return true;
}
#endif
