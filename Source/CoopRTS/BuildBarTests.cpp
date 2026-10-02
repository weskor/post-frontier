#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "HUD/HUDPanels.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "NavigationSystem.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildBarTest, "CoopRTS.HUD.BuildBar",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
void Key(ACommandPlayerController* Controller, FKey Value, EInputEvent Event = IE_Pressed)
{
	const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
	Controller->InputKey(FInputKeyEventArgs(nullptr, Device, Value, Event, FPlatformTime::Cycles64()));
}

bool FindPlacement(ACommandGameState* State, FVector& Result)
{
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->GetWorld());
	if (!Navigation || Navigation->IsNavigationBuildInProgress())
		return false;
	const FVector Center = State->FriendlyHeadquarters->GetActorLocation();
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			const FVector Point = State->ResolveBuildingLocation(0,
				Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f));
			FString Reason;
			FNavLocation Ground;
			if (State->ValidateBuildingPlacement(0, 0, Point, Reason)
				&& Navigation->ProjectPointToNavigation(Point, Ground, FVector(45.f, 45.f, 200.f)))
			{
				Result = Point;
				return true;
			}
		}
	return false;
}

class FBuildBarScenario : public IAutomationLatentCommand
{
public:
	explicit FBuildBarScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 40.)
		{
			Test->AddError(TEXT("Build bar scenario exceeded 40 seconds."));
			return true;
		}
		UWorld* World = ArmyTestSetup::World();
		ACommandPlayerController* PC = World ? ArmyTestSetup::Controller(World) : nullptr;
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		ACommandPlayerState* Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!PC || !Wallet || Wallet->CommanderIndex < 0 || !ArmyTestSetup::MapReady(State) || !State->Content)
			return false;
		if (Stage == 0)
		{
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->Destroy();
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				if (It->TeamIndex == 5)
					It->Destroy();
			State->bVerificationIncomePaused = true;
			Wallet->Resources = 4000;
			FVector Point;
			if (!FindPlacement(State, Point))
				return false;
			Key(PC, EKeys::B);
			Key(PC, EKeys::Q);
			if (!Check(PC->IsPlacingBuilding() && PC->GetPlacementIndex() == 0, TEXT("B Q enters Barracks placement")))
				return true;
			const auto Context = CommandHUDPanels::MakeContext(PC);
			const auto Layout = CommandHUDPanels::MakeLayout(Context, 1280.f, 720.f);
			Check(CommandHUDPanels::HitTest(Context, Layout, CommandHUDPanels::BuildCard(Layout.Build, 1, 3).Center())
					== EHUDAction::BuildSlot1,
				TEXT("Build bar remains clickable during placement"));
			const int32 Balance = Wallet->Resources;
			PC->PlaceBuildingAt(ArmyTestSetup::OutsideArena(State), false);
			Check(PC->IsPlacingBuilding() && Wallet->Resources == Balance && PC->GetFeedbackOpacity() == 1.f,
				TEXT("Invalid ground keeps placement open, preserves funds and displays feedback"));
			PC->PlaceBuildingAt(Point, true);
			Check(PC->IsPlacingBuilding(), TEXT("Shift submission retains placement after acceptance"));
			First = PC->GetSelectedBuilding();
			Check(First.IsValid() && First->OwningPlayerState == Wallet, TEXT("Accepted Shift placement selects its new owned building"));
			Check(Wallet->Resources == Balance - State->Content->Building(0)->BuildCost, TEXT("Shift placement charges exactly one building"));
			if (!FindPlacement(State, Point))
			{
				Test->AddError(TEXT("No second build footprint."));
				return true;
			}
			PC->PlaceBuildingAt(Point, false);
			Check(!PC->IsPlacingBuilding() && PC->GetSelectedBuilding() != First.Get(), TEXT("Next unshifted placement selects the second building and ends mode"));
			Key(PC, EKeys::B);
			Key(PC, EKeys::W);
			Check(PC->IsPlacingBuilding() && PC->GetPlacementIndex() == 1, TEXT("B W enters Extractor placement"));
			Key(PC, EKeys::RightMouseButton);
			Stage = 1;
			return false;
		}
		if (Stage == 1)
		{
			if (PC->IsPlacingBuilding())
				return false;
			Key(PC, EKeys::RightMouseButton, IE_Released);
			Check(!PC->IsPlacingBuilding(), TEXT("Right-click cancels Extractor placement"));
			Key(PC, EKeys::B);
			Key(PC, EKeys::E);
			Check(PC->IsPlacingBuilding() && PC->GetPlacementIndex() == 2, TEXT("B E enters Workshop placement"));
			Key(PC, EKeys::Escape);
			Stage = 2;
			return false;
		}
		if (Stage == 2)
		{
			if (PC->IsPlacingBuilding())
				return false;
			Key(PC, EKeys::Escape, IE_Released);
			Check(!PC->IsPlacingBuilding() && PC->GetUIScreen() == ECommandScreen::Game, TEXT("Escape cancels without opening pause"));
			Wallet->Resources = 0;
			Key(PC, EKeys::B);
			Key(PC, EKeys::Q);
			Check(!PC->IsPlacingBuilding() && PC->GetFeedbackOpacity() == 1.f, TEXT("Unaffordable build hotkey explains rejection without entering mode"));
			FVector2D Button;
			ACommandHUD* HUD = Cast<ACommandHUD>(PC->GetHUD());
			if (!Check(HUD && HUD->FindActionScreenPosition(EHUDAction::BuildSlot0, Button), TEXT("Greyed-out build button remains reachable")))
				return true;
			PC->HandleHUDClick(Button);
			Check(!PC->IsPlacingBuilding() && Wallet->Resources == 0 && PC->GetFeedbackOpacity() == 1.f,
				TEXT("Clicking unaffordable button explains rejection without spending or placement"));
			FeedbackTime = World->GetRealTimeSeconds();
			Stage = 3;
			return false;
		}
		const double Age = World->GetRealTimeSeconds() - FeedbackTime;
		if (Stage == 3 && Age >= 2.5)
		{
			Check(Age < 3. && PC->GetFeedbackOpacity() == 1.f, TEXT("Feedback stays opaque during three-second hold"));
			Stage = 4;
		}
		if (Stage == 4 && Age >= 3.25)
		{
			Check(Age < 4. && PC->GetFeedbackOpacity() > 0.f && PC->GetFeedbackOpacity() < 1.f,
				TEXT("Feedback fades between three and four seconds"));
			Stage = 5;
		}
		if (Age >= 4.1)
		{
			Check(PC->GetFeedbackOpacity() == 0.f && PC->GetOrderFeedback().IsEmpty(), TEXT("Expired feedback clears after four seconds"));
			return true;
		}
		return false;
	}
private:
	bool Check(bool bCondition, const TCHAR* Message) { return Test->TestTrue(Message, bCondition); }
	FAutomationTestBase* Test;
	double Started;
	double FeedbackTime = 0.;
	int32 Stage = 0;
	TWeakObjectPtr<ACommandBuilding> First;
};
}

bool FBuildBarTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBuildBarScenario(this));
	return true;
}

#endif
