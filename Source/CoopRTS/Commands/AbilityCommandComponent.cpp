#include "AbilityCommandComponent.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandService.h"
#include "CoopAudioSubsystem.h"
#include "Engine/World.h"
#include "GameState/GameStateEconomy.h"
#include "MapRegion.h"
#include "Rules/FortifyPolicy.h"

namespace
{
FObjectiveEvent MakeEvent(const AMapRegion& Region, const ACommandGameState& State, const TCHAR* Id, int32 Team)
{
	FObjectiveEvent Event;
	Event.Id = Id;
	Event.ServerTime = State.GetServerWorldTimeSeconds();
	Event.Location = State.GetRegionAnchor(Region.RegionIndex);
	Event.RegionIndex = Region.RegionIndex;
	Event.RegionName = Region.DisplayName.ToString();
	Event.AffectedTeam = Team;
	return Event;
}

// Every controller of a roster commander on Team, except the one holding Except.
template <typename Visit>
void ForEachTeammate(const ACommandGameState& State, int32 Team, const ACommandPlayerState* Except, Visit&& Do)
{
	const TArray<ACommandPlayerState*> Roster = FGameStateEconomy::Roster(State);
	for (FConstPlayerControllerIterator It = State.GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(It->Get());
		const ACommandPlayerState* Player = Controller ? Controller->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (Player && Player != Except && Player->TeamIndex == Team && Roster.Contains(Player) && Controller->AbilityCommands)
			Do(*Controller->AbilityCommands);
	}
}
}

UAbilityCommandComponent::UAbilityCommandComponent()
{
	SetIsReplicatedByDefault(true);
	Events.Reserve(UObjectiveAnnouncer::HistoryLimit);
}

void UAbilityCommandComponent::ServerCastFortify_Implementation(AMapRegion* Region)
{
	ACommandPlayerController* Controller = CastChecked<ACommandPlayerController>(GetOwner());
	const FCommandResult Result = FCommandService::CastFortify(Controller->GetPlayerState<ACommandPlayerState>(), Region);
	ClientFortifyFeedback(Result.Message, Result.IsAccepted());
}

void UAbilityCommandComponent::ClientFortifyFeedback_Implementation(const FString& Message, bool bAccepted)
{
	CastChecked<ACommandPlayerController>(GetOwner())->CompleteFortifyInput(Message, bAccepted);
	// The caster's own team hears the voiced line too; teammates hear it with the row.
	if (const ACommandGameState* State = bAccepted ? GetWorld()->GetGameState<ACommandGameState>() : nullptr)
		PlayCastVoice(State->GetServerWorldTimeSeconds());
}

void UAbilityCommandComponent::PlayCastVoice(float ServerTime) const
{
	if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
		Audio->PlayAnnouncer(FortifyPolicy::CastEventId, ServerTime);
}

void UAbilityCommandComponent::ClientReceiveTeamEvent_Implementation(const FObjectiveEvent& Event)
{
	const bool bFull = Events.Num() == UObjectiveAnnouncer::HistoryLimit;
	FObjectiveEvent& Stored = bFull ? Events[OldestEvent] : Events.AddDefaulted_GetRef();
	Stored = Event;
	Stored.Sequence = NextSequence--;
	if (bFull)
		OldestEvent = (OldestEvent + 1) % UObjectiveAnnouncer::HistoryLimit;
	if (Event.Id == FName(FortifyPolicy::CastEventId))
		PlayCastVoice(Event.ServerTime);
}

void UAbilityCommandComponent::ResetForMatch()
{
	Events.Reset();
	OldestEvent = 0;
	NextSequence = SequenceBase;
}

void UAbilityCommandComponent::PostFortifyCast(const AMapRegion& Region, const ACommandPlayerState& Caster)
{
	const ACommandGameState* State = Region.GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
		return;
	FObjectiveEvent Event = MakeEvent(Region, *State, FortifyPolicy::CastEventId, Caster.TeamIndex);
	FObjectiveForce& Attribution = Event.Forces.AddDefaulted_GetRef();
	Attribution.TeamIndex = Caster.TeamIndex;
	Attribution.CommanderIndex = Caster.CommanderIndex;
	Attribution.PlayerName = Caster.GetPlayerName();
	ForEachTeammate(*State, Caster.TeamIndex, &Caster,
		[&Event](UAbilityCommandComponent& Recipient) { Recipient.ClientReceiveTeamEvent(Event); });
}

void UAbilityCommandComponent::PostFortifyEnded(const AMapRegion& Region, int32 Team)
{
	const ACommandGameState* State = Region.GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
		return;
	const FObjectiveEvent Event = MakeEvent(Region, *State, FortifyPolicy::EndedEventId, Team);
	ForEachTeammate(*State, Team, nullptr,
		[&Event](UAbilityCommandComponent& Recipient) { Recipient.ClientReceiveTeamEvent(Event); });
}
