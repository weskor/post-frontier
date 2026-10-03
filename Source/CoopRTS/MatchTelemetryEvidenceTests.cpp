#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "MatchTelemetryScenario.h"
#include "ArmyTestSetup.h"
#include "MatchTelemetry.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

namespace MatchTelemetryScenarioTests
{
bool FMatchScenario::IsNull(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key) const
{
	const TSharedPtr<FJsonValue>* Value = Object->Values.Find(Key);
	return Value && Value->IsValid() && (*Value)->Type == EJson::Null;
}

bool FMatchScenario::ReadEvidence(const FString& Path, TSharedPtr<FJsonObject>& Root)
{
	FString Json;
	if (!Check(FFileHelper::LoadFileToString(Json, *Path), TEXT("Telemetry output exists and loads")))
		return false;
	Test->AddInfo(FString::Printf(TEXT("Observed telemetry JSON (%s):\n%s"), *Path, *Json));
	if (!Check(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid(),
			TEXT("Telemetry output is valid JSON")))
		return false;
	const TCHAR* PrivateStrings[] = { PrivateOnlineId, TEXT("Telemetry zero activity"), TEXT("Telemetry leaver"),
		TEXT("Telemetry reconnected private name"), TEXT("Telemetry instant private name"), FPlatformProcess::ComputerName() };
	for (const TCHAR* Private : PrivateStrings)
		if (!Check(!Json.Contains(Private), TEXT("Serialized consumer output excludes account IDs, hostnames and player names")))
			return false;
	return Check(HostName.IsEmpty() || !Json.Contains(HostName), TEXT("Host player name is absent from serialized output"));
}

bool FMatchScenario::ValidateAbandoned()
{
	TSharedPtr<FJsonObject> Root;
	if (!ReadEvidence(AbandonedPath, Root))
		return false;
	double Schema = 0., Seconds = -1., RegionIndex = 0.;
	FString Id, Result, Map, Cause, RegionName;
	const TSharedPtr<FJsonObject>* Ending = nullptr;
	if (!Check(Root->TryGetNumberField(TEXT("schema_version"), Schema) && Schema == 2
				&& Root->TryGetNumberField(TEXT("battle_seconds"), Seconds) && FMath::IsFinite(Seconds) && Seconds >= 3.
				&& Root->TryGetStringField(TEXT("match_id"), Id) && Id == AbandonedId
				&& Root->TryGetStringField(TEXT("map"), Map) && Map == AbandonedMap
				&& Root->TryGetStringField(TEXT("result"), Result) && Result == TEXT("Abandoned")
				&& Root->TryGetObjectField(TEXT("ending"), Ending)
				&& (*Ending)->TryGetStringField(TEXT("cause"), Cause) && Cause == TEXT("level_transition")
				&& (*Ending)->TryGetNumberField(TEXT("region_index"), RegionIndex) && RegionIndex == INDEX_NONE
				&& (*Ending)->TryGetStringField(TEXT("region_name"), RegionName) && RegionName.IsEmpty()
				&& IsNull(*Ending, TEXT("location")),
			TEXT("Real ongoing travel writes Abandoned with truthful transition cause and explicitly unknown ending location")))
		return false;
	return ValidatePlayers(Root, Seconds, true);
}

bool FMatchScenario::ValidateFile(ACommandGameState* State, bool bFresh)
{
	const FString& Path = State->MatchTelemetry->GetOutputPath();
	if (!Check(Path == PathFor(State->MatchTelemetry), TEXT("Terminal output is a unique match JSON under Saved/Telemetry")))
		return false;
	TSharedPtr<FJsonObject> Root;
	if (!ReadEvidence(Path, Root))
		return false;
	double Schema = 0., Seconds = 0.;
	FString Id, Result;
	const TSharedPtr<FJsonObject>* Ending = nullptr;
	if (!Check(Root->TryGetNumberField(TEXT("schema_version"), Schema) && Schema == 2
				&& Root->TryGetNumberField(TEXT("battle_seconds"), Seconds) && FMath::IsFinite(Seconds) && Seconds >= 3.
				&& Seconds == State->MatchTelemetry->GetBattleSeconds()
				&& Root->TryGetStringField(TEXT("match_id"), Id) && Id == State->MatchTelemetry->GetMatchId()
				&& Root->TryGetStringField(TEXT("result"), Result) && Result == TEXT("Victory")
				&& Root->TryGetObjectField(TEXT("ending"), Ending),
			TEXT("Exact version, real terminal result and full simulation battle duration fields exist")))
		return false;
	if (!ValidatePlayers(Root, Seconds, bFresh))
		return false;
	const AMapRegion* Region = State->FindRegionAt(State->EnemyHeadquarters->GetActorLocation());
	FString Cause, RegionName;
	double RegionIndex = INDEX_NONE;
	const TSharedPtr<FJsonObject>* Location = nullptr;
	if (!Check(Region && (*Ending)->TryGetStringField(TEXT("cause"), Cause) && Cause == TEXT("enemy_headquarters_destroyed")
				&& (*Ending)->TryGetNumberField(TEXT("region_index"), RegionIndex) && RegionIndex == Region->RegionIndex
				&& (*Ending)->TryGetStringField(TEXT("region_name"), RegionName) && RegionName == Region->DisplayName.ToString()
				&& (*Ending)->TryGetObjectField(TEXT("location"), Location),
			TEXT("Terminal region, name and cause derive from the destroyed map HQ")))
		return false;
	const FVector Spot = State->EnemyHeadquarters->GetActorLocation();
	double X = 0., Y = 0., Z = 0.;
	return Check((*Location)->TryGetNumberField(TEXT("x"), X) && X == Spot.X
			&& (*Location)->TryGetNumberField(TEXT("y"), Y) && Y == Spot.Y
			&& (*Location)->TryGetNumberField(TEXT("z"), Z) && Z == Spot.Z,
		TEXT("Ending location is the exact map-derived HQ position"));
}

bool FMatchScenario::ValidatePlayers(const TSharedPtr<FJsonObject>& Root, double Seconds, bool bFresh)
{
	const TArray<TSharedPtr<FJsonValue>>* Players = nullptr;
	if (!Check(Root->TryGetArrayField(TEXT("players"), Players) && Players->Num() == (bFresh ? 1 : 4),
			TEXT("Complete human roster retains zero-action/zero-duration humans and merges the real reconnect")))
		return false;
	TSet<FString> Ids;
	bool Seen[4] = {};
	for (const TSharedPtr<FJsonValue>& Value : *Players)
	{
		if (!ValidatePlayer(Value, Seconds, bFresh, Ids, Seen))
			return false;
	}
	for (const FString& Id : Ids)
		OriginalPlayerIds.Add(Id);
	return true;
}

bool FMatchScenario::ValidatePlayer(const TSharedPtr<FJsonValue>& Value, double Seconds, bool bFresh, TSet<FString>& Ids, bool (&Seen)[4])
{
	if (!Check(Value.IsValid() && Value->Type == EJson::Object, TEXT("Every player entry is an object")))
		return false;
	const TSharedPtr<FJsonObject> Player = Value->AsObject();
	double Commander = -1., Joined = -1., Participation = -1.;
	FString PlayerId;
	FGuid Pseudonym;
	bool bDisconnected = false;
	if (!Check(Player->Values.Num() == 13 && !Player->HasField(TEXT("player_name"))
				&& Player->TryGetNumberField(TEXT("commander_index"), Commander)
				&& Player->TryGetStringField(TEXT("player_id"), PlayerId)
				&& FGuid::ParseExact(PlayerId, EGuidFormats::DigitsWithHyphens, Pseudonym) && Pseudonym.IsValid()
				&& !Ids.Contains(PlayerId) && !OriginalPlayerIds.Contains(PlayerId)
				&& Player->TryGetBoolField(TEXT("disconnected"), bDisconnected)
				&& Player->TryGetNumberField(TEXT("joined_seconds"), Joined) && FMath::IsFinite(Joined) && Joined >= 0.
				&& Player->TryGetNumberField(TEXT("participation_seconds"), Participation) && FMath::IsFinite(Participation),
			TEXT("Only commander slot and distinct random match-local UUID attribute players; participation fields are finite")))
		return false;
	Ids.Add(PlayerId);
	const int32 Index = static_cast<int32>(Commander);
	const bool bHost = Index == HostIndex;
	if (!Check(Commander == Index && Index >= 0 && Index < 4 && !Seen[Index] && (bHost || !bFresh),
			TEXT("No enemy, duplicate slot or extra reconnect participant appears")))
		return false;
	Seen[Index] = true;
	const bool bLeft = !bFresh && (Index == 1 || Index == 3);
	const double ExpectedJoined = bFresh || bHost ? Joined
		: Index == 1                              ? FirstJoined
		: Index == 2                              ? ZeroJoined
												  : InstantJoined;
	const double ExpectedParticipation = !bFresh && Index == 1
		? FirstLeft - FirstJoined + LastLeft - Rejoined
		: !bFresh && Index == 3 ? 0.
								: Seconds - ExpectedJoined;
	double Left = -1.;
	if (!Check(Joined == ExpectedJoined && Joined <= Seconds && (bFresh || !bHost || Joined <= FirstJoined)
				&& bDisconnected == bLeft
				&& (bLeft ? Player->TryGetNumberField(TEXT("left_seconds"), Left) && Left == (Index == 1 ? LastLeft : InstantJoined)
						  : IsNull(Player, TEXT("left_seconds")))
				&& FMath::IsNearlyEqual(Participation, ExpectedParticipation, 1.e-9) && Participation >= 0.
				&& (bFresh || bHost || Index == 3 || (Joined >= 3. && Participation < Seconds))
				&& (bFresh || Index != 1 || (FirstLeft > FirstJoined && Rejoined > FirstLeft && LastLeft > Rejoined)),
			TEXT("Exact first join, last logout and connected intervals exclude prejoin, pause and reconnect gaps")))
		return false;
	return ValidatePlayerRates(Player, bFresh, bHost, Index, ExpectedParticipation);
}

bool FMatchScenario::ValidatePlayerRates(const TSharedPtr<FJsonObject>& Player, bool bFresh, bool bHost, int32 Index, double ExpectedParticipation)
{
	const int32 Orders = bFresh ? 0 : bHost || Index == 1 ? 2
		: Index == 3                                      ? 1
														  : 0;
	const int32 Builds = !bFresh && bHost ? 1 : 0;
	const int32 Pings = !bFresh && bHost ? 1 : 0;
	const TCHAR* Counts[] = { TEXT("orders"), TEXT("builds"), TEXT("pings") };
	const TCHAR* Rates[] = { TEXT("orders_per_minute"), TEXT("builds_per_minute"), TEXT("pings_per_minute"), TEXT("decisions_per_minute") };
	const int32 Expected[] = { Orders, Builds, Pings, Orders + Builds + Pings };
	for (int32 Category = 0; Category < 4; ++Category)
	{
		double Count = 0., Rate = 0.;
		const double ExpectedRate = ExpectedParticipation > 0. ? Expected[Category] * 60. / ExpectedParticipation : 0.;
		if (!Check((Category == 3 || (Player->TryGetNumberField(Counts[Category], Count) && Count == Expected[Category]))
					&& Player->TryGetNumberField(Rates[Category], Rate) && FMath::IsFinite(Rate)
					&& FMath::IsNearlyEqual(Rate, ExpectedRate, 1.e-9),
				TEXT("Consumer JSON has exact accepted counts and participation-minute rates, including finite zero-duration rates")))
			return false;
	}
	return true;
}
}

#endif
