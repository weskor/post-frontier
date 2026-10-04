#include "CommandPlayerController.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "CommandMenuGameMode.h"
#include "CoopAudioSubsystem.h"
#include "CoopSessionSubsystem.h"
#include "Commands/MatchCommandComponent.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
bool IsPrimaryScreen(ECommandScreen Screen)
{
	return Screen == ECommandScreen::MainMenu || Screen == ECommandScreen::Pause || Screen == ECommandScreen::Result;
}
}

bool ACommandPlayerController::IsMatchTerminal() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return State && State->MatchResult != EMatchResult::Ongoing;
}

bool ACommandPlayerController::CanIssueGameplayCommand()
{
	if (GetUIScreen() == ECommandScreen::Game)
		return true;
	SetFeedback(TEXT("Match over: press Enter to restart."));
	return false;
}

bool ACommandPlayerController::IsMenuWorld() const
{
	return GetWorld() && GetWorld()->GetAuthGameMode<ACommandMenuGameMode>() != nullptr;
}

ECommandScreen ACommandPlayerController::GetUIScreen() const
{
	return Screen == ECommandScreen::Game && IsMatchTerminal() ? ECommandScreen::Result : Screen;
}

float ACommandPlayerController::GetMasterVolume() const
{
	const UGameInstance* Instance = GetGameInstance();
	const UCoopAudioSubsystem* Audio = Instance ? Instance->GetSubsystem<UCoopAudioSubsystem>() : nullptr;
	return Audio ? Audio->GetMasterVolume() : 1.f;
}

void ACommandPlayerController::RequestRestart()
{
	if (GetUIScreen() == ECommandScreen::Result)
		MatchCommands->ServerRequestRestart();
	else if (GetUIScreen() == ECommandScreen::MainMenu)
		HandleHUDAction(EHUDAction::PlaySolo);
}

void ACommandPlayerController::ToggleActivePause()
{
	if (GetUIScreen() != ECommandScreen::Game)
		return;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (State && State->IsActivePaused())
		MatchCommands->ServerResume();
	else
		MatchCommands->ServerPause();
}

void ACommandPlayerController::ShowScreen(ECommandScreen NewScreen)
{
	Screen = NewScreen;
	PendingPan = FVector2D::ZeroVector;
	bDragging = false;
	bSelectionDragging = false;
	// Local menu overlays must never pause a listen host or its remote commanders.
	if (GetNetMode() == NM_Standalone && !IsMenuWorld())
		if (ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>())
			State->RefreshSoloMenuPause(this, NewScreen != ECommandScreen::Game && NewScreen != ECommandScreen::Result);
	UE_LOG(LogTemp, Display, TEXT("Solo screen=%d paused=%d"), static_cast<int32>(GetUIScreen()), GetWorld()->IsPaused());
}

void ACommandPlayerController::Escape()
{
	if (bPlacingBuilding || bAssigningOrder || bFortifyTargeting || bBuildHotkeyPending)
	{
		CancelPointerMode();
		return;
	}
	const ECommandScreen Current = GetUIScreen();
	if (Current == ECommandScreen::Game)
		ShowScreen(ECommandScreen::Pause);
	else if (Current == ECommandScreen::Pause)
		ShowScreen(ECommandScreen::Game);
	else if (Current == ECommandScreen::MainMenu)
	{
		ReturnScreen = Current;
		ShowScreen(ECommandScreen::ConfirmQuit);
	}
	else if (Current != ECommandScreen::Result)
		ShowScreen(ReturnScreen);
	PlayUISound(TEXT("Click"));
}

bool ACommandPlayerController::HandleScreenAction(EHUDAction Action)
{
	const bool bMapAction = Action == EHUDAction::MapV2 || Action == EHUDAction::MapClassic;
	if (!bMapAction && (static_cast<uint8>(Action) < static_cast<uint8>(EHUDAction::PlaySolo) || static_cast<uint8>(Action) > static_cast<uint8>(EHUDAction::InviteFriends)))
		return false;
	if (bTravelPending)
		return true;
	const ECommandScreen Current = GetUIScreen();
	bool bApplied;
	switch (Action)
	{
	case EHUDAction::MapV2:
	case EHUDAction::MapClassic:
	case EHUDAction::PlaySolo:
	case EHUDAction::HostCoop:
		bApplied = ApplyMenuWorldAction(Action, Current);
		break;
	case EHUDAction::InviteFriends:
	case EHUDAction::Menu:
	case EHUDAction::Resume:
	case EHUDAction::Controls:
	case EHUDAction::Audio:
	case EHUDAction::Back:
	case EHUDAction::MainMenu:
	case EHUDAction::Quit:
		bApplied = ApplyNavigationAction(Action, Current);
		break;
	default:
		bApplied = ApplyLeaveAction(Action, Current);
		break;
	}
	if (bApplied)
		PlayUISound(TEXT("Click"));
	return true;
}

