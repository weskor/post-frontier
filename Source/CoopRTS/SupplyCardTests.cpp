#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "SupplyDeliveryFixture.h"

#include "CommandHUD.h"
#include "HUD/ForceBar.h"
#include "HUD/HUDPanels.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// The force card of a cut-off force: a CUT OFF chip in its header and the refill line
// "Refill: HELD · cut off · 1 recruit waiting" (ui.md, Step 1b surface 2), rendered and captured.

namespace SupplyTests
{
class FCardScenario : public FScenarioBase
{
public:
	using FScenarioBase::FScenarioBase;

protected:
	bool Run() override
	{
		if (Stage < 2)
		{
			if (!PutForceInFar())
				return false;
			PC = ArmyTestSetup::Controller(GameWorld);
			if (!Check(PC && Produce() == UnitCost(), TEXT("A recruit is paid and in transit to the force in Far")))
				return true;
			SetStage(2);
		}
		switch (Stage)
		{
		case 2:
			return InTransit();
		case 3:
			return Cut();
		case 4:
			return Capture();
		default:
			return Captured();
		}
	}

private:
	FString CardLine()
	{
		using namespace CommandHUDPanels;
		const FContext Context = MakeContext(PC);
		FForceCard Card;
		ReadForceCard(Context, *Force, INDEX_NONE, Card);
		return Card.Production.ToString();
	}
	bool InTransit()
	{
		// A recruit on its way is counted apart from the joined strength and shows no cut-off state.
		const FString Line = CardLine();
		if (!Check(Line.EndsWith(TEXT("\u00B7 1 travelling")) && !Line.Contains(TEXT("HELD")) && !Force->bSupplyCutOff,
				*FString::Printf(TEXT("An in-transit recruit reads as travelling: '%s'"), *Line)))
			return true;
		Fixture->SetController(Neck, 5);
		SetStage(3);
		return false;
	}
	bool Cut()
	{
		if (Force->RecruitsWaiting != 1)
			return !Check(StageSeconds() < 2., TEXT("Cutting the neck holds the recruit at the producer"));
		const FString Line = CardLine();
		if (!Check(Force->bSupplyCutOff && Line == TEXT("Refill: HELD \u00B7 cut off \u00B7 1 recruit waiting"),
				*FString::Printf(TEXT("A cut-off force's card reads the HELD refill line: '%s'"), *Line)))
			return true;
		SetStage(4);
		return false;
	}
	bool Capture()
	{
		// Let the HUD draw the cut-off card for a few frames before the screenshot.
		if (StageSeconds() < .5)
			return false;
		FString Directory = FPaths::ProjectSavedDir() / TEXT("Screenshots");
		FString Log;
		if (FParse::Value(FCommandLine::Get(), TEXT("abslog="), Log))
			Directory = FPaths::GetPath(Log);
		Path = FPaths::ConvertRelativePathToFull(Directory / TEXT("force-card-cut-off.png"));
		IFileManager::Get().Delete(*Path);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		SetStage(5);
		return false;
	}
	bool Captured()
	{
		if (IFileManager::Get().FileSize(*Path) > 0)
		{
			Test->AddInfo(FString::Printf(TEXT("Rendered force card capture: %s"), *Path));
			return true;
		}
		return !Check(StageSeconds() < 10., TEXT("The rendered capture of the cut-off card is written"));
	}

	ACommandPlayerController* PC = nullptr;
	FString Path;
};
}

using namespace SupplyTests;
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSupplyCardTest, "CoopRTS.HUD.SupplyCard.CutOffAndHeld",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSupplyCardTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCardScenario(this));
	return true;
}
#endif
