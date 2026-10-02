#include "CoopSessionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "OnlineSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Containers/Ticker.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"

namespace
{
FAutoConsoleCommandWithWorldAndArgs VerifySteamSession(
	TEXT("CoopSteam.Verify"), TEXT("Development only: state, checksum, reject, invite-refusal (solo-host fixture)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
		if (World && World->GetGameInstance())
			World->GetGameInstance()->GetSubsystem<UCoopSessionSubsystem>()->VerifySession(Args);
	}));
}

void UCoopSessionSubsystem::VerifySession(const TArray<FString>& Args)
{
	if (Args.Num() == 1 && Args[0] == TEXT("checksum") && CanInvite())
		GEngine->BroadcastNetworkFailure(GetWorld(), GetWorld()->GetNetDriver(), ENetworkFailure::NetChecksumMismatch, TEXT("Developer stale-client checksum"));
	else if (Args.Num() == 1 && Args[0] == TEXT("invite-refusal") && CanInvite()
		&& GetWorld()->GetGameState() && GetWorld()->GetGameState()->PlayerArray.Num() == 1)
	{
		// A temporary roster entry exercises the guard, not remote networking.
		APlayerState* Guest = GetWorld()->SpawnActor<APlayerState>();
		if (Guest)
		{
			FOnlineSessionSearchResult Result;
			Result.Session = *Sessions->GetNamedSession(NAME_GameSession);
			OnInviteAccepted(true, 0, IOnlineSubsystem::Get()->GetIdentityInterface()->GetUniquePlayerId(0), Result);
			Guest->Destroy();
		}
	}
	else if (Args.Num() == 1 && Args[0] == TEXT("reject") && CanInvite()
		&& GetWorld()->GetGameState() && GetWorld()->GetGameState()->PlayerArray.Num() == 1)
	{
		// Rejoin the actual Steam lobby without a second account. Keep its Steam membership
		// until JoinSession completes; inject only the pending handshake's rejection reason.
		PendingInvite.Session = *Sessions->GetNamedSession(NAME_GameSession);
		Sessions->RemoveNamedSession(NAME_GameSession);
		bHosting = false;
		bVerifyPendingRejection = true;
		JoinPendingInvite();
	}
	UE_LOG(LogTemp, Display, TEXT("CoopSteamVerify: host=%d busy=%d session=%d canHost=%d status=%s"),
		bHosting, IsBusy(), Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr, CanHost(), *GetStatus());
}
#endif

void UCoopSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	IOnlineSubsystem* Online = IOnlineSubsystem::Get();
	if (Online && Online->GetSubsystemName() == FName(TEXT("STEAM")))
		Sessions = Online->GetSessionInterface();
	if (Sessions.IsValid())
	{
		CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnCreated));
		JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnJoined));
		DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::OnDestroyed));
		InviteHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &ThisClass::OnInviteAccepted));
	}
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::OnNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::OnTravelFailure);
	}
}

void UCoopSessionSubsystem::Deinitialize()
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	bVerifyPendingRejection = false;
#endif
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
		Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteHandle);
		if (Sessions->GetNamedSession(NAME_GameSession))
			Sessions->DestroySession(NAME_GameSession);
		Sessions.Reset();
	}
	Super::Deinitialize();
}

bool UCoopSessionSubsystem::IsSteamAvailable() const
{
	IOnlineSubsystem* Online = IOnlineSubsystem::Get();
	if (!Sessions.IsValid() || !Online || Online->GetSubsystemName() != FName(TEXT("STEAM")))
		return false;
	const IOnlineIdentityPtr Identity = Online->GetIdentityInterface();
	return Identity.IsValid() && Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn;
}

bool UCoopSessionSubsystem::CanHost() const
{
	return IsSteamAvailable() && Operation == EOperation::Idle && !Sessions->GetNamedSession(NAME_GameSession);
}

bool UCoopSessionSubsystem::CanInvite() const
{
	return IsSteamAvailable() && bHosting && Operation == EOperation::Idle
		&& GetWorld() && GetWorld()->GetNetMode() == NM_ListenServer;
}

FString UCoopSessionSubsystem::GetStatus() const
{
	if (!Message.IsEmpty())
		return Message;
	if (!IsSteamAvailable())
		return TEXT("Steam unavailable - start Steam and log in. Solo works offline.");
	if (bHosting || (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession)))
	{
		const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
		return FString::Printf(TEXT("%s / players %d/5"), bHosting ? TEXT("Hosting on Steam") : TEXT("Steam co-op"), State ? State->PlayerArray.Num() : 1);
	}
	return TEXT("Steam ready / co-op for up to 5 commanders");
}

void UCoopSessionSubsystem::SetMessage(const FString& Text)
{
	Message = Text;
	UE_LOG(LogTemp, Display, TEXT("CoopSteam: %s"), *Text);
}

