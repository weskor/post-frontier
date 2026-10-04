#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "CommandCamera.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainViewCaptureTest, "CoopRTS.Visual.TerrainCaptures.Overview",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Rendered proof for Habitable Zone v2 (scope terrain-view, run on request): the real game renders an overview, a
// ramp and plateau close-up and a cover-region close-up with the HUD hidden. Each PNG must exist and be non-empty; a person inspects them.
namespace TerrainViewTests
{
struct FShot
{
	const TCHAR* Name;
	FVector Focus;
	int32 ZoomSteps; // Positive moves closer to the ground: each step is a factor of 1.15 on the arm length.
};

// Overview over the arena centre; Switchback's ramp to Human Near from the low side; West Cut's cover props.
const FShot Shots[] = {
	{ TEXT("overview"), FVector(-1500., -1500., 0.), -30 },
	{ TEXT("ramp-plateau"), FVector(-2800., -1000., 0.), 4 },
	{ TEXT("ridge-plateau"), FVector(7500., -5500., 0.), 0 },
	{ TEXT("cover-region"), FVector(-1800., -7500., 0.), 3 },
	{ TEXT("hazard-ground"), FVector(-5000., 6500., 0.), 0 },
};

class FTerrainViewCapture : public IAutomationLatentCommand
{
public:
	explicit FTerrainViewCapture(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 240.)
		{
			Test->AddError(FString::Printf(TEXT("Terrain captures timed out at shot %d"), Index));
			return true;
		}
		UWorld* World = ArmyTestSetup::World();
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		ACommandPlayerController* PC = World ? ArmyTestSetup::Controller(World) : nullptr;
		ACommandCamera* Camera = PC ? Cast<ACommandCamera>(PC->GetPawn()) : nullptr;
		if (!ArmyTestSetup::MapReady(State) || !Camera || ArmyTestSetup::GameSeconds(World) < 3.)
			return false;
		if (Index >= UE_ARRAY_COUNT(Shots))
		{
			Test->AddInfo(FString::Printf(TEXT("Terrain captures written to %s"), *Directory()));
			return true;
		}
		if (!bHudHidden)
		{
			PC->ConsoleCommand(TEXT("ShowHUD")); // Toggles AHUD::bShowHUD off: the panels cover 40% of a shot.
			bHudHidden = true;
		}
		const FShot& Shot = Shots[Index];
		const FString Path = FPaths::Combine(Directory(), FString(Shot.Name) + TEXT(".png"));
		if (!bRequested)
		{
			IFileManager::Get().Delete(*Path);
			Camera->FocusOn(Shot.Focus);
			for (int32 Step = 0; Step < FMath::Abs(Shot.ZoomSteps); ++Step)
				Camera->Zoom(Shot.ZoomSteps > 0 ? 1.f : -1.f);
			Frames = 0;
			bRequested = true;
			return false;
		}
		if (++Frames == 30) // Let streaming, shadows and exposure settle after the move.
			FScreenshotRequest::RequestScreenshot(Path, false, false);
		if (Frames > 30 && IFileManager::Get().FileSize(*Path) > 0)
		{
			Test->AddInfo(FString::Printf(TEXT("Captured %s (%lld bytes)"), *Path, IFileManager::Get().FileSize(*Path)));
			++Index;
			bRequested = false;
			return false;
		}
		if (Frames > 600)
		{
			Test->AddError(FString::Printf(TEXT("No screenshot produced for %s"), Shot.Name));
			return true;
		}
		return false;
	}

private:
	static FString Directory() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("TerrainCaptures")); }

	FAutomationTestBase* Test;
	int32 Index = 0;
	int32 Frames = 0;
	bool bRequested = false;
	bool bHudHidden = false;
	double Started;
};
}

bool FTerrainViewCaptureTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(TerrainViewTests::FTerrainViewCapture(this));
	return true;
}

#endif
