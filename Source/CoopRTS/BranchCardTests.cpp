#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "BranchFixture.h"

#include "CommandHUD.h"
#include "HAL/FileManager.h"
#include "HUD/ForceBar.h"
#include "HUD/HUDPanels.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// The tier-2 branch on screen (ui.md surface 5), rendered and captured: the unlocked type picker grid, the
// TIER 2 BRANCH button short of Data and ready, the upgrade bar with its pill and the paused production, and the
// force card's REFIT chip and line. Geometry is asserted through the shared layout; the captures are for inspection.

namespace BranchTests
{
class FPanelScenario : public FBranchScenario
{
public:
	using FBranchScenario::FBranchScenario;

protected:
	bool Run() override
	{
		if (Stage < 2)
		{
			if (!PutForceInFar())
				return false;
			SetStage(2);
		}
		switch (Stage)
		{
		case 2:
			Produce();
			Produce();
			SetStage(3);
			return false;
		case 3:
			return Gathered();
		case 4:
			return Picker();
		case 5:
			return NeedData();
		case 6:
			return Ready();
		case 7:
			return Upgrading();
		case 8:
			return Refitting();
		default:
			return Emergency();
		}
	}

private:
	bool Gathered()
	{
		if (Joined() < 3)
			return !Check(StageSeconds() < 12., TEXT("Both recruits reach the force"));
		PC = ArmyTestSetup::Controller(GameWorld);
		HUD = PC ? Cast<ACommandHUD>(PC->GetHUD()) : nullptr;
		Fresh = Fixture->SpawnBarracks(0, 1.f, 1);
		if (!Check(PC && HUD && Fresh.IsValid(), TEXT("The rendered scope has a controller, a HUD and a second, unlocked Barracks")))
			return true;
		PC->SelectActorWithModifiers(Fresh.Get(), false, false);
		SetStage(4);
		return false;
	}
	// Chips of the unlocked picker: 28 px at least, in two columns, inside the FORCE TYPE column and apart.
	bool Picker()
	{
		using namespace CommandHUDPanels;
		const FContext Context = MakeContext(PC);
		int32 Width, Height;
		PC->GetViewportSize(Width, Height);
		const FLayout Layout = MakeLayout(Context, Width, Height);
		const FRect Recipes = Column(Layout.Inspector, 0, 3);
		TArray<FRect> Chips;
		ForEachButton(Context, Layout, [&](const FButton& Button) {
			if (RecipeSlot(Button.Action) != INDEX_NONE)
				Chips.Add(Button.Rect);
		});
		bool bClean = Chips.Num() == RecipeCount(Context) && Chips.Num() > 0;
		for (int32 Index = 0; Index < Chips.Num(); ++Index)
		{
			bClean &= Chips[Index].H >= 28.f && Chips[Index].X >= Recipes.X && Chips[Index].Right() <= Recipes.Right() + .5f && Chips[Index].Bottom() <= Recipes.Bottom();
			for (int32 Other = Index + 1; Other < Chips.Num(); ++Other)
				bClean &= !Chips[Index].Intersects(Chips[Other]);
		}
		if (!Check(PC->GetSelectedBuilding() == Fresh.Get() && bClean, TEXT("The unlocked picker lists every base type as non-overlapping chips of at least 28 px")))
			return true;
		if (!Shot(TEXT("branch-picker")))
			return false;
		Fresh->Destroy();
		PC->SelectActorWithModifiers(Producer.Get(), false, false);
		Wallet->Resources = 10000;
		Wallet->Data = 0;
		SetStage(5);
		return false;
	}
	bool NeedData()
	{
		using namespace CommandHUDPanels;
		BranchPolicy::FDecision Decision;
		if (!Check(ReadBranchArea(MakeContext(PC), Decision) == EBranchArea::Button && Decision.Verdict == BranchPolicy::EVerdict::NeedResources && Decision.DataShort == 50,
				TEXT("The locked Barracks shows the button, short of 50 Data")))
			return true;
		if (!Shot(TEXT("branch-need-data")))
			return false;
		FVector2D Click;
		if (!Check(HUD->FindActionScreenPosition(EHUDAction::BranchPurchase, Click) && PC->HandleHUDClick(Click), TEXT("The greyed button is clickable")))
			return true;
		if (!Check(PC->GetOrderFeedback() == TEXT("Need 50 more Data") && Wallet->Resources == 10000 && Producer->Branch.Phase == EBranchPhase::None,
				*FString::Printf(TEXT("The click explains and spends nothing: '%s'"), *PC->GetOrderFeedback())))
			return true;
		Wallet->Data = 200;
		SetStage(6);
		return false;
	}
	bool Ready()
	{
		using namespace CommandHUDPanels;
		FForceCard Card;
		ReadForceCard(MakeContext(PC), *Force, INDEX_NONE, Card);
		if (!Check(Card.bBranchAffordable && !Card.bRefitting, TEXT("The force card shows the T2 chip while the branch is affordable")))
			return true;
		if (!Shot(TEXT("branch-ready")))
			return false;
		FVector2D Click;
		if (!Check(HUD->FindActionScreenPosition(EHUDAction::BranchPurchase, Click) && PC->HandleHUDClick(Click)
					&& Producer->IsUpgrading() && Wallet->Resources == 9900 && Wallet->Data == 150,
				TEXT("Clicking the ready button buys the branch: 100 Power and 50 Data")))
			return true;
		SetStage(7);
		return false;
	}
	bool Upgrading()
	{
		using namespace CommandHUDPanels;
		const FContext Context = MakeContext(PC);
		int32 Width, Height;
		PC->GetViewportSize(Width, Height);
		bool bToggleBlocked = false, bButton = false;
		ForEachButton(Context, MakeLayout(Context, Width, Height), [&](const FButton& Button) {
			bToggleBlocked |= Button.Action == EHUDAction::ToggleProduction && Button.Block == EBlock::Upgrading;
			bButton |= Button.Action == EHUDAction::BranchPurchase;
		});
		FForceCard Card;
		ReadForceCard(Context, *Force, INDEX_NONE, Card);
		if (!Check(bToggleBlocked && !bButton && !Card.bBranchAffordable, TEXT("While upgrading the button is gone, the pause toggle is disabled and the T2 chip is not offered")))
			return true;
		// Let the bar fill to about 12 of its 20 s before the capture.
		if (StageSeconds() < 12.)
			return false;
		if (!Shot(TEXT("branch-upgrading")))
			return false;
		SetStage(8);
		return false;
	}
	bool Refitting()
	{
		using namespace CommandHUDPanels;
		if (Producer->IsUpgrading() || Branched() < 1)
			return !Check(StageSeconds() < 40., TEXT("The upgrade finishes and the first member refits"));
		FForceCard Card;
		ReadForceCard(MakeContext(PC), *Force, INDEX_NONE, Card);
		if (!Check(Card.bRefitting && FString(Card.RefitLine.ToView()).StartsWith(TEXT("Refit 1/3 \u2192 Warden")), TEXT("The card reads the refit line with its progress")))
			return true;
		if (!Shot(TEXT("branch-refit")))
			return false;
		// A human free force is the emergency force of an offline HQ: its card says so.
		const FVector Anchor = State->GetRegionAnchor(Far) + FVector(600.f, 0.f, 0.f);
		EmergencyForce = AArmyGroup::SpawnFreeForce(*GameWorld, *Wallet, Anchor, TArray<int32>{ BaseIndex(), BaseIndex() }, 4, 1.f);
		if (!Check(EmergencyForce.IsValid(), TEXT("An emergency force spawns for the commander")))
			return true;
		SetStage(9);
		return false;
	}
	bool Emergency()
	{
		using namespace CommandHUDPanels;
		FForceCard Card;
		ReadForceCard(MakeContext(PC), *EmergencyForce, INDEX_NONE, Card);
		if (!Check(FString(Card.Title.ToView()) == TEXT("4  EMERGENCY Brawler") && FString(Card.Production.ToView()).StartsWith(TEXT("Emergency force"))
					&& !Card.bRefitting && !Card.bBranchAffordable,
				*FString::Printf(TEXT("The emergency force's card is named EMERGENCY: '%s' / '%s'"), *FString(Card.Title.ToView()), *FString(Card.Production.ToView()))))
			return true;
		return Shot(TEXT("branch-emergency"));
	}

