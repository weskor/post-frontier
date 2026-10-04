#include "GiftCommandComponent.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandService.h"
#include "GameState/GameStateEconomy.h"

UGiftCommandComponent::UGiftCommandComponent()
{
	SetIsReplicatedByDefault(true);
}

TeamPanelPolicy::EResource UGiftCommandComponent::ToPolicy(EEconomyResource Resource)
{
	return Resource == EEconomyResource::Power ? TeamPanelPolicy::EResource::Power : TeamPanelPolicy::EResource::Data;
}

EEconomyResource UGiftCommandComponent::FromPolicy(TeamPanelPolicy::EResource Resource)
{
	return Resource == TeamPanelPolicy::EResource::Power ? EEconomyResource::Power : EEconomyResource::Data;
}

void UGiftCommandComponent::Teammates(const ACommandGameState& State, const ACommandPlayerState* Self,
	TArray<const ACommandPlayerState*, TInlineAllocator<TeamPanelPolicy::MaxTeammates>>& Out)
{
	Out.Reset();
	if (!Self)
		return;
	for (const ACommandPlayerState* Wallet : FGameStateEconomy::Roster(State))
		if (Wallet != Self && Wallet->TeamIndex == Self->TeamIndex && Out.Num() < TeamPanelPolicy::MaxTeammates)
			Out.Add(Wallet);
}

void UGiftCommandComponent::ReadLog(const ACommandGameState& State, TeamPanelPolicy::FLogBuffer& Out)
{
	Out.Reset();
	const float Start = State.GetBattleClockStartServerTime();
	for (const FGiftLogEntry& Entry : State.GiftLog)
		Out.Add({ Entry.SenderSlot, Entry.RecipientSlot, ToPolicy(Entry.Resource), Entry.Amount, FMath::Max(0.f, Entry.ServerTime - Start) });
}

TeamPanelPolicy::FSendInput UGiftCommandComponent::MakeSendInput(const ACommandGameState& State,
	const ACommandPlayerState* Sender, int32 Teammate, TeamPanelPolicy::EResource Resource, int32 Amount)
{
	TeamPanelPolicy::FSendInput In;
	In.bBattleLive = State.MatchResult == EMatchResult::Ongoing;
	In.bPlanning = State.IsPlanning();
	In.Teammate = Teammate;
	In.Resource = Resource;
	In.Amount = Amount;
	if (!Sender)
		return In;
	In.Balance = Resource == TeamPanelPolicy::EResource::Power ? Sender->Resources : Sender->Data;
	TArray<const ACommandPlayerState*, TInlineAllocator<TeamPanelPolicy::MaxTeammates>> Mates;
	Teammates(State, Sender, Mates);
	for (const ACommandPlayerState* Wallet : Mates)
		In.bTeammatePresent |= Wallet->CommanderIndex == Teammate;
	return In;
}

void UGiftCommandComponent::ServerGift_Implementation(ACommandPlayerState* Recipient, int32 Teammate, EEconomyResource Resource, int32 Amount)
{
	const ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	ACommandPlayerState* Sender = Controller->GetPlayerState<ACommandPlayerState>();
	const FCommandResult Result = FCommandService::Gift(Sender, Recipient, Resource, Amount);
	FString Message;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!Result.IsAccepted() && State)
	{
		// The panel's own wording when its rules explain the refusal; otherwise the service's reason.
		const TeamPanelPolicy::FSendInput In = MakeSendInput(
			*State, Sender, IsValid(Recipient) ? Recipient->CommanderIndex : Teammate, ToPolicy(Resource), Amount);
		const TeamPanelPolicy::ESendVerdict Verdict = TeamPanelPolicy::Verdict(In);
		TStringBuilder<96> Reason;
		TeamPanelPolicy::AppendReason(Reason, Verdict, In);
		Message = Verdict != TeamPanelPolicy::ESendVerdict::Ok ? FString(Reason.ToView()) : Result.Message;
	}
	ClientGiftFeedback(Message, Result.IsAccepted());
}

void UGiftCommandComponent::ClientGiftFeedback_Implementation(const FString& Message, bool bAccepted)
{
	CastChecked<ACommandPlayerController>(GetOwner())->CompleteGiftInput(Message, bAccepted);
}
