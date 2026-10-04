#include "CommandService.h"
#include "CommandGameState.h"
#include "Engine/World.h"
#include "GameState/GameStateGifts.h"

namespace
{
const TCHAR* VerdictText(EGiftVerdict Verdict)
{
	switch (Verdict)
	{
	case EGiftVerdict::SenderNotInRoster:
		return TEXT("Gift rejected: you are not a commander in this battle.");
	case EGiftVerdict::OtherTeam:
		return TEXT("Gift rejected: you can only gift a teammate.");
	case EGiftVerdict::RecipientNotInRoster:
		return TEXT("Gift rejected: that commander is not in this battle.");
	case EGiftVerdict::ToSelf:
		return TEXT("Gift rejected: you cannot gift yourself.");
	case EGiftVerdict::AmountNotPositive:
		return TEXT("Gift rejected: send a whole amount above zero.");
	case EGiftVerdict::Overdraft:
		return TEXT("Gift rejected: not enough in your wallet.");
	case EGiftVerdict::RecipientFull:
		return TEXT("Gift rejected: the recipient's wallet cannot hold that much.");
	default:
		return TEXT("Gift sent.");
	}
}
}

FCommandResult FCommandService::Gift(ACommandPlayerState* Sender, ACommandPlayerState* Recipient, EEconomyResource Resource, int32 Amount)
{
	ACommandGameState* State = IsValid(Sender) && Sender->HasAuthority() && Sender->GetWorld()
		? Sender->GetWorld()->GetGameState<ACommandGameState>()
		: nullptr;
	if (!State || State->MatchResult != EMatchResult::Ongoing)
		return { ECommandRejection::Unavailable, TEXT("Gift rejected: no live battle."), nullptr };
	if (State->IsPlanning())
		return { ECommandRejection::Unavailable, TEXT("Nothing runs during planning."), nullptr };
	const EGiftVerdict Verdict = GameStateGifts::Apply(*State, *Sender, Recipient, Resource, Amount);
	if (Verdict == EGiftVerdict::Accepted)
		return { ECommandRejection::None, VerdictText(Verdict), nullptr };
	return { Verdict == EGiftVerdict::SenderNotInRoster ? ECommandRejection::InvalidOwner : ECommandRejection::InvalidRequest,
		VerdictText(Verdict), nullptr };
}
