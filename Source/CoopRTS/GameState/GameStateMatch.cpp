#include "CommandGameState.h"

#include "CommandPlayerState.h"
#include "CoopAudioSubsystem.h"
#include "MatchTelemetry.h"

// Match lifecycle: terminal result, its telemetry flush and the outcome stinger.

void ACommandGameState::SetMatchResult(EMatchResult Result)
{
	if (!HasAuthority() || MatchResult == Result)
		return;
	const bool bTerminalTransition = MatchResult == EMatchResult::Ongoing && Result != EMatchResult::Ongoing;
	if (bTerminalTransition)
		for (APlayerState* Player : PlayerArray)
			MatchTelemetry->RegisterHuman(Cast<ACommandPlayerState>(Player));
	MatchResult = Result;
	if (bTerminalTransition)
		MatchTelemetry->FlushMatch();
	OnRep_MatchResult();
	ForceNetUpdate();
}

void ACommandGameState::OnRep_MatchResult()
{
	if (!bMatchAudioInitialized)
		return;
	const EMatchResult PreviousResult = LastAudioMatchResult;
	LastAudioMatchResult = MatchResult;
	if (bOutcomeAudioPlayed || PreviousResult != EMatchResult::Ongoing
		|| MatchResult == EMatchResult::Ongoing)
		return;
	bOutcomeAudioPlayed = true;
	if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
		Audio->PlayOutcome(MatchResult == EMatchResult::Victory);
}

void ACommandGameState::AddPlayerState(APlayerState* PlayerState)
{
	// The controllerless enemy is match-local, not a human roster or travel member.
	if (const ACommandPlayerState* Commander = Cast<ACommandPlayerState>(PlayerState))
		if (Commander->TeamIndex == 5)
			return;
	Super::AddPlayerState(PlayerState);
	if (MatchTelemetry)
		MatchTelemetry->RegisterHuman(Cast<ACommandPlayerState>(PlayerState));
}
