#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "CommandCamera.h"
#include "HAL/FileManager.h"
#include "HUD/PressureView.h"
#include "JevThreatWorldFixture.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplitBrainViewTest, "CoopRTS.Visual.SplitBrain.Published",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Rendered proof of the published Split-Brain Cut plans (scope split-brain-view, run on request, rendered): two humans hold
// both regions of the first pair, the clock jumps to the publication, and the real HUD is captured with its alert row, its
// timeline cells and its region badges. Each PNG must exist and be non-empty; a person inspects them.
namespace
{
using namespace JevThreatKit;

struct FShot
{
	const TCHAR* Name;
	// Region whose anchor the camera focuses, or INDEX_NONE for the arena centre.
	int32 Region;
	int32 ZoomSteps; // Positive moves closer to the ground, negative out.
	// Taken after the release launches the forces, to show their live plans.
	bool bAfterLaunch = false;
};

const FShot Shots[] = {
	{ TEXT("published-overview"), INDEX_NONE, -30 },
	{ TEXT("published-uplink"), 3, 0 },
	{ TEXT("march-overview"), INDEX_NONE, -30, true },
};

FString Directory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SplitBrainCaptures"));
}

class FViewScenario : public FThreatScenario
{
public:
	explicit FViewScenario(FAutomationTestBase* InTest) : FThreatScenario(InTest, true) {}

private:
	bool Prepare() override
	{
		if (!Arrange({ 3, 8 }))
			return true;
		SkipTo(329.f);
		Enter(1);
		return false;
	}

	bool Step() override
	{
		switch (Stage)
		{
		case 1:
			if (!WaveLaunched(2))
				return false;
			// The v1.1 and v1.2 waves are not under test and would crowd the timeline.
			DestroyForces(Kit.World, [](const AArmyGroup&) { return true; });
			SkipTo(331.f);
			Enter(2);
			return false;
		case 2:
			if (Release().Cuts.IsEmpty())
				return InStage() > 3. ? Fail(TEXT("The threat did not publish")) : false;
			// The HUD reads the game state's battle clock, which SkipTo does not move; give it the same skew so the bar,
			// the release cell and the clock show the true 331 s.
			if (UPressureView* Pressure = UPressureView::Get(Kit.State))
				Pressure->SetClockSkew(Kit.Planner->GetMatchSeconds() - (Kit.State->GetServerWorldTimeSeconds() - Kit.State->GetBattleClockStartServerTime()));
			Enter(3);
			return false;
		default:
			return Capture();
		}
	}

	bool Capture()
	{
		if (Index >= UE_ARRAY_COUNT(Shots))
		{
			Test->AddInfo(FString::Printf(TEXT("Split-Brain captures written to %s"), *Directory()));
			return true;
		}
		ACommandCamera* Camera = Cast<ACommandCamera>(Kit.PC->GetPawn());
		if (!Camera)
			return Fail(TEXT("No camera to frame the capture"));
		const FShot& Shot = Shots[Index];
		const FString Path = FPaths::Combine(Directory(), FString(Shot.Name) + TEXT(".png"));
		// The march shot waits for the 360 s launch: the forces' own plans keep the threat's name on the bar.
		if (Shot.bAfterLaunch && Release().CutForces.IsEmpty())
		{
			if (!bJumped)
			{
				SkipTo(360.8f);
				bJumped = true;
				Frames = 0;
			}
			return ++Frames > 300 ? Fail(TEXT("The cut forces did not launch")) : false;
		}
		if (!bRequested)
		{
			IFileManager::Get().Delete(*Path);
			Camera->FocusOn(Shot.Region == INDEX_NONE ? FVector(-1500., -1500., 0.) : Kit.State->GetRegionAnchor(Shot.Region));
			for (int32 Step = 0; Step < FMath::Abs(Shot.ZoomSteps); ++Step)
				Camera->Zoom(Shot.ZoomSteps > 0 ? 1.f : -1.f);
			Frames = 0;
			bRequested = true;
			return false;
		}
		// Let the camera and the feed settle (1.5 s), then capture while the alert row (8 s) and the cut plans still stand.
		if (++Frames == 90)
			FScreenshotRequest::RequestScreenshot(Path, false, false);
		if (Frames > 90 && IFileManager::Get().FileSize(*Path) > 0)
		{
			const bool bAfterLaunch = Shot.bAfterLaunch;
			Check(bAfterLaunch ? !Release().CutForces.IsEmpty() && Release().Cuts.IsEmpty()
							   : !Release().Cuts.IsEmpty() && !NewEvents(FName(JevThreat::AnnouncerId)).IsEmpty(),
				TEXT("The plans (or, after launch, the forces) and the alert event still stood when the shot was taken"));
			Test->AddInfo(FString::Printf(TEXT("Captured %s (%lld bytes)"), *Path, IFileManager::Get().FileSize(*Path)));
			++Index;
			bRequested = false;
			return false;
		}
		return Frames > 900 ? Fail(*FString::Printf(TEXT("No screenshot produced for %s"), Shot.Name)) : false;
	}

	int32 Index = 0;
	int32 Frames = 0;
	bool bRequested = false;
	bool bJumped = false;
};
}

bool FSplitBrainViewTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FViewScenario(this));
	return true;
}

#endif