bool ACommandPlayerController::ApplyMenuWorldAction(EHUDAction Action, ECommandScreen Current)
{
	if (Current != ECommandScreen::MainMenu || !IsMenuWorld())
		return false;
	UCoopSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UCoopSessionSubsystem>();
	switch (Action)
	{
	case EHUDAction::MapV2:
	case EHUDAction::MapClassic:
		if (Session)
			Session->SelectMap(Action == EHUDAction::MapV2);
		return true;
	case EHUDAction::PlaySolo:
		if (Session && Session->IsBusy())
			return false;
		bTravelPending = true;
		UGameplayStatics::OpenLevel(this, Session ? Session->GetSelectedMap() : FName(TEXT("/Game/Maps/AvailabilityZoneV2")));
		return true;
	case EHUDAction::HostCoop:
		if (Session)
			Session->Host();
		return true;
	default:
		return false;
	}
}

bool ACommandPlayerController::ApplyNavigationAction(EHUDAction Action, ECommandScreen Current)
{
	UCoopSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UCoopSessionSubsystem>();
	switch (Action)
	{
	case EHUDAction::InviteFriends:
		if (Current != ECommandScreen::Pause && Current != ECommandScreen::Result)
			return false;
		if (Session)
			Session->Invite();
		return true;
	case EHUDAction::Menu:
		if (Current != ECommandScreen::Game)
			return false;
		CancelMode();
		ShowScreen(ECommandScreen::Pause);
		return true;
	case EHUDAction::Resume:
		if (Current != ECommandScreen::Pause)
			return false;
		ShowScreen(ECommandScreen::Game);
		return true;
	case EHUDAction::Controls:
	case EHUDAction::Audio:
		if (!IsPrimaryScreen(Current))
			return false;
		ReturnScreen = Current;
		ShowScreen(Action == EHUDAction::Controls ? ECommandScreen::Controls : ECommandScreen::Audio);
		return true;
	case EHUDAction::Back:
		if (Current != ECommandScreen::Controls && Current != ECommandScreen::Audio
			&& Current != ECommandScreen::ConfirmLeave && Current != ECommandScreen::ConfirmQuit)
			return false;
		ShowScreen(ReturnScreen);
		return true;
	case EHUDAction::MainMenu:
	case EHUDAction::Quit:
		if (!IsPrimaryScreen(Current) || (Action == EHUDAction::MainMenu && IsMenuWorld()))
			return false;
		ReturnScreen = Current;
		ShowScreen(Action == EHUDAction::MainMenu ? ECommandScreen::ConfirmLeave : ECommandScreen::ConfirmQuit);
		return true;
	default:
		return false;
	}
}

bool ACommandPlayerController::ApplyLeaveAction(EHUDAction Action, ECommandScreen Current)
{
	UCoopSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UCoopSessionSubsystem>();
	switch (Action)
	{
	case EHUDAction::ConfirmLeave:
		if (Current != ECommandScreen::ConfirmLeave)
			return false;
		SetPause(false);
		if (Session)
			Session->Leave();
		else
			UGameplayStatics::OpenLevel(this, TEXT("/Game/Maps/Menu"));
		return true;
	case EHUDAction::ConfirmQuit:
		if (Current != ECommandScreen::ConfirmQuit)
			return false;
		if (Session)
			Session->Leave(true);
		else
			UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
		return true;
	case EHUDAction::Restart:
		if (Current != ECommandScreen::Result)
			return false;
		SetPause(false);
		RequestRestart();
		return true;
	case EHUDAction::VolumeDown:
	case EHUDAction::VolumeUp:
		if (Current != ECommandScreen::Audio)
			return false;
		if (UGameInstance* Instance = GetGameInstance())
			if (UCoopAudioSubsystem* Audio = Instance->GetSubsystem<UCoopAudioSubsystem>())
				Audio->SetMasterVolume(FMath::Clamp(Audio->GetMasterVolume() + (Action == EHUDAction::VolumeUp ? .1f : -.1f), 0.f, 1.f));
		return true;
	default:
		return false;
	}
}