void UCoopSessionSubsystem::Host()
{
	if (!CanHost())
	{
		SetMessage(IsSteamAvailable() ? TEXT("A Steam session is already active or busy.") : TEXT("Steam unavailable - start Steam and log in. Solo works offline."));
		return;
	}
	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = 5;
	Settings.NumPrivateConnections = 0;
	Settings.bIsLANMatch = false;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bAllowInvites = true;
	Settings.bUsesPresence = true;
	Settings.bUseLobbiesIfAvailable = true;
	Settings.bAllowJoinViaPresence = true;
	Settings.bAllowJoinViaPresenceFriendsOnly = true;
	Operation = EOperation::Creating;
	SetMessage(TEXT("Creating Steam lobby (5 commanders)..."));
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings) && Operation == EOperation::Creating)
		OnCreated(NAME_GameSession, false);
}

void UCoopSessionSubsystem::OnCreated(FName Name, bool bSuccess)
{
	if (Name != NAME_GameSession || Operation != EOperation::Creating)
		return;
	Operation = EOperation::Idle;
	if (AfterDestroy != EAfterDestroy::None)
	{
		DestroyThen(AfterDestroy);
		return;
	}
	if (!bSuccess)
	{
		FailToMenu(TEXT("Steam lobby creation failed. Try again or play solo."));
		return;
	}
	bHosting = true;
	Message.Empty();
	UE_LOG(LogTemp, Display, TEXT("CoopSteam: lobby created public=5; opening %s listen"), *GetSelectedMap().ToString());
	UGameplayStatics::OpenLevel(GetGameInstance(), GetSelectedMap(), true, TEXT("listen"));
}

void UCoopSessionSubsystem::Invite()
{
	if (!CanInvite())
	{
		SetMessage(TEXT("Invites require a hosted Steam match and a logged-in Steam client."));
		return;
	}
	const IOnlineExternalUIPtr UI = IOnlineSubsystem::Get()->GetExternalUIInterface();
	if (!UI.IsValid() || !UI->ShowInviteUI(0, NAME_GameSession))
		SetMessage(TEXT("Steam invite overlay unavailable. Enable the Steam overlay."));
	else
	{
		Message.Empty();
		UE_LOG(LogTemp, Display, TEXT("CoopSteam: invite overlay requested"));
	}
}

void UCoopSessionSubsystem::OnInviteAccepted(bool bSuccess, int32 LocalUser, FUniqueNetIdPtr User, const FOnlineSessionSearchResult& Result)
{
	if (!bSuccess || !Result.IsValid() || !User.IsValid() || !IsSteamAvailable())
	{
		SetMessage(TEXT("Steam invitation unavailable or expired. Ask the host to invite again."));
		return;
	}
	if (Operation != EOperation::Idle)
	{
		SetMessage(TEXT("Steam session is busy. Accept the invitation again when it finishes."));
		return;
	}
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (bHosting && State && State->PlayerArray.Num() > 1)
	{
		SetMessage(TEXT("Cannot join an invitation while hosting other players. Leave the match first."));
		return;
	}
	PendingInvite = Result;
	InviteUser = LocalUser;
	DestroyThen(EAfterDestroy::Join);
}

void UCoopSessionSubsystem::JoinPendingInvite()
{
	AfterDestroy = EAfterDestroy::None;
	if (!IsSteamAvailable() || !PendingInvite.IsValid())
	{
		FailToMenu(TEXT("Steam join unavailable. Start Steam and request a new invite."));
		return;
	}
	Operation = EOperation::Joining;
	SetMessage(TEXT("Joining Steam lobby..."));
	if (!Sessions->JoinSession(InviteUser, NAME_GameSession, PendingInvite) && Operation == EOperation::Joining)
		OnJoined(NAME_GameSession, EOnJoinSessionCompleteResult::UnknownError);
	PendingInvite = FOnlineSessionSearchResult();
}

