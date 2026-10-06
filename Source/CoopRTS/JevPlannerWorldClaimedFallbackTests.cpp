#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevPlannerWorldFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevClaimedFallbackWorldTest, "CoopRTS.Enemy.Planner.ClaimedFallback",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Every JEV region is held, so the player's main is the only target. The first force claims it; the second stands in
// it, healthy, with nothing else legal. It must support the claim, not retreat home.
void FJevPlannerWorldScenario::PlaceInHumanMain(ACommandGameState* State)
{
	HumanMain = ArmyTestSetup::RegionAt(State, State->FriendlyHeadquarters->GetActorLocation());
	Park(*Forces[1]);
	int32 Offset = 0;
	for (AArmyUnit* Unit : Forces[1]->GetUnits())
		Unit->SetActorLocation(ArmyTestSetup::FromFriendlyHQ(State, -250.f, 60.f * Offset++ - 150.f, 100.f));
}

bool FJevPlannerWorldScenario::ClaimedFallback(ACommandGameState* State, float Now)
{
	if (!Check(ArmyTestSetup::CurrentRegion(Forces[1].Get()) == HumanMain,
			TEXT("The second force stands in the player's main, the only enemy region")))
		return true;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FJevPublishedPlan* Published = Plan(State, Index);
		if (!Check(Published && Published->Verb == EForceVerb::Attack && Published->TargetRegionIndex == HumanMain
					&& Forces[Index]->Verb == EForceVerb::Attack && Forces[Index]->TargetRegionIndex == HumanMain,
				TEXT("A healthy force attacks the only enemy region, joining the sibling that claimed it, and never retreats")))
			return true;
	}
	if (Now < Initial[1].CommittedUntil + 2.f)
		return false; // Past the end of the second force's commitment, when the planner re-plans it.
	Test->AddInfo(TEXT("JEV claimed fallback: a healthy force with only a claimed target supports it, before and after its commitment expires."));
	return true;
}

bool FJevClaimedFallbackWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::ClaimedFallback));
	return true;
}
#endif
