// Development-only, explicit command-line opt-in. Each process observes its own game world;
// only the listen host with the separate Authority switch can arrange encounters.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "EnemyCommander.h"
#include "Headquarters.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "HAL/PlatformFileManager.h"
#include "Json.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Containers/Ticker.h"

namespace CoopRTSNetworkVerification
{
namespace
{
FString Directory;
FString Peer;
bool bAuthorityFixtures = false;
FTSTicker::FDelegateHandle TickHandle;
int32 LastCommand = 0;
int32 Generation = 0;
TWeakObjectPtr<UWorld> LastWorld;

TSharedPtr<FJsonObject> Object() { return MakeShared<FJsonObject>(); }
void Number(const TSharedPtr<FJsonObject>& ObjectValue, const TCHAR* Key, double Value)
{
	ObjectValue->SetNumberField(Key, Value);
}
void Vector(const TSharedPtr<FJsonObject>& ObjectValue, const TCHAR* Key, FVector Value)
{
	TArray<TSharedPtr<FJsonValue>> Coordinates;
	Coordinates.Add(MakeShared<FJsonValueNumber>(Value.X));
	Coordinates.Add(MakeShared<FJsonValueNumber>(Value.Y));
	Coordinates.Add(MakeShared<FJsonValueNumber>(Value.Z));
	ObjectValue->SetArrayField(Key, Coordinates);
}
TSharedPtr<FJsonObject> Snapshot(UWorld* World)
{
	auto Result = Object();
	Number(Result, TEXT("generation"), Generation);
	Result->SetBoolField(TEXT("ready"), World != nullptr);
	if (!World) return Result;
	Number(Result, TEXT("netMode"), static_cast<int32>(World->GetNetMode()));
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	if (!State) { Result->SetBoolField(TEXT("ready"), false); return Result; }
#if DO_ENABLE_NET_TEST
	if (const UNetDriver* Driver = World->GetNetDriver())
	{
		Number(Result, TEXT("pktLag"), Driver->PacketSimulationSettings.PktLag);
		Number(Result, TEXT("pktLoss"), Driver->PacketSimulationSettings.PktLoss);
	}
#endif
	Number(Result, TEXT("result"), static_cast<int32>(State->MatchResult));
	Number(Result, TEXT("resourceSites"), State->ControlledResourceSites);
	Result->SetBoolField(TEXT("incomePaused"), State->bVerificationIncomePaused);
	auto Wallets = TArray<TSharedPtr<FJsonValue>>();
	for (const APlayerState* PS : State->PlayerArray)
	{
		const auto* Wallet = Cast<ACommandPlayerState>(PS);
		if (!IsValid(Wallet)) continue;
		auto Entry = Object();
		Number(Entry, TEXT("index"), Wallet->CommanderIndex);
		Number(Entry, TEXT("wallet"), Wallet->Resources);
		Number(Entry, TEXT("doctrine"), static_cast<int32>(Wallet->Doctrine));
		Wallets.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("players"), Wallets);
	int32 LocalIndex = -1;
	for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
		if (It->IsLocalController())
		{
			Result->SetStringField(TEXT("orderFeedback"), It->GetOrderFeedback());
			if (const auto* PS = It->GetPlayerState<ACommandPlayerState>())
				LocalIndex = PS->CommanderIndex;
			break;
		}
	Number(Result, TEXT("localIndex"), LocalIndex);
	auto Armies = TArray<TSharedPtr<FJsonValue>>();
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
	{
		const AArmyGroup* Group = *It;
		auto Entry = Object();
		Number(Entry, TEXT("owner"), IsValid(Group->OwningPlayerState) ? Group->OwningPlayerState->CommanderIndex : -1);
		Number(Entry, TEXT("team"), Group->TeamIndex);
		Number(Entry, TEXT("army"), Group->ArmyIndex);
		Number(Entry, TEXT("order"), static_cast<int32>(Group->Order));
		Number(Entry, TEXT("serial"), Group->OrderSerial);
		Number(Entry, TEXT("doctrine"), static_cast<int32>(Group->GetDoctrine()));
		Vector(Entry, TEXT("center"), Group->GetCenter());
		Vector(Entry, TEXT("destination"), Group->Destination);
		Vector(Entry, TEXT("home"), Group->HomeLocation);
		auto Units = TArray<TSharedPtr<FJsonValue>>();
		for (const AArmyUnit* Unit : Group->Units)
		{
			if (!IsValid(Unit)) continue;
			auto Member = Object();
			Number(Member, TEXT("slot"), Unit->CompositionSlot);
			Number(Member, TEXT("role"), static_cast<int32>(Unit->UnitRole));
			Number(Member, TEXT("health"), Unit->Health);
			Number(Member, TEXT("attacks"), Unit->AttackCount);
			Number(Member, TEXT("owner"), Unit->CommanderIndex);
			Vector(Member, TEXT("position"), Unit->GetActorLocation());
			Units.Add(MakeShared<FJsonValueObject>(Member));
		}
		Entry->SetArrayField(TEXT("units"), Units);
		Armies.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("armies"), Armies);
	auto Sites = TArray<TSharedPtr<FJsonValue>>();
	for (const ACapturePoint* Site : State->CaptureSites)
	{
		if (!IsValid(Site)) continue;
		auto Entry = Object();
		Number(Entry, TEXT("index"), Site->SiteIndex);
		Number(Entry, TEXT("owner"), Site->ControllingTeam);
		Number(Entry, TEXT("progress"), Site->CaptureProgress);
		Sites.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("sites"), Sites);
	Number(Result, TEXT("friendlyHQ"), IsValid(State->FriendlyHeadquarters) ? State->FriendlyHeadquarters->Health : -1);
	Number(Result, TEXT("enemyHQ"), IsValid(State->EnemyHeadquarters) ? State->EnemyHeadquarters->Health : -1);
	return Result;
}
AArmyGroup* FindArmy(UWorld* World, int32 Owner, int32 Index)
{
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		if (It->ArmyIndex == Index && IsValid(It->OwningPlayerState)
			&& It->OwningPlayerState->CommanderIndex == Owner) return *It;
	return nullptr;
}
ACommandPlayerController* LocalController(UWorld* World)
{
	for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
		if (It->IsLocalController()) return *It;
	return nullptr;
}
FString Execute(UWorld* World, const TSharedPtr<FJsonObject>& Request)
{
	if (!World) return TEXT("game world unavailable");
	FString Action;
	Request->TryGetStringField(TEXT("action"), Action);
	if (Action == TEXT("observe")) return FString();
	ACommandPlayerController* PC = LocalController(World);
	const ACommandPlayerState* Own = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
	const int32 Owner = static_cast<int32>(Request->GetIntegerField(TEXT("owner")));
	const int32 Index = static_cast<int32>(Request->GetIntegerField(TEXT("army")));
	if (Action == TEXT("doctrine") || Action == TEXT("restart") || Action == TEXT("order")
		|| Action == TEXT("attack") || Action == TEXT("buy"))
	{
		if (!Own || Own->CommanderIndex < 0) return TEXT("local owning controller unavailable");
		if (Action == TEXT("doctrine")) PC->ServerChooseDoctrine(static_cast<EArmyDoctrine>(Request->GetIntegerField(TEXT("choice"))));
		else if (Action == TEXT("restart")) PC->ServerRequestRestart();
		else
		{
			AArmyGroup* Army = FindArmy(World, Owner, Index);
			if (!Army) return TEXT("target army not replicated locally");
			if (Action == TEXT("buy"))
			{
				const int32 Repeats = FMath::Clamp(static_cast<int32>(Request->GetIntegerField(TEXT("repeat"))), 1, 32);
				for (int32 I = 0; I < Repeats; ++I) PC->ServerReinforce(Army);
			}
			else if (Action == TEXT("attack"))
			{
				ACommandGameState* State = World->GetGameState<ACommandGameState>();
				if (!State || !IsValid(State->EnemyHeadquarters)) return TEXT("enemy HQ not replicated");
				PC->ServerIssueAttack(Army, State->EnemyHeadquarters->GetActorLocation(), State->EnemyHeadquarters);
			}
			else
			{
				FVector Destination(Request->GetNumberField(TEXT("x")), Request->GetNumberField(TEXT("y")), 0.f);
				PC->ServerIssueOrder(Army, static_cast<EArmyOrder>(Request->GetIntegerField(TEXT("order"))), Destination);
			}
		}
		return FString();
	}
	// No fixture execution on clients, even if a malicious client sends fixture commands.
	if (!bAuthorityFixtures || !World->GetAuthGameMode() || World->GetNetMode() != NM_ListenServer)
		return TEXT("host-only authority fixture switch required");
	ACommandGameState* State = World->GetGameState<ACommandGameState>();
	AArmyGroup* Army = FindArmy(World, Owner, Index);
	if (Action == TEXT("isolate"))
	{
		for (TActorIterator<AEnemyCommander> It(World); It; ++It) It->Destroy();
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			if (It->TeamIndex == 5) It->IssueHold();
		return FString();
	}
	if (!State || !Army) return TEXT("server fixture army unavailable");
	if (Action == TEXT("income"))
	{
		State->bVerificationIncomePaused = Request->GetBoolField(TEXT("paused"));
		return FString();
	}
	if (Action == TEXT("fund"))
	{
		if (!IsValid(Army->OwningPlayerState)) return TEXT("fixture wallet unavailable");
		const int32 Amount = static_cast<int32>(Request->GetIntegerField(TEXT("amount")));
		if (Amount < 0 || Amount > 1000) return TEXT("fixture wallet amount out of bounds");
		Army->OwningPlayerState->Resources = Amount;
		Army->OwningPlayerState->ForceNetUpdate();
		return FString();
	}
	if (Action == TEXT("capture"))
	{
		const int32 SiteIndex = static_cast<int32>(Request->GetIntegerField(TEXT("site")));
		ACapturePoint* Site = nullptr;
		for (ACapturePoint* Candidate : State->CaptureSites)
			if (IsValid(Candidate) && Candidate->SiteIndex == SiteIndex) Site = Candidate;
		if (!Site || Army->Units.IsEmpty()) return TEXT("capture site or unit unavailable");
		Army->Units[0]->SetActorLocation(Site->GetActorLocation() + FVector(0.f, 0.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);
		return FString();
	}
	if (Action == TEXT("kill"))
	{
		const int32 Slot = static_cast<int32>(Request->GetIntegerField(TEXT("slot")));
		AArmyUnit* Victim = nullptr;
		for (AArmyUnit* Unit : Army->Units) if (IsValid(Unit) && Unit->CompositionSlot == Slot) Victim = Unit;
		AArmyUnit* Shooter = nullptr;
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			if (It->TeamIndex == 5 && !It->Units.IsEmpty()) { Shooter = It->Units[0]; break; }
		if (!Victim || !IsValid(Shooter)) return TEXT("live casualty or hostile shooter unavailable");
		const FVector Previous = Shooter->GetActorLocation();
		Victim->Health = 1; // Fixture shortens the encounter; the actual hostile weapon causes the death.
		Victim->ForceNetUpdate();
		Shooter->SetActorLocation(Victim->GetActorLocation() + FVector(90.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		Shooter->NextAttackTime = 0.f;
		Shooter->FireAt(Victim);
		Shooter->SetActorLocation(Previous, false, nullptr, ETeleportType::TeleportPhysics);
		return Victim->Health == 0 ? FString() : TEXT("hostile weapon did not kill casualty");
	}
	if (Action == TEXT("finish"))
	{
		const bool bWin = Request->GetBoolField(TEXT("win"));
		AHeadquarters* Target = bWin ? State->EnemyHeadquarters : State->FriendlyHeadquarters;
		AArmyUnit* Shooter = bWin && !Army->Units.IsEmpty() ? Army->Units[0] : nullptr;
		if (!bWin)
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				if (It->TeamIndex == 5 && !It->Units.IsEmpty()) { Shooter = It->Units[0]; break; }
		if (!IsValid(Target) || !IsValid(Shooter)) return TEXT("HQ or shooter unavailable");
		Target->Health = 1;
		Target->ForceNetUpdate();
		const FVector Previous = Shooter->GetActorLocation();
		Shooter->SetActorLocation(Target->GetActorLocation() + FVector(90.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		Shooter->NextAttackTime = 0.f;
		Shooter->FireAt(Target);
		Shooter->SetActorLocation(Previous, false, nullptr, ETeleportType::TeleportPhysics);
		return Target->Health == 0 ? FString() : TEXT("weapon did not destroy HQ");
	}
	return TEXT("unknown command");
}
bool Tick(float)
{
	UWorld* World = nullptr;
	if (GEngine)
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (UWorld* Candidate = Context.World())
				if (Candidate->IsGameWorld() && Candidate->GetNetMode() != NM_Standalone)
				{ World = Candidate; break; }
	if (World && World != LastWorld.Get()) { LastWorld = World; ++Generation; }
	FString Input;
	if (!FFileHelper::LoadFileToString(Input, *(Directory / TEXT("request.json")))) return true;
	TSharedPtr<FJsonObject> Request;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Input), Request) || !Request.IsValid()) return true;
	int32 Id = 0;
	if (!Request->TryGetNumberField(TEXT("id"), Id) || Id <= LastCommand) return true;
	LastCommand = Id;
	auto Reply = Object();
	Number(Reply, TEXT("id"), Id);
	Reply->SetStringField(TEXT("peer"), Peer);
	const FString Error = Execute(World, Request);
	Reply->SetStringField(TEXT("error"), Error);
	Reply->SetObjectField(TEXT("state"), Snapshot(World));
	FString Output;
	FJsonSerializer::Serialize(Reply.ToSharedRef(), TJsonWriterFactory<>::Create(&Output));
	FFileHelper::SaveStringToFile(Output, *(Directory / TEXT("reply.tmp")));
	FPlatformFileManager::Get().GetPlatformFile().MoveFile(*(Directory / TEXT("reply.json")), *(Directory / TEXT("reply.tmp")));
	UE_LOG(LogTemp, Display, TEXT("Network verification peer=%s id=%d error=%s"), *Peer, Id, *Error);
	return true;
}
} // namespace
void Start()
{
	if (!FParse::Value(FCommandLine::Get(), TEXT("CoopRTSNetVerifyDir="), Directory)
		|| !FParse::Value(FCommandLine::Get(), TEXT("CoopRTSNetVerifyPeer="), Peer)) return;
	bAuthorityFixtures = FParse::Param(FCommandLine::Get(), TEXT("CoopRTSNetVerifyAuthority"));
	if (Directory.IsEmpty() || Peer.IsEmpty()) return;
	UE_LOG(LogTemp, Display, TEXT("Network verification enabled peer=%s authorityFixtures=%d"), *Peer, bAuthorityFixtures);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), .1f);
}
void Stop()
{
	if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
}
} // namespace CoopRTSNetworkVerification
#endif
