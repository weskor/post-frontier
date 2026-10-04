#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "VerbInputFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVerbInputWorldTest, "CoopRTS.Input.Verbs",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace VerbInputTests
{
bool FScenario::Update()
{
	if (Started < 0.)
		Started = FPlatformTime::Seconds();
	if (FPlatformTime::Seconds() - Started > 45.)
		return Fail(TEXT("Verb input scenario exceeded 45 seconds"));
	if (bFailed)
		return true;
	UWorld* World = ArmyTestSetup::World();
	if (!World || ArmyTestSetup::GameSeconds(World) < 3. || (Stage == 0 && !ArmyTestSetup::NavigationReady(World)))
		return false;
	if (Stage == 0)
		return Initialize(World);
	if (!Check(IsValid(PC) && IsValid(HUD) && IsValid(Camera) && IsValid(State)
				&& IsValid(Forces[0]) && IsValid(Forces[1]) && IsValid(Hostile) && IsValid(Producer),
			TEXT("Isolated input fixtures survive")))
		return true;
	return RunStage(World);
}

bool FScenario::RunStage(UWorld* World)
{
	switch (Stage)
	{
	case 1:
		return StageSmartMinimap();
	case 2:
		return StageSmartGround();
	case 3:
		return StageAttackKey();
	case 4:
		return StageRetreatKey();
	case 5:
		return StageReopenAfterRetreat();
	case 6:
		return StageEscape();
	case 7:
		return StageReopenForRightClick();
	case 8:
		return StageRightClick();
	case 9:
		return StageQueueAppend();
	case 10:
		return StageQueueFullTargeting();
	case 11:
		return StageUnshiftedConfirm();
	case 12:
		return StageGroundAttackOpen();
	case 13:
		return StageGroundAttackClick();
	case 14:
		return StageRally();
	case 15:
		return StageArrowKey();
	case 16:
		return StageEdgePan(World);
	}
	return false;
}
}

bool FVerbInputWorldTest::RunTest(const FString&)
{
	for (const FIntPoint Resolution : { FIntPoint(1600, 900), FIntPoint(1280, 720) })
		ADD_LATENT_AUTOMATION_COMMAND(VerbInputTests::FScenario(this, Resolution));
	return true;
}
#endif
