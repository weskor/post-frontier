#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "CommandCamera.h"
#include "Commands/ConstructionCommandComponent.h"
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
			Check(PC->IsBuildHotkeyPending(), TEXT("B exposes the pending build prefix"));
			Key(PC, EKeys::F1);
			Check(!PC->IsBuildHotkeyPending(), TEXT("Any non-grid key ends the build prefix"));
			Key(PC, EKeys::W);
			Check(!PC->IsPlacingBuilding(), TEXT("W after a non-grid key remains a camera key"));
			Key(PC, EKeys::W, IE_Released);
			ACommandCamera* Camera = Cast<ACommandCamera>(PC->GetPawn());
			if (!Check(IsValid(Camera), TEXT("Local commander has a camera")))
				return true;
			const FVector FocusTarget = State->FriendlyHeadquarters->GetActorLocation();
			PC->bInitialFocusPending = false;
			Camera->SetActorLocation(FocusTarget + FVector(500.f, 500.f, 0.f));
			Key(PC, EKeys::B);
			Key(PC, EKeys::F);
			PC->PlayerTick(0.f);
			Check(!PC->IsBuildHotkeyPending() && !PC->IsPlacingBuilding()
					&& FVector::DistSquared2D(Camera->GetActorLocation(), FocusTarget) < 1.f,
				TEXT("F during pending B cancels the prefix and focuses the camera on the HQ"));
			Key(PC, EKeys::F, IE_Released);
			Key(PC, EKeys::B);
			Key(PC, EKeys::B);
			Check(!PC->IsBuildHotkeyPending(), TEXT("A second B ends the pending prefix"));
			Key(PC, EKeys::B);
			Key(PC, EKeys::Q);
			if (!Check(PC->IsPlacingBuilding() && PC->GetPlacementIndex() == 0, TEXT("B Q enters Barracks placement")))
				return true;
			const auto Context = CommandHUDPanels::MakeContext(PC);
			const auto Layout = CommandHUDPanels::MakeLayout(Context, 1280.f, 720.f);
			Check(CommandHUDPanels::IsPanelPoint(Context, Layout, CommandHUDPanels::BuildCard(Layout.Build, 1, 3).Center()),
				TEXT("Collapsed-deck build bar captures clicks instead of starting world selection"));
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
			const ACommandBuilding* Second = PC->GetSelectedBuilding();
			Check(!PC->IsPlacingBuilding() && IsValid(Second) && Second != First.Get() && Second->OwningPlayerState == Wallet,
				TEXT("Next unshifted placement selects the second owned building and ends mode"));
			Key(PC, EKeys::B);
			Key(PC, EKeys::W);
			Check(PC->IsPlacingBuilding() && PC->GetPlacementIndex() == 1, TEXT("B W enters Extractor placement"));
			// Arrange a network request still in flight when the user cancels.
			PC->bPlacementPending = true;
			PC->bSelectionDragging = true;
			Key(PC, EKeys::RightMouseButton);
			Stage = 1;
			return false;
		}
		if (Stage == 1)
		{
			if (PC->IsPlacingBuilding())
				return false;
			UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Navigation || Navigation->IsNavigationBuildInProgress())
				return false;
			Key(PC, EKeys::RightMouseButton, IE_Released);
			Check(!PC->IsPlacingBuilding(), TEXT("Right-click cancels Extractor placement"));
			Check(!PC->bSelectionDragging && PC->bPlacementPending,
				TEXT("Cancellation stops force box selection while retaining the in-flight placement"));
			Key(PC, EKeys::B);
			Key(PC, EKeys::Q);
			Check(!PC->IsPlacingBuilding() && PC->GetOrderFeedback() == TEXT("Waiting for placement confirmation."),
				TEXT("B Q cannot reopen placement while the cancelled request awaits its reply"));
			PC->HandleHUDAction(EHUDAction::BuildSlot0);
			Check(!PC->IsPlacingBuilding() && PC->GetOrderFeedback() == TEXT("Waiting for placement confirmation."),
				TEXT("Build bar action also refuses placement until the cancelled request completes"));
			FVector Point;
			if (!Check(FindPlacement(State, Point), TEXT("A third footprint exists for the late placement result")))
				return true;
			const ACommandBuilding* Selection = PC->GetSelectedBuilding();
			const int32 Balance = Wallet->Resources;
			const FCommandResult LateResult = FCommandService::PlaceBuilding(Wallet, 0, Point);
			if (!Check(LateResult.IsAccepted() && IsValid(LateResult.Building), TEXT("Late acceptance contains a real paid building")))
				return true;
			PC->HandleHUDAction(EHUDAction::Menu);
			PC->ConstructionCommands->ClientPlacementFeedback(LateResult.Message, LateResult.IsAccepted(), LateResult.Building, 0);
			Check(!PC->IsPlacingBuilding() && PC->GetSelectedBuilding() == Selection,
				TEXT("Late acceptance after cancellation neither selects its building nor reopens placement"));
			Check(Wallet->Resources == Balance - State->Content->Building(0)->BuildCost
					&& PC->GetOrderFeedback() == LateResult.Message && PC->GetFeedbackOpacity() == 1.f,
				TEXT("Cancelled paid placement still reports the authoritative result"));
			Check(PC->GetUIScreen() == ECommandScreen::Pause, TEXT("A late result preserves the paused menu"));
			PC->HandleHUDAction(EHUDAction::Resume);
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
			UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Navigation || Navigation->IsNavigationBuildInProgress())
				return false;
			Key(PC, EKeys::Escape, IE_Released);
			Check(!PC->IsPlacingBuilding() && PC->GetUIScreen() == ECommandScreen::Game, TEXT("Escape cancels without opening pause"));
			FVector Point;
			if (!Check(FindPlacement(State, Point), TEXT("An ownership-arrival footprint exists")))
				return true;
			Key(PC, EKeys::B);
			Key(PC, EKeys::Q);
			const ACommandBuilding* Selection = PC->GetSelectedBuilding();
			const FCommandResult Deferred = FCommandService::PlaceBuilding(Wallet, 0, Point);
			if (!Check(Deferred.IsAccepted() && IsValid(Deferred.Building), TEXT("Ownership-arrival result contains a real paid building")))
				return true;
			Deferred.Building->OwningPlayerState = nullptr;
			PC->ConstructionCommands->ClientPlacementFeedback(Deferred.Message, Deferred.IsAccepted(), Deferred.Building, 0);
			Check(!PC->IsPlacingBuilding() && PC->GetSelectedBuilding() == Selection,
				TEXT("Acceptance ends placement without selecting an actor before ownership arrives"));
			Deferred.Building->OwningPlayerState = Wallet;
			PC->PlayerTick(0.f);
			Check(PC->GetSelectedBuilding() == Deferred.Building,
				TEXT("Replicated ownership arrival selects the actual accepted building"));
			PC->SelectActor(First.Get());
			PC->PendingPlacedBuilding = Deferred.Building;
			Deferred.Building->OwningPlayerState = nullptr;
			PC->HandleHUDAction(EHUDAction::BuildSlot1);
			Deferred.Building->OwningPlayerState = Wallet;
			PC->PlayerTick(0.f);
			Check(PC->GetSelectedBuilding() == First.Get() && PC->IsPlacingBuilding() && PC->GetPlacementIndex() == 1,
				TEXT("A new build mode discards deferred selection from the previous placement"));
			PC->CancelMode();
			First->ConstructionProgress = 1.f;
			First->bForceConfigured = true;
			First->ForceGroup = ArmyTestSetup::SpawnGroup(World, PC, 0, ArmyTestSetup::FromFriendlyHQ(State, 1700.f, 600.f, 100.f));
			if (!Check(IsValid(First->ForceGroup), TEXT("Order-mode fixture has a live force")))
				return true;
			PC->SelectForce(First->ForceGroup);
			PC->PendingPlacedBuilding = Deferred.Building;
			Deferred.Building->OwningPlayerState = nullptr;
			PC->BeginForceAttack();
			Deferred.Building->OwningPlayerState = Wallet;
			PC->PlayerTick(0.f);
			Check(PC->IsForceSelected(First->ForceGroup) && !PC->GetSelectedBuilding() && PC->IsAssigningOrder(),
				TEXT("Ownership arrival cannot retarget selected forces during Attack targeting"));
			PC->CancelMode();
			CheckDeferredForceSelection(PC, Wallet, Deferred.Building, First->ForceGroup);
			Wallet->Resources = 0;
			Key(PC, EKeys::B);
			Key(PC, EKeys::Q);
			Check(!PC->IsPlacingBuilding() && PC->GetFeedbackOpacity() == 1.f, TEXT("Unaffordable build hotkey explains rejection without entering mode"));
			const auto Context = CommandHUDPanels::MakeContext(PC);
			const auto Layout = CommandHUDPanels::MakeLayout(Context, 1280.f, 720.f);
			Check(CommandHUDPanels::HitTest(Context, Layout, CommandHUDPanels::BuildCard(Layout.Build, 0, 3).Center())
					== EHUDAction::BuildSlot0,
				TEXT("Unaffordable build button remains a hit target for explanation"));
			FeedbackTime = World->GetRealTimeSeconds();
			Stage = 3;
			return false;
		}
		if (Stage == 6)
		{
			if (PC->IsBuildHotkeyPending())
				return false;
			Check(World->GetRealTimeSeconds() - PrefixTime >= 2., TEXT("Build prefix expires after two seconds"));
			Key(PC, EKeys::W);
			Check(!PC->IsPlacingBuilding(), TEXT("W after prefix expiry remains a camera key"));
			Key(PC, EKeys::W, IE_Released);
			return true;
		}
		const double Age = World->GetRealTimeSeconds() - FeedbackTime;
		if (Stage == 3 && Age >= 2.5)
		{
			Check(FMath::IsNearlyEqual(PC->GetFeedbackOpacity(), FMath::Clamp(static_cast<float>(4. - Age), 0.f, 1.f)),
				TEXT("Feedback opacity matches the sampled real-time age during the hold"));
			Stage = 4;
		}
		if (Stage == 4 && Age >= 3.25)
		{
			Check(FMath::IsNearlyEqual(PC->GetFeedbackOpacity(), FMath::Clamp(static_cast<float>(4. - Age), 0.f, 1.f)),
				TEXT("Feedback opacity matches the sampled real-time age during the fade"));
			Stage = 5;
		}
		if (Age >= 4.1)
		{
			Check(PC->GetFeedbackOpacity() == 0.f && PC->GetOrderFeedback().IsEmpty(), TEXT("Expired feedback clears after four seconds"));
			Wallet->Resources = 4000;
			Key(PC, EKeys::B);
			Check(PC->IsBuildHotkeyPending(), TEXT("Build prefix starts a fresh two-second window"));
			PrefixTime = World->GetRealTimeSeconds();
			Stage = 6;
			return false;
		}
		return false;
	}
