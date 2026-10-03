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
		StartedSimulationSeconds = GetWorld()->GetTimeSeconds();
		for (APlayerState* Player : State->PlayerArray)
			RegisterHuman(Cast<ACommandPlayerState>(Player));
	}
}

FMatchTelemetryPlayer* UMatchTelemetry::FindOrRegister(ACommandPlayerState* Player)
{
	const ACommandGameState* State = AuthorityState();
	if (!State || bFlushed || State->MatchResult != EMatchResult::Ongoing
		|| !IsValid(Player) || Player->GetWorld() != GetWorld() || Player->TeamIndex != 0
		|| Player->CommanderIndex < 0 || Player->CommanderIndex >= 5)
		return nullptr;
	for (FMatchTelemetryPlayer& Entry : Players)
		if (Entry.State == Player)
		{
			Entry.State = Player;
			Entry.CommanderIndex = Player->CommanderIndex;
			Entry.PlayerName = Player->GetPlayerName();
			Entry.bDisconnected = false;
			return &Entry;
		}
	const FUniqueNetIdRepl& UniqueId = Player->GetUniqueId();
	const FString OnlineId = UniqueId.IsValid() ? UniqueId.ToString() : FString();
	if (!OnlineId.IsEmpty())
		for (FMatchTelemetryPlayer& Entry : Players)
			if (Entry.PlayerId == OnlineId)
			{
				Entry.State = Player;
				Entry.CommanderIndex = Player->CommanderIndex;
				Entry.PlayerName = Player->GetPlayerName();
				Entry.bDisconnected = false;
				return &Entry;
			}
	FMatchTelemetryPlayer& Entry = Players.AddDefaulted_GetRef();
	Entry.State = Player;
	// Steam identity remains stable across reconnects. Offline players receive a
	// distinct match-local identity, never a reusable commander slot or name.
	Entry.PlayerId = !OnlineId.IsEmpty() ? OnlineId
										 : TEXT("local:") + FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Entry.CommanderIndex = Player->CommanderIndex;
	Entry.PlayerName = Player->GetPlayerName();
	return &Entry;
}

void UMatchTelemetry::RegisterHuman(ACommandPlayerState* Player)
{
	FindOrRegister(Player);
}

void UMatchTelemetry::HumanLeft(ACommandPlayerState* Player)
{
	if (FMatchTelemetryPlayer* Entry = FindOrRegister(Player))
		Entry->bDisconnected = true;
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
	EndedBattleSeconds = GetBattleSeconds();
	bFlushed = true; // One attempt even on failure; reporting cannot change outcome.

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);
	Root->SetStringField(TEXT("match_id"), MatchId);
	Root->SetStringField(TEXT("map"), UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()));
	Root->SetNumberField(TEXT("battle_seconds"), EndedBattleSeconds);
	Root->SetStringField(TEXT("result"), State->MatchResult == EMatchResult::Victory ? TEXT("Victory") : TEXT("Defeat"));

	TArray<TSharedPtr<FJsonValue>> PlayerValues;
	PlayerValues.Reserve(Players.Num());
	const double PerMinute = EndedBattleSeconds > 0. ? 60. / EndedBattleSeconds : 0.;
	for (FMatchTelemetryPlayer& Entry : Players)
	{
		if (!Entry.bDisconnected && Entry.State.IsValid())
		{
			Entry.PlayerName = Entry.State->GetPlayerName();
			Entry.CommanderIndex = Entry.State->CommanderIndex;
		}
		const TSharedRef<FJsonObject> Player = MakeShared<FJsonObject>();
		Player->SetStringField(TEXT("player_id"), Entry.PlayerId);
		Player->SetStringField(TEXT("player_name"), Entry.PlayerName);
		Player->SetNumberField(TEXT("commander_index"), Entry.CommanderIndex);
		Player->SetBoolField(TEXT("disconnected"), Entry.bDisconnected);
		Player->SetNumberField(TEXT("orders"), Entry.Orders);
		Player->SetNumberField(TEXT("builds"), Entry.Builds);
		Player->SetNumberField(TEXT("pings"), Entry.Pings);
		// Every rate uses the full battle's simulation duration, including for
		// late joiners/leavers. A zero-duration battle has finite zero rates.
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
	const bool bDestroyedHQ = IsValid(HQ) && HQ->Health <= 0;
	const bool bTie = bDestroyedHQ && bDefeat && IsValid(State->EnemyHeadquarters) && State->EnemyHeadquarters->Health <= 0;
	Ending->SetStringField(TEXT("cause"), bDestroyedHQ ? bTie ? TEXT("both_headquarters_destroyed") : bDefeat ? TEXT("friendly_headquarters_destroyed")
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
