#include "MatchTelemetry.h"

#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UMatchTelemetry::UMatchTelemetry()
{
	PrimaryComponentTick.bCanEverTick = false;
}

ACommandGameState* UMatchTelemetry::AuthorityState() const
{
	ACommandGameState* State = Cast<ACommandGameState>(GetOwner());
	return State && State->HasAuthority() ? State : nullptr;
}

void UMatchTelemetry::BeginPlay()
{
	Super::BeginPlay();
	if (ACommandGameState* State = AuthorityState())
	{
		MatchId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		// Seamless travel renames the old world before component EndPlay.
		MapName = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
		StartedSimulationSeconds = GetWorld()->GetTimeSeconds();
		for (APlayerState* Player : State->PlayerArray)
			RegisterHuman(Cast<ACommandPlayerState>(Player));
	}
}

void UMatchTelemetry::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	const ACommandGameState* State = AuthorityState();
	if (State && State->MatchResult == EMatchResult::Ongoing && !bFlushed && !MatchId.IsEmpty())
	{
		const TCHAR* Cause = TEXT("unknown_end_play");
		switch (EndPlayReason)
		{
		case EEndPlayReason::LevelTransition:
			Cause = TEXT("level_transition");
			break;
		case EEndPlayReason::RemovedFromWorld:
			Cause = TEXT("removed_from_world");
			break;
		case EEndPlayReason::EndPlayInEditor:
			Cause = TEXT("end_play_in_editor");
			break;
		case EEndPlayReason::Quit:
			Cause = TEXT("application_quit");
			break;
		case EEndPlayReason::Destroyed:
			Cause = TEXT("owner_destroyed");
			break;
		}
		WriteMatch(true, Cause);
	}
	Super::EndPlay(EndPlayReason);
}

FMatchTelemetryPlayer* UMatchTelemetry::FindOrRegister(ACommandPlayerState* Player)
{
	const ACommandGameState* State = AuthorityState();
	if (!State || MatchId.IsEmpty() || bFlushed || State->MatchResult != EMatchResult::Ongoing
		|| !IsValid(Player) || Player->GetWorld() != GetWorld() || Player->TeamIndex != 0
		|| Player->CommanderIndex < 0 || Player->CommanderIndex >= 5)
		return nullptr;
	const FUniqueNetIdRepl& UniqueId = Player->GetUniqueId();
	for (FMatchTelemetryPlayer& Entry : Players)
		if (Entry.State == Player || (UniqueId.IsValid() && Entry.OnlineId.IsValid() && Entry.OnlineId == UniqueId))
		{
			Entry.State = Player;
			Entry.CommanderIndex = Player->CommanderIndex;
			if (!Entry.OnlineId.IsValid() && UniqueId.IsValid())
				Entry.OnlineId = UniqueId;
			if (Entry.bDisconnected)
				Entry.ConnectedSinceSeconds = GetBattleSeconds();
			Entry.bDisconnected = false;
			return &Entry;
		}
	FMatchTelemetryPlayer& Entry = Players.AddDefaulted_GetRef();
	Entry.State = Player;
	Entry.OnlineId = UniqueId;
	// Never derive this match-local label from an account ID, host or name.
	Entry.PlayerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Entry.CommanderIndex = Player->CommanderIndex;
	Entry.JoinedSeconds = GetBattleSeconds();
	Entry.ConnectedSinceSeconds = Entry.JoinedSeconds;
	return &Entry;
}

void UMatchTelemetry::RegisterHuman(ACommandPlayerState* Player)
{
	FindOrRegister(Player);
}

void UMatchTelemetry::HumanLeft(ACommandPlayerState* Player)
{
	for (FMatchTelemetryPlayer& Entry : Players)
		if (Entry.State == Player && !Entry.bDisconnected && !bFlushed)
		{
			Entry.LeftSeconds = GetBattleSeconds();
			Entry.ParticipationSeconds += FMath::Max(0., Entry.LeftSeconds - Entry.ConnectedSinceSeconds);
			Entry.bHasLeft = true;
			Entry.bDisconnected = true;
			return;
		}
}

void UMatchTelemetry::RecordAccepted(ACommandPlayerState* Player, EMatchDecision Decision)
{
	const ACommandGameState* State = AuthorityState();
	if (!State || bFlushed || State->MatchResult != EMatchResult::Ongoing || !IsValid(Player)
		|| Player->GetWorld() != GetWorld() || Player->TeamIndex != 0
		|| Player->CommanderIndex < 0 || Player->CommanderIndex >= 5)
		return;
	FMatchTelemetryPlayer* Entry = nullptr;
	for (FMatchTelemetryPlayer& Candidate : Players)
		if (Candidate.State == Player)
		{
			Entry = &Candidate;
			break;
		}
	if (!Entry)
		Entry = FindOrRegister(Player);
	if (!Entry)
		return;
	switch (Decision)
	{
	case EMatchDecision::Order:
		++Entry->Orders;
		break;
	case EMatchDecision::Build:
		++Entry->Builds;
		break;
	case EMatchDecision::Ping:
		++Entry->Pings;
		break;
	}
}

double UMatchTelemetry::GetBattleSeconds() const
{
	return bFlushed  ? EndedBattleSeconds
		: GetWorld() ? FMath::Max(0., static_cast<double>(GetWorld()->GetTimeSeconds()) - StartedSimulationSeconds)
					 : 0.;
}

void UMatchTelemetry::FlushMatch()
{
	const ACommandGameState* State = AuthorityState();
	if (!State || bFlushed || State->MatchResult == EMatchResult::Ongoing)
		return;
	WriteMatch(false);
}

