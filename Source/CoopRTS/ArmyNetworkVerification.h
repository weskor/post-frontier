// Shared machinery of the loopback network probe. Private to the ArmyNetworkVerification*.cpp files;
// the only outside entry points are Start and Stop, called from the game module.
#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class AArmyGroup;
class ACommandBuilding;
class ACommandGameState;
class ACommandPlayerController;
class ACommandPlayerState;
class UArmyUnitDefinition;
class UObject;
class UWorld;

namespace CoopRTSNetworkVerification
{
void Start();
void Stop();

namespace Probe
{
// One request as seen by the action handlers: the world, the parsed JSON and the local actors it concerns.
struct FProbeRequest
{
	UWorld* World = nullptr;
	TSharedPtr<FJsonObject> Request;
	FString Action;
	ACommandPlayerController* PC = nullptr;
	ACommandPlayerState* Own = nullptr;
	ACommandGameState* State = nullptr;
	int32 Owner = 0;
	int32 Index = 0;
};

// Session state shared by the snapshot and the fixture actions.
extern bool bAuthorityFixtures;
extern int32 Generation;
extern FVector PlacementCandidate;
extern bool bPlacementCandidateValid;

// JSON and lookup helpers.
TSharedPtr<FJsonObject> Object();
int32 LifetimeId(const UObject* Value);
void Number(const TSharedPtr<FJsonObject>& ObjectValue, const TCHAR* Key, double Value);
void Vector(const TSharedPtr<FJsonObject>& ObjectValue, const TCHAR* Key, FVector Value);
const UArmyUnitDefinition* ProductionDefinition(const ACommandGameState& State, const ACommandBuilding& Building);
ACommandPlayerController* LocalController(UWorld* World);
AArmyGroup* FindArmy(UWorld* World, int32 Owner, int32 Index);

// Snapshot of the local world, serialised into every reply.
TSharedPtr<FJsonObject> Snapshot(UWorld* World);
void ArmiesSnapshot(UWorld* World, const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result);
void StructuresSnapshot(const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result);
void ObjectiveSnapshot(const UObject* Context, const TSharedPtr<FJsonObject>& Result);
void PingSnapshot(UWorld* World, const TSharedPtr<FJsonObject>& Result);
void JevIntentSnapshot(UWorld* World, const TSharedPtr<FJsonObject>& Result);

// Action handlers. Each returns true when it recognised the action and sets Error to its outcome
// (empty on success); false leaves the action to the next handler.
bool HandleMatchAction(const FProbeRequest& Probe, FString& Error);
bool HandleCommandAction(const FProbeRequest& Probe, FString& Error);
bool HandleInputAction(const FProbeRequest& Probe, FString& Error);
bool HandleEconomyFixture(const FProbeRequest& Probe, FString& Error);
bool HandleScenarioFixture(const FProbeRequest& Probe, FString& Error);
bool HandleForceFixture(const FProbeRequest& Probe, AArmyGroup& Army, FString& Error);
}
}
#endif