	// Lets the HUD draw for a moment, requests a screenshot and waits for the file; true once it is written.
	bool Shot(const TCHAR* Name)
	{
		if (!bShotRequested)
		{
			if (StageSeconds() < 1.)
				return false;
			FString Directory = FPaths::ProjectSavedDir() / TEXT("Screenshots");
			FString Log;
			if (FParse::Value(FCommandLine::Get(), TEXT("abslog="), Log))
				Directory = FPaths::GetPath(Log);
			ShotPath = FPaths::ConvertRelativePathToFull(Directory / (FString(Name) + TEXT(".png")));
			IFileManager::Get().Delete(*ShotPath);
			FScreenshotRequest::RequestScreenshot(ShotPath, false, false);
			bShotRequested = true;
			ShotStarted = FPlatformTime::Seconds();
			return false;
		}
		if (IFileManager::Get().FileSize(*ShotPath) > 0)
		{
			Test->AddInfo(FString::Printf(TEXT("Rendered branch capture: %s"), *ShotPath));
			bShotRequested = false;
			return true;
		}
		Check(FPlatformTime::Seconds() - ShotStarted < 10., TEXT("The rendered capture is written"));
		return false;
	}

	ACommandPlayerController* PC = nullptr;
	ACommandHUD* HUD = nullptr;
	TWeakObjectPtr<ACommandBuilding> Fresh;
	TWeakObjectPtr<AArmyGroup> EmergencyForce;
	FString ShotPath;
	bool bShotRequested = false;
	double ShotStarted = 0.;
};
}

using namespace BranchTests;
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBranchPanelTest, "CoopRTS.HUD.Branch.Panel",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FBranchPanelTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPanelScenario(this));
	return true;
}
#endif
