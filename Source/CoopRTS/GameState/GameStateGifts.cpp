#include "GameState/GameStateGifts.h"

#include "CommandGameState.h"
#include "GameState/GameStateEconomy.h"

namespace
{
int32 Balance(const ACommandPlayerState& Wallet, EEconomyResource Resource)
{
	return Resource == EEconomyResource::Power ? Wallet.Resources : Wallet.Data;
}

FGiftInput GiftInput(const ACommandGameState& State, const ACommandPlayerState& Sender,
	const ACommandPlayerState* Recipient, EEconomyResource Resource, int32 Amount)
{
	const TArray<ACommandPlayerState*> Roster = FGameStateEconomy::Roster(State);
	const bool bRecipient = IsValid(Recipient);
	return { Roster.Contains(&Sender), Roster.Contains(Recipient), bRecipient && Recipient->TeamIndex == Sender.TeamIndex,
		Sender.CommanderIndex, bRecipient ? Recipient->CommanderIndex : INDEX_NONE, Amount, Balance(Sender, Resource),
		bRecipient ? Balance(*Recipient, Resource) : 0 };
}
}

namespace GameStateGifts
{
EGiftVerdict Apply(ACommandGameState& State, ACommandPlayerState& Sender, ACommandPlayerState* Recipient,
	EEconomyResource Resource, int32 Amount)
{
	const EGiftVerdict Verdict = EconomyPolicy::GiftVerdict(GiftInput(State, Sender, Recipient, Resource, Amount));
	if (Verdict != EGiftVerdict::Accepted)
		return Verdict;
	// The verdict covered both balances, so neither step below can fail or saturate.
	const bool bPower = Resource == EEconomyResource::Power;
	Sender.TrySpend(FResourceCost{ bPower ? Amount : 0, bPower ? 0 : Amount });
	if (bPower)
		Recipient->AddResources(Amount);
	else
		Recipient->AddData(Amount);
	FGiftLogEntry Entry;
	Entry.SenderSlot = Sender.CommanderIndex;
	Entry.RecipientSlot = Recipient->CommanderIndex;
	Entry.Resource = Resource;
	Entry.Amount = Amount;
	Entry.ServerTime = State.GetServerWorldTimeSeconds();
	State.GiftLog.Add(Entry);
	if (State.GiftLog.Num() > EconomyPolicy::GiftLogLimit)
		State.GiftLog.RemoveAt(0, State.GiftLog.Num() - EconomyPolicy::GiftLogLimit);
	State.ForceNetUpdate();
	return Verdict;
}
}
