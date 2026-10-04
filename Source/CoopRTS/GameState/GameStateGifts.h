#pragma once

#include "CoreMinimal.h"
#include "CommandPlayerState.h"
#include "Rules/EconomyPolicy.h"

class ACommandGameState;

// Gifts between teammates: validation, the atomic transfer and the replicated team log.
namespace GameStateGifts
{
// Move Amount from Sender to Recipient, or change nothing and say why. An accepted gift joins the log.
EGiftVerdict Apply(ACommandGameState& State, ACommandPlayerState& Sender, ACommandPlayerState* Recipient,
	EEconomyResource Resource, int32 Amount);
}
