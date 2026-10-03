#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevPlannerWorldFixture.h"
#include "CapturePoint.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevCommittedClaimsWorldTest, "CoopRTS.Enemy.Planner.CommittedClaims",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FJevPlannerWorldScenario::PrepareClaims(ACommandGameState* State)
{
	if (!Check(Initial[0].TargetRegionIndex != Initial[1].TargetRegionIndex,
			TEXT("Independent expansion plans reserve distinct targets")))
		return true;
	for (AMapRegion* Region : State->Regions)
		if (IsValid(Region) && IsValid(Region->Anchor) && Region->RegionRole != ERegionRole::Main
			&& Region->RegionIndex != Initial[1].TargetRegionIndex)
			Region->Anchor->ControllingTeam = 5;
	if (!Check(State->GetRegionController(Initial[0].TargetRegionIndex) == 5
				&& State->GetRegionController(Initial[1].TargetRegionIndex) == INDEX_NONE,
			TEXT("Capture invalidates the earlier force while the later force's remaining neutral target stays reserved")))
		return true;
	return false;
}

bool FJevPlannerWorldScenario::CommittedClaims(ACommandGameState* State, float Now, const FJevPublishedPlan* Changed)
{
	if (!Check(Changed->TicketNumber != Initial[0].TicketNumber
				&& Changed->TargetRegionIndex != Initial[1].TargetRegionIndex
				&& Now < Initial[1].CommittedUntil,
			TEXT("Earlier force's replacement cannot steal a later force's still-committed target")))
		return true;
	Test->AddInfo(TEXT("JEV committed claims: earlier invalidation respects later held destination before evaluation order reaches that force."));
	return true;
}

bool FJevCommittedClaimsWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::CommittedClaims));
	return true;
}
#endif
