#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CommandCamera.h"
#include "FormationHeadingNeck.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingViewTest, "CoopRTS.Visual.FormationHeading.Plans",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Rendered proof for the heading-aware formation (scope formation-heading-view, run on request): a six-member
// force really marches a two-hop route through a built 3 m neck, the camera following it, and the real game is
// captured on the approach, in the neck and when the force has arrived and holds (the box). Nothing is teleported
// or frozen. The test uses no formation API, so the same capture runs against main for comparison. Each PNG must
// exist and be non-empty; a person inspects them.
namespace FormationHeadingView
{
class FCapture : public IAutomationLatentCommand
{
public:
	explicit FCapture(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 240.)
			return Fail(FString::Printf(TEXT("Captures timed out at stage %d (shot %d)"), Stage, Shot));
		UWorld* World = ArmyTestSetup::World();
		State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		ACommandPlayerController* PC = World ? ArmyTestSetup::Controller(World) : nullptr;
		Camera = PC ? Cast<ACommandCamera>(PC->GetPawn()) : nullptr;
		if (!ArmyTestSetup::MapReady(State) || !Camera || ArmyTestSetup::GameSeconds(World) < 3. || !ArmyTestSetup::NavigationReady(World))
			return false;
		const double Now = ArmyTestSetup::GameSeconds(World);
		switch (Stage)
		{
		case 0:
			return Begin(*World, *PC, Now);
		case 1:
			return Now - StageStarted >= .5 ? Build(*World, *PC, Now) : false;
		case 2:
			return Now - StageStarted >= 2.5 ? March(*World, *PC, Now) : false;
		default:
			return Follow(Now);
		}
	}

private:
	bool Fail(const FString& Message)
	{
		Test->AddError(Message);
		return true;
	}

	bool Begin(UWorld& World, ACommandPlayerController& PC, double Now)
	{
		for (TActorIterator<AEnemyCommander> It(&World); It; ++It)
			It->Destroy();
		for (TActorIterator<AArmyGroup> It(&World); It; ++It)
			if (It->IsOpposingArmy())
				It->Destroy();
		const int32 Home = ArmyTestSetup::RegionAt(State, State->FriendlyHeadquarters->GetActorLocation());
		Force = ArmyTestSetup::SpawnGroup(&World, &PC, 0, State->GetRegionAnchor(Home) + FVector(0.f, -350.f, 100.f));
		if (!Force.IsValid() || Force->GetUnits().Num() != 6)
			return Fail(TEXT("A six-member fixture force must spawn"));
		PC.ConsoleCommand(TEXT("ShowHUD")); // Toggles the HUD off: the panels cover a large part of a shot.
		Stage = 1;
		StageStarted = Now;
		return false;
	}

	bool Build(UWorld& World, ACommandPlayerController& PC, double Now)
	{
		ACommandPlayerState* Wallet = PC.GetPlayerState<ACommandPlayerState>();
		if (!FormationHeadingNeck::ChooseNeckRoute(World, *Wallet, *Force, *State, Route))
			return Fail(TEXT("A route with a path exists on the map"));
		FormationHeadingNeck::BuildNeck(World, Route, FMath::Clamp(Route.Points.Num() / 2, 6, Route.Points.Num() - 4), NeckGap, Walls);
		Stage = 2;
		StageStarted = Now;
		return false;
	}

	bool March(UWorld& World, ACommandPlayerController& PC, double Now)
	{
		ACommandPlayerState* Wallet = PC.GetPlayerState<ACommandPlayerState>();
		if (!FormationHeadingNeck::Replan(World, *Wallet, *Force, *State, Route))
			return Fail(TEXT("The force has a path through the neck"));
		Camera->FocusOn(Force->GetCenter());
		for (int32 Step = 0; Step < 3; ++Step)
			Camera->Zoom(1.f);
		Stage = 3;
		StageStarted = Now;
		return false;
	}

	// Each shot is requested once its condition holds, then waited for before the next.
	bool Follow(double Now)
	{
		if (!Force.IsValid() || Force->GetUnits().Num() != 6)
			return Fail(TEXT("The force survives its march"));
		Camera->FocusOn(Force->GetCenter());
		if (Shot >= UE_ARRAY_COUNT(Names))
		{
			Test->AddInfo(FString::Printf(TEXT("Formation heading captures written to %s"), *Directory()));
			return true;
		}
		const FString Path = FPaths::Combine(Directory(), FString(Names[Shot]) + TEXT(".png"));
		if (!bRequested)
		{
			if (!Ready(Shot))
				return false;
			IFileManager::Get().Delete(*Path);
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			bRequested = true;
			Frames = 0;
			return false;
		}
		if (IFileManager::Get().FileSize(*Path) > 0)
		{
			Test->AddInfo(FString::Printf(TEXT("Captured %s (%lld bytes) at %.1f s"), *Path, IFileManager::Get().FileSize(*Path), Now - StageStarted));
			++Shot;
			bRequested = false;
			return false;
		}
		return ++Frames > 600 ? Fail(TEXT("No screenshot was produced")) : false;
	}

	bool Ready(int32 Which) const
	{
		const FormationHeadingNeck::FSpread Spread = FormationHeadingNeck::MeasureSpread(*Force, Route);
		switch (Which)
		{
		case 0:
			return Spread.Index != INDEX_NONE && Spread.Index >= Route.NeckIndex - 3;
		case 1:
			return Spread.Index != INDEX_NONE && Spread.Index >= Route.NeckIndex;
		default:
			return Force->IsHoldingRegion() && Force->HoldRegionIndex == Route.Target && Force->HoldPostIndex != INDEX_NONE
				&& !Force->bHoldResponding && Force->GetUnits()[0]->GetVelocity().Size2D() < 5.f && Force->GetUnits()[5]->GetVelocity().Size2D() < 5.f;
		}
	}

	static FString Directory() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("FormationHeadingCaptures")); }

	static constexpr const TCHAR* Names[] = { TEXT("column-approach"), TEXT("column-neck"), TEXT("box-arrival") };
	static constexpr double NeckGap = 300.;

	FAutomationTestBase* Test;
	double Started, StageStarted = 0.;
	int32 Stage = 0, Shot = 0, Frames = 0;
	bool bRequested = false;
	ACommandGameState* State = nullptr;
	ACommandCamera* Camera = nullptr;
	TWeakObjectPtr<AArmyGroup> Force;
	FormationHeadingNeck::FRoute Route;
	TArray<TWeakObjectPtr<AActor>> Walls;
};
}

bool FFormationHeadingViewTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FormationHeadingView::FCapture(this));
	return true;
}

#endif