private:
	void CheckDeferredForceSelection(ACommandPlayerController* PC, ACommandPlayerState* Wallet,
		ACommandBuilding* Building, AArmyGroup* Force)
	{
		Force->ForceNumber = 1;
		for (bool bBox : { false, true })
		{
			PC->SelectForce(Force);
			PC->HandleHUDAction(EHUDAction::BuildSlot0);
			Building->OwningPlayerState = nullptr;
			PC->ConstructionCommands->ClientPlacementFeedback(TEXT("Building placed; construction started."), true, Building, 0);
			if (bBox)
			{
				int32 Width, Height;
				PC->GetViewportSize(Width, Height);
				PC->SelectForceBox(FVector2D::ZeroVector, FVector2D(Width, Height), true);
			}
			else
				PC->SelectForceNumber(Force->ForceNumber);
			Building->OwningPlayerState = Wallet;
			PC->PlayerTick(0.f);
			Check(PC->IsForceSelected(Force) && PC->GetSelectedForces().Num() == 1 && !PC->GetSelectedBuilding(),
				bBox ? TEXT("Ownership arrival cannot replace additive box force selection with the deferred building")
					 : TEXT("Ownership arrival cannot replace numbered force selection with the deferred building"));
		}
	}
	bool Check(bool bCondition, const TCHAR* Message) { return Test->TestTrue(Message, bCondition); }
	FAutomationTestBase* Test;
	double Started;
	double FeedbackTime = 0.;
	double PrefixTime = 0.;
	int32 Stage = 0;
	TWeakObjectPtr<ACommandBuilding> First;
};

bool FBuildBarTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBuildBarScenario(this));
	return true;
}

#endif
