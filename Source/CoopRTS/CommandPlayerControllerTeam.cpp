#include "CommandPlayerController.h"

#include "CommandGameState.h"
#include "CommandHUD.h"
#include "Commands/GiftCommandComponent.h"
#include "Rules/ControllerInputPolicy.h"

namespace
{
using TeammateList = TArray<const ACommandPlayerState*, TInlineAllocator<TeamPanelPolicy::MaxTeammates>>;

int32 Balance(const ACommandPlayerState& Wallet, TeamPanelPolicy::EResource Resource)
{
	return Resource == TeamPanelPolicy::EResource::Power ? Wallet.Resources : Wallet.Data;
}
}

void ACommandPlayerController::ToggleTeamPanel()
{
	if (GetUIScreen() != ECommandScreen::Game)
		return;
	TeamPanelPolicy::Toggle(TeamFlow);
	PlayUISound(TEXT("Click"));
}

void ACommandPlayerController::OpenTeamPanelFor(int32 Slot)
{
	if (GetUIScreen() == ECommandScreen::Game)
		TeamPanelPolicy::OpenFor(TeamFlow, Slot);
}

int32 ACommandPlayerController::TeammateSlotOfRow(int32 Row) const
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	TeammateList Mates;
	if (State)
		UGiftCommandComponent::Teammates(*State, GetPlayerState<ACommandPlayerState>(), Mates);
	return Mates.IsValidIndex(Row) ? Mates[Row]->CommanderIndex : INDEX_NONE;
}

void ACommandPlayerController::UpdateTeamPanel()
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
		return;
	TeamPanelPolicy::FLogBuffer Log;
	UGiftCommandComponent::ReadLog(*State, Log);
	TeamPanelPolicy::ClampLog(TeamFlow, Log.Num());
	// Everything logged while the panel is open has been seen.
	if (TeamFlow.bOpen)
		GiftSeenThrough = TeamPanelPolicy::LatestTime(Log, GiftSeenThrough);
}

bool ACommandPlayerController::HasUnseenGift() const
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const ACommandPlayerState* Wallet = GetPlayerState<ACommandPlayerState>();
	if (!State || !Wallet || TeamFlow.bOpen)
		return false;
	TeamPanelPolicy::FLogBuffer Log;
	UGiftCommandComponent::ReadLog(*State, Log);
	return TeamPanelPolicy::HasUnseenGift(Log, Wallet->CommanderIndex, GiftSeenThrough);
}

void ACommandPlayerController::SetTeamRefusal(const FString& Text)
{
	TeamRefusal = Text;
	TeamRefusalStarted = GetWorld()->GetRealTimeSeconds();
}

bool ACommandPlayerController::GetTeamRefusal(FString& OutText, float& OutOpacity) const
{
	OutOpacity = TeamRefusal.IsEmpty()
		? 0.f
		: ControllerInputPolicy::FeedbackOpacity(GetWorld()->GetRealTimeSeconds() - TeamRefusalStarted);
	OutText = TeamRefusal;
	return OutOpacity > 0.f;
}

void ACommandPlayerController::SendGift()
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const ACommandPlayerState* Wallet = GetPlayerState<ACommandPlayerState>();
	if (!State || !Wallet || bGiftPending)
		return;
	const TeamPanelPolicy::FSendInput In = UGiftCommandComponent::MakeSendInput(
		*State, Wallet, TeamFlow.Teammate, TeamFlow.Resource, TeamFlow.Amount);
	const TeamPanelPolicy::ESendVerdict Verdict = TeamPanelPolicy::Verdict(In);
	if (Verdict != TeamPanelPolicy::ESendVerdict::Ok)
	{
		TStringBuilder<96> Reason;
		TeamPanelPolicy::AppendReason(Reason, Verdict, In);
		SetTeamRefusal(FString(Reason.ToView()));
		PlayUISound(TEXT("Reject"));
		return;
	}
	TeammateList Mates;
	UGiftCommandComponent::Teammates(*State, Wallet, Mates);
	for (const ACommandPlayerState* Recipient : Mates)
		if (Recipient->CommanderIndex == TeamFlow.Teammate)
		{
			TStringBuilder<96> Sent;
			Sent.Appendf(TEXT("Sent %d %s to "), TeamFlow.Amount, TeamPanelPolicy::ResourceName(TeamFlow.Resource));
			TeamPanelPolicy::AppendCommander(Sent, TeamFlow.Teammate);
			PendingGiftText = FString(Sent.ToView());
			TeamRefusal.Reset();
			bGiftPending = true;
			GiftCommands->ServerGift(const_cast<ACommandPlayerState*>(Recipient), TeamFlow.Teammate,
				UGiftCommandComponent::FromPolicy(TeamFlow.Resource), TeamFlow.Amount);
			return;
		}
}

void ACommandPlayerController::CompleteGiftInput(const FString& Message, bool bAccepted)
{
	bGiftPending = false;
	if (bAccepted)
	{
		SetCommandFeedback(PendingGiftText, true);
		return;
	}
	SetTeamRefusal(Message);
	PlayUISound(TEXT("Reject"));
}

bool ACommandPlayerController::HandleTeamPanelAction(EHUDAction Action)
{
	using namespace TeamPanelPolicy;
	const uint8 Ordinal = static_cast<uint8>(Action);
	if (Ordinal < static_cast<uint8>(EHUDAction::TeamToggle) || Ordinal > static_cast<uint8>(EHUDAction::TeamLogDown))
		return false;
	if (Action == EHUDAction::TeamToggle)
	{
		ToggleTeamPanel();
		return true;
	}
	if (Action == EHUDAction::TeamClose)
	{
		TeamFlow.bOpen = false;
		PlayUISound(TEXT("Click"));
		return true;
	}
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const ACommandPlayerState* Wallet = GetPlayerState<ACommandPlayerState>();
	if (!TeamFlow.bOpen || !State || !Wallet)
		return true;
	// A refusal explains the last Send; any change to the flow outdates it.
	if (Action != EHUDAction::TeamSend)
		TeamRefusal.Reset();
	const int32 Funds = Balance(*Wallet, TeamFlow.Resource);
	const int32 Row = Ordinal - static_cast<uint8>(EHUDAction::TeamRow0);
	const int32 Preset = Ordinal - static_cast<uint8>(EHUDAction::TeamPreset0);
	if (Row >= 0 && Row < MaxTeammates)
		SelectTeammate(TeamFlow, TeammateSlotOfRow(Row));
	else if (Action == EHUDAction::TeamPower || Action == EHUDAction::TeamData)
		SelectResource(TeamFlow, Action == EHUDAction::TeamPower ? EResource::Power : EResource::Data);
	else if (Preset >= 0 && Preset < PresetCount)
		ApplyPreset(TeamFlow, Preset, Funds);
	else if (Action == EHUDAction::TeamStepDown || Action == EHUDAction::TeamStepUp)
		StepAmount(TeamFlow, Action == EHUDAction::TeamStepUp ? 1 : -1, Funds);
	else if (Action == EHUDAction::TeamSend)
		SendGift();
	else
	{
		TeamPanelPolicy::FLogBuffer Log;
		UGiftCommandComponent::ReadLog(*State, Log);
		ScrollLog(TeamFlow, Action == EHUDAction::TeamLogUp ? -1 : 1, Log.Num());
	}
	PlayUISound(TEXT("Click"));
	return true;
}
