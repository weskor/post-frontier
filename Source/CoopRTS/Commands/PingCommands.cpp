#include "CommandService.h"
#include "PingCommandComponent.h"
#include "ArenaBounds.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "MapRegion.h"

FCommandResult FCommandService::Ping(ACommandPlayerController* Controller, const FVector& Location, AArmyGroup* Force)
{
	ACommandPlayerState* Sender = IsValid(Controller) ? Controller->GetPlayerState<ACommandPlayerState>() : nullptr;
	UWorld* World = IsValid(Controller) ? Controller->GetWorld() : nullptr;
	const ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
	if (!World || !Controller->HasAuthority() || !IsValid(Sender) || Sender->TeamIndex != 0
		|| Sender->CommanderIndex < 0 || Sender->CommanderIndex >= 5 || !State
		|| State->MatchResult != EMatchResult::Ongoing || !Controller->PingCommands)
		return { ECommandRejection::Unavailable, TEXT("Ping unavailable: no ongoing team battle.") };
	const bool bTeammate = IsValid(Force) && !Force->IsActorBeingDestroyed() && Force->GetWorld() == World
		&& Force->GetTeamIndex() == Sender->TeamIndex && IsValid(Force->GetOwningPlayerState())
		&& Force->GetOwningPlayerState() != Sender;
	if (Force && (!IsValid(Force) || Force->IsActorBeingDestroyed() || Force->GetWorld() != World))
		return { ECommandRejection::InvalidRequest, TEXT("Ping rejected: force unavailable.") };
	const FVector Spot = bTeammate ? Force->GetCenter() : Location;
	if (Spot.ContainsNaN() || !AArenaBounds::IsTravelLocation(World, Spot))
		return { ECommandRejection::InvalidRequest, TEXT("Ping rejected: point inside the arena.") };
	if (!Controller->PingCommands->Throttle.Accept(FPlatformTime::Seconds()))
		return { ECommandRejection::InvalidRequest, TEXT("Ping throttled: one ping every 2 seconds.") };

	FObjectiveEvent Event;
	Event.Id = bTeammate ? TEXT("ping_need_help") : TEXT("ping_look_here");
	Event.ServerTime = State->GetServerWorldTimeSeconds();
	Event.Location = Spot;
	Event.AffectedTeam = Sender->TeamIndex;
	if (const AMapRegion* Region = State->FindRegionAt(Spot))
	{
		Event.RegionIndex = Region->RegionIndex;
		Event.RegionName = Region->DisplayName.ToString();
	}
	FObjectiveForce& Attribution = Event.Forces.AddDefaulted_GetRef();
	Attribution.TeamIndex = Sender->TeamIndex;
	Attribution.CommanderIndex = Sender->CommanderIndex;
	Attribution.PlayerName = Sender->GetPlayerName();
	if (bTeammate)
	{
		Attribution.ForceNumber = Force->ForceNumber;
		if (!Force->GetUnits().IsEmpty() && IsValid(Force->GetUnits()[0]))
			Attribution.UnitIndex = Force->GetUnits()[0]->GetUnitIndex();
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		ACommandPlayerController* Recipient = Cast<ACommandPlayerController>(It->Get());
		const ACommandPlayerState* Player = Recipient ? Recipient->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (Player && Player->TeamIndex == Sender->TeamIndex && Player->CommanderIndex >= 0
			&& Player->CommanderIndex < 5 && Recipient->PingCommands)
			Recipient->PingCommands->ClientReceivePing(Event);
	}
	return { ECommandRejection::None, bTeammate ? TEXT("Need help here ping sent.") : TEXT("Look here ping sent.") };
}
