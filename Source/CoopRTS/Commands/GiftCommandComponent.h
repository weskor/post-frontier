#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CommandPlayerState.h"
#include "Rules/TeamPanelPolicy.h"
#include "GiftCommandComponent.generated.h"

class ACommandGameState;

// Owner-only gifting (ui.md surface 3). The Team panel sends here; the authority is FCommandService::Gift and the
// replicated team log, so this component keeps no history of its own.
UCLASS()
class COOPRTS_API UGiftCommandComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGiftCommandComponent();
	UFUNCTION(Server, Reliable)
	// Teammate is the commander slot the panel chose, sent beside the pointer so a teammate who left in flight (a null
	// Recipient) is still named in the refusal.
	void ServerGift(ACommandPlayerState* Recipient, int32 Teammate, EEconomyResource Resource, int32 Amount);
	// The verdict of the last gift: the panel's text on a rejection, empty on acceptance.
	UFUNCTION(Client, Reliable)
	void ClientGiftFeedback(const FString& Message, bool bAccepted);

	// What the panel's Send verdict reads, assembled from replicated state so the panel and the authority agree on
	// every peer. Teammate is a commander slot, INDEX_NONE for nobody; Sender may be null.
	static TeamPanelPolicy::FSendInput MakeSendInput(const ACommandGameState& State, const ACommandPlayerState* Sender,
		int32 Teammate, TeamPanelPolicy::EResource Resource, int32 Amount);
	// The commander's teammates in roster order, at most TeamPanelPolicy::MaxTeammates: the panel's rows.
	static void Teammates(const ACommandGameState& State, const ACommandPlayerState* Self,
		TArray<const ACommandPlayerState*, TInlineAllocator<TeamPanelPolicy::MaxTeammates>>& Out);
	// The replicated team log, oldest first, timed on the battle clock.
	static void ReadLog(const ACommandGameState& State, TeamPanelPolicy::FLogBuffer& Out);
	static TeamPanelPolicy::EResource ToPolicy(EEconomyResource Resource);
	static EEconomyResource FromPolicy(TeamPanelPolicy::EResource Resource);
};
