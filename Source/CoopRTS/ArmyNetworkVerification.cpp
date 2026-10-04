// Development-only, explicit command-line opt-in. Each process observes its own game world;
// only the listen host with the separate Authority switch can arrange encounters.
// This file is the session driver: it polls request.json, routes the action to the handlers in the
// ArmyNetworkVerification*.cpp files and replies with the world snapshot.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/PlatformFileManager.h"
#include "HUD/ForceBarVerification.h"
#include "HUD/FortifyVerification.h"
#include "HUD/MapPresentationVerification.h"
#include "JevProductionVerification.h"
#include "HUD/PressureVerification.h"
#include "Json.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"

namespace CoopRTSNetworkVerification
{
namespace Probe
{
bool bAuthorityFixtures = false;
int32 Generation = 0;
FVector PlacementCandidate = FVector::ZeroVector;
bool bPlacementCandidateValid = false;
}

namespace
{
using namespace Probe;

FString Directory;
FString Peer;
FTSTicker::FDelegateHandle TickHandle;
int32 LastCommand = 0;
TWeakObjectPtr<UWorld> LastWorld;

// No fixture execution on clients, even if a malicious client sends fixture commands.
FString ExecuteFixture(const FProbeRequest& Probe)
{
	UWorld* World = Probe.World;
	if (!bAuthorityFixtures || !World->GetAuthGameMode() || World->GetNetMode() != NM_ListenServer)
		return TEXT("host-only authority fixture switch required");
	if (!Probe.State)
		return TEXT("server fixture state unavailable");
	FString Error;
	if (HandleScenarioFixture(Probe, Error) || HandleEconomyFixture(Probe, Error) || HandleAbilityFixture(Probe, Error)
		|| HandlePlanningFixture(Probe, Error))
		return Error;
	AArmyGroup* Army = FindArmy(World, Probe.Owner, Probe.Index);
	if (!Army)
		return TEXT("server fixture army unavailable");
	if (HandleForceFixture(Probe, *Army, Error))
		return Error;
	return TEXT("unknown command");
}

FString Execute(UWorld* World, const TSharedPtr<FJsonObject>& Request)
{
	if (!World)
		return TEXT("game world unavailable");
	FString ForceCardError;
	if (ForceBarVerification::Apply(*World, Request, ForceCardError) || FortifyVerification::Apply(*World, Request, ForceCardError)
		|| MapPresentationVerification::Apply(*World, Request, ForceCardError)
		|| JevProductionVerification::Apply(*World, Request, ForceCardError)
		|| PressureVerification::Apply(*World, Request, ForceCardError))
		return ForceCardError;
	FProbeRequest Probe;
	Probe.World = World;
	Probe.Request = Request;
	Request->TryGetStringField(TEXT("action"), Probe.Action);
	if (Probe.Action == TEXT("observe"))
		return FString();
	Probe.PC = LocalController(World);
	Probe.Own = Probe.PC ? Probe.PC->GetPlayerState<ACommandPlayerState>() : nullptr;
	Probe.Owner = static_cast<int32>(Request->GetIntegerField(TEXT("owner")));
	Probe.Index = static_cast<int32>(Request->GetIntegerField(TEXT("army")));
	Probe.State = World->GetGameState<ACommandGameState>();
	FString Error;
	if (HandleMatchAction(Probe, Error) || HandleCommandAction(Probe, Error) || HandleInputAction(Probe, Error)
		|| HandleAbilityAction(Probe, Error))
		return Error;
	return ExecuteFixture(Probe);
}

UWorld* FindGameWorld()
{
	if (GEngine)
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (UWorld* Candidate = Context.World())
				if (Candidate->IsGameWorld())
					return Candidate;
	return nullptr;
}

void WriteReply(int32 Id, const FString& Error, UWorld* World)
{
	auto Reply = Object();
	Number(Reply, TEXT("id"), Id);
	Reply->SetStringField(TEXT("peer"), Peer);
	Reply->SetStringField(TEXT("error"), Error);
	Reply->SetObjectField(TEXT("state"), Snapshot(World));
	FString Output;
	FJsonSerializer::Serialize(Reply.ToSharedRef(), TJsonWriterFactory<>::Create(&Output));
	// Memos carry non-ASCII text; AutoDetect would write UTF-16, which the harness cannot parse.
	FFileHelper::SaveStringToFile(Output, *(Directory / TEXT("reply.tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FPlatformFileManager::Get().GetPlatformFile().MoveFile(*(Directory / TEXT("reply.json")), *(Directory / TEXT("reply.tmp")));
}

bool Tick(float)
{
	UWorld* World = FindGameWorld();
	if (World && World != LastWorld.Get())
	{
		LastWorld = World;
		++Generation;
		bPlacementCandidateValid = false;
	}
	FString Input;
	if (!FFileHelper::LoadFileToString(Input, *(Directory / TEXT("request.json"))))
		return true;
	TSharedPtr<FJsonObject> Request;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Input), Request) || !Request.IsValid())
		return true;
	int32 Id = 0;
	if (!Request->TryGetNumberField(TEXT("id"), Id) || Id <= LastCommand)
		return true;
	LastCommand = Id;
	const FString Error = Execute(World, Request);
	WriteReply(Id, Error, World);
	UE_LOG(LogTemp, Display, TEXT("Network verification peer=%s id=%d error=%s"), *Peer, Id, *Error);
	return true;
}
}

void Start()
{
	if (!FParse::Value(FCommandLine::Get(), TEXT("CoopRTSNetVerifyDir="), Directory)
		|| !FParse::Value(FCommandLine::Get(), TEXT("CoopRTSNetVerifyPeer="), Peer))
		return;
	bAuthorityFixtures = FParse::Param(FCommandLine::Get(), TEXT("CoopRTSNetVerifyAuthority"));
	if (Directory.IsEmpty() || Peer.IsEmpty())
		return;
	UE_LOG(LogTemp, Display, TEXT("Network verification enabled peer=%s authorityFixtures=%d"), *Peer, bAuthorityFixtures);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), .1f);
}

void Stop()
{
	if (TickHandle.IsValid())
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
}
}
#endif