void UCoopSessionSubsystem::OnJoined(FName Name, EOnJoinSessionCompleteResult::Type Result)
{
	if (Name != NAME_GameSession || Operation != EOperation::Joining)
		return;
	Operation = EOperation::Idle;
	if (AfterDestroy != EAfterDestroy::None)
	{
		DestroyThen(AfterDestroy);
		return;
	}
	FString Address;
	APlayerController* Controller = GetGameInstance()->GetFirstLocalPlayerController();
	if (Result != EOnJoinSessionCompleteResult::Success || !Controller
		|| !Sessions->GetResolvedConnectString(NAME_GameSession, Address) || Address.IsEmpty())
	{
		FailToMenu(FString::Printf(TEXT("Steam join failed (%s). Ask for a new invite."), LexToString(Result)));
		return;
	}
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	// Do not connect Steam to its own identity: cancellation races its local listener.
	// The rejection fixture still joins the real lobby, then uses an unused loopback port.
	if (bVerifyPendingRejection)
		Address = TEXT("127.0.0.1:1");
#endif
	SetMessage(TEXT("Connecting to Steam host..."));
	UE_LOG(LogTemp, Display, TEXT("CoopSteam: lobby joined; ClientTravel %s"), *Address);
	Controller->SetPause(false);
	Controller->ClientTravel(Address, TRAVEL_Absolute);
	Message.Empty();
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	if (bVerifyPendingRejection)
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float) {
			if (!bVerifyPendingRejection)
				return false;
			FWorldContext* Context = GetGameInstance()->GetWorldContext();
			if (!Context || !Context->PendingNetGame)
				return true;
			Context->PendingNetGame->ConnectionError = TEXT("Match full (developer verification)");
			bVerifyPendingRejection = false;
			UE_LOG(LogTemp, Display, TEXT("CoopSteamVerify: rejecting real pending join"));
			return false;
		}));
	}
#endif
}

void UCoopSessionSubsystem::Leave(bool bQuit)
{
	Message.Empty();
	DestroyThen(bQuit ? EAfterDestroy::Quit : EAfterDestroy::Menu);
}

void UCoopSessionSubsystem::DestroyThen(EAfterDestroy Next)
{
	AfterDestroy = Next;
	// Async create/join must finish before destroying their named session.
	if (Operation != EOperation::Idle)
		return;
	if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession))
	{
		FinishDeparture();
		return;
	}
	Operation = EOperation::Destroying;
	UE_LOG(LogTemp, Display, TEXT("CoopSteam: destroying session"));
	if (!Sessions->DestroySession(NAME_GameSession) && Operation == EOperation::Destroying)
		OnDestroyed(NAME_GameSession, false);
}

void UCoopSessionSubsystem::OnDestroyed(FName Name, bool bSuccess)
{
	if (Name != NAME_GameSession || Operation != EOperation::Destroying)
		return;
	Operation = EOperation::Idle;
	if (!bSuccess)
	{
		AfterDestroy = EAfterDestroy::None;
		SetMessage(TEXT("Steam session cleanup failed. Retry Leave or Quit."));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("CoopSteam: session destroyed"));
	FinishDeparture();
}

void UCoopSessionSubsystem::FinishDeparture()
{
	bHosting = false;
	const EAfterDestroy Next = AfterDestroy;
	AfterDestroy = EAfterDestroy::None;
	if (Next == EAfterDestroy::Join)
	{
		JoinPendingInvite();
		return;
	}
	APlayerController* Controller = GetGameInstance()->GetFirstLocalPlayerController();
	if (Controller)
		Controller->SetPause(false);
	if (Next == EAfterDestroy::Quit)
		UKismetSystemLibrary::QuitGame(GetGameInstance(), Controller, EQuitPreference::Quit, false);
	else if (Next == EAfterDestroy::Menu)
		UGameplayStatics::OpenLevel(GetGameInstance(), TEXT("/Game/Maps/Menu"));
}

void UCoopSessionSubsystem::FailToMenu(const FString& Text)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	bVerifyPendingRejection = false;
#endif
	SetMessage(Text);
	DestroyThen(EAfterDestroy::Menu);
}

void UCoopSessionSubsystem::OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
	if (World)
	{
		if (World->GetGameInstance() != GetGameInstance())
			return;
		// Only a broken listener may end the host lobby; client faults are isolated.
		if (World->GetNetMode() == NM_ListenServer
			&& Type != ENetworkFailure::NetDriverListenFailure
			&& Type != ENetworkFailure::NetDriverCreateFailure
			&& Type != ENetworkFailure::NetDriverAlreadyExists)
			return;
	}
	else
	{
		// Pending handshakes have no world. Scope the driver to this game instance,
		// not every subsystem subscribed to the engine-wide failure delegate.
		if (!GEngine || !Driver || Driver->NetDriverName != NAME_PendingNetDriver)
			return;
		const FWorldContext* Context = GEngine->GetWorldContextFromPendingNetGameNetDriver(Driver);
		if (!Context || Context->OwningGameInstance != GetGameInstance())
			return;
	}
	if (AfterDestroy != EAfterDestroy::None || Operation == EOperation::Destroying)
		return;
	FailToMenu(FString::Printf(TEXT("Connection failed: %s"), *Error));
}

void UCoopSessionSubsystem::OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
	if (!World || World->GetGameInstance() != GetGameInstance() || AfterDestroy != EAfterDestroy::None)
		return;
	FailToMenu(FString::Printf(TEXT("Travel failed: %s"), *Error));
}
