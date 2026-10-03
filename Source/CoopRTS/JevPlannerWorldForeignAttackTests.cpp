#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevPlannerWorldFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevForeignAttackWorldTest, "CoopRTS.Enemy.Planner.ForeignAttack",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FJevPlannerWorldScenario::ForeignAttack(ACommandGameState* State, float Now, const FJevPublishedPlan* Changed)
{
	if (!Check(Now < Initial[0].CommittedUntil && !Changed->bEscalated
				&& Changed->Verb == EForceVerb::Attack && State->GetRegionController(InvadedRegion) == 0
				&& State->IsRegionContested(InvadedRegion, 5),
			TEXT("Attack arrival among actual player-region defenders must not become a defending escalation")))
		return true;
	if (!Unchanged(State, 0))
		return true;
	if (ForeignObservedAt == 0.f)
		ForeignObservedAt = Now;
	if (Now - ForeignObservedAt < 1.f)
		return false;
	Test->AddInfo(TEXT("JEV foreign Attack: real target arrival and hostile player-region occupants preserve the accepted Attack, ticket, deadline and memo."));
	return true;
}

bool FJevForeignAttackWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::ForeignAttack));
	return true;
}
#endif