void UMatchTelemetry::WriteMatch(bool bAbandoned, const TCHAR* AbandonmentCause)
{
	const ACommandGameState* State = AuthorityState();
	if (!State || bFlushed || MatchId.IsEmpty()
		|| (State->MatchResult == EMatchResult::Ongoing) != bAbandoned)
		return;
	EndedBattleSeconds = GetBattleSeconds();
	bFlushed = true; // One attempt even on failure; reporting cannot change outcome.

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 2);
	Root->SetStringField(TEXT("match_id"), MatchId);
	Root->SetStringField(TEXT("map"), MapName);
	Root->SetNumberField(TEXT("battle_seconds"), EndedBattleSeconds);
	Root->SetStringField(TEXT("result"), bAbandoned ? TEXT("Abandoned") : State->MatchResult == EMatchResult::Victory ? TEXT("Victory")
																													  : TEXT("Defeat"));

	TArray<TSharedPtr<FJsonValue>> PlayerValues;
	PlayerValues.Reserve(Players.Num());
	for (FMatchTelemetryPlayer& Entry : Players)
	{
		if (!Entry.bDisconnected && Entry.State.IsValid())
			Entry.CommanderIndex = Entry.State->CommanderIndex;
		const double Participation = Entry.ParticipationSeconds
			+ (Entry.bDisconnected ? 0. : FMath::Max(0., EndedBattleSeconds - Entry.ConnectedSinceSeconds));
		const double PerMinute = Participation > 0. ? 60. / Participation : 0.;
		const TSharedRef<FJsonObject> Player = MakeShared<FJsonObject>();
		Player->SetStringField(TEXT("player_id"), Entry.PlayerId);
		Player->SetNumberField(TEXT("commander_index"), Entry.CommanderIndex);
		Player->SetBoolField(TEXT("disconnected"), Entry.bDisconnected);
		Player->SetNumberField(TEXT("joined_seconds"), Entry.JoinedSeconds);
		if (Entry.bHasLeft)
			Player->SetNumberField(TEXT("left_seconds"), Entry.LeftSeconds);
		else
			Player->SetField(TEXT("left_seconds"), MakeShared<FJsonValueNull>());
		Player->SetNumberField(TEXT("participation_seconds"), Participation);
		Player->SetNumberField(TEXT("orders"), Entry.Orders);
		Player->SetNumberField(TEXT("builds"), Entry.Builds);
		Player->SetNumberField(TEXT("pings"), Entry.Pings);
		// Rates use accumulated connected simulation time, excluding prejoin and
		// disconnection gaps. Zero participation produces finite zero rates.
		Player->SetNumberField(TEXT("orders_per_minute"), Entry.Orders * PerMinute);
		Player->SetNumberField(TEXT("builds_per_minute"), Entry.Builds * PerMinute);
		Player->SetNumberField(TEXT("pings_per_minute"), Entry.Pings * PerMinute);
		Player->SetNumberField(TEXT("decisions_per_minute"), (Entry.Orders + Entry.Builds + Entry.Pings) * PerMinute);
		PlayerValues.Add(MakeShared<FJsonValueObject>(Player));
	}
	Root->SetArrayField(TEXT("players"), PlayerValues);

	const TSharedRef<FJsonObject> Ending = MakeShared<FJsonObject>();
	const bool bDefeat = State->MatchResult == EMatchResult::Defeat;
	const AHeadquarters* HQ = bDefeat ? State->FriendlyHeadquarters.Get() : State->EnemyHeadquarters.Get();
	const bool bDestroyedHQ = !bAbandoned && IsValid(HQ) && HQ->Health <= 0;
	const bool bTie = bDestroyedHQ && bDefeat && IsValid(State->EnemyHeadquarters) && State->EnemyHeadquarters->Health <= 0;
	Ending->SetStringField(TEXT("cause"), bAbandoned ? AbandonmentCause : bDestroyedHQ ? bTie ? TEXT("both_headquarters_destroyed") : bDefeat ? TEXT("friendly_headquarters_destroyed")
																																			  : TEXT("enemy_headquarters_destroyed")
																					   : TEXT("match_result_set"));
	const AMapRegion* Region = bDestroyedHQ ? State->FindRegionAt(HQ->GetActorLocation()) : nullptr;
	Ending->SetNumberField(TEXT("region_index"), Region ? Region->RegionIndex : INDEX_NONE);
	Ending->SetStringField(TEXT("region_name"), Region ? Region->DisplayName.ToString() : FString());
	if (bDestroyedHQ)
	{
		const FVector Spot = HQ->GetActorLocation();
		const TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
		Location->SetNumberField(TEXT("x"), Spot.X);
		Location->SetNumberField(TEXT("y"), Spot.Y);
		Location->SetNumberField(TEXT("z"), Spot.Z);
		Ending->SetObjectField(TEXT("location"), Location);
	}
	else
		Ending->SetField(TEXT("location"), MakeShared<FJsonValueNull>());
	Root->SetObjectField(TEXT("ending"), Ending);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Telemetry"));
	OutputPath = FPaths::Combine(Directory, FString::Printf(TEXT("match-%s.json"), *MatchId));
	if (!FJsonSerializer::Serialize(Root, Writer) || !IFileManager::Get().MakeDirectory(*Directory, true)
		|| !FFileHelper::SaveStringToFile(Json, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		UE_LOG(LogTemp, Warning, TEXT("Unable to write match telemetry: %s"), *OutputPath);
}
