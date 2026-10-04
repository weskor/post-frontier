#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "PlanningFixture.h"
#include "UnrealClient.h"

// Rendered proof for JEV's first plans (scope planning-jev-plans-view, run on request): during planning, with the
// world frozen, the real HUD draws the timeline, region badges and memo feed from the plans JEV published before
// 0:00. The PNG must exist and be non-empty; a person inspects it.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningJevPlansViewTest, "CoopRTS.Visual.PlanningJevPlans",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace PlanningJevPlansViewTests
{
class FScenario final : public PlanningFixture::FScenario
{
public:
	using PlanningFixture::FScenario::FScenario;

private:
	static FString Path() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("PlanningJevPlansCaptures") / TEXT("planning-timeline.png")); }

	bool Step() override
	{
		if (State->EnemyPlans.Num() != 2 || !JevKitStands(2))
		{
			if (StageSeconds() > 30.)
			{
				Check(false, FString::Printf(TEXT("JEV's two kit forces have published plans (plans %d)"), State->EnemyPlans.Num()));
				return Done();
			}
			return false;
		}
		if (!bRequested)
		{
			IFileManager::Get().Delete(*Path());
			Frames = 0;
			bRequested = true;
		}
		if (++Frames == 150)
			FScreenshotRequest::RequestScreenshot(Path(), false, false);
		if (Frames > 150 && IFileManager::Get().FileSize(*Path()) > 0)
		{
			Test->AddInfo(FString::Printf(TEXT("Captured %s (%lld bytes) with %d plans (tickets %d, %d), planning %d, paused %d"), *Path(),
				IFileManager::Get().FileSize(*Path()), State->EnemyPlans.Num(), State->EnemyPlans[0].TicketNumber,
				State->EnemyPlans[1].TicketNumber, State->IsPlanning(), World->IsPaused()));
			return Done();
		}
		if (Frames > 900)
		{
			Check(false, TEXT("No screenshot was produced"));
			return Done();
		}
		return false;
	}

	int32 Frames = 0;
	bool bRequested = false;
};
}

bool FPlanningJevPlansViewTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(PlanningJevPlansViewTests::FScenario(this));
	return true;
}

#endif
