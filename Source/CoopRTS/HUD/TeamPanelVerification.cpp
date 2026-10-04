#include "TeamPanelVerification.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
#include "Commands/GiftCommandComponent.h"
#include "HUDPanels.h"
#include "Json.h"
#include "TeamPanel.h"

namespace TeamPanelVerification
{
using namespace CommandHUDPanels;
using namespace CoopRTSNetworkVerification::Probe;

static ACommandPlayerState* Find(const ACommandGameState& State, int32 Slot)
{
	for (APlayerState* Player : State.PlayerArray)
		if (auto* Candidate = Cast<ACommandPlayerState>(Player); Candidate && Candidate->CommanderIndex == Slot)
			return Candidate;
	return nullptr;
}

static EEconomyResource ResourceOf(const FJsonObject& Request)
{
	return Request.GetStringField(TEXT("resource")) == TEXT("data") ? EEconomyResource::Data : EEconomyResource::Power;
}

// giftSend: the local commander sends through its owning component, as the Send button does.
static FString Send(ACommandGameState& State, ACommandPlayerController& Controller, const FJsonObject& Request)
{
	ACommandPlayerState* To = Find(State, static_cast<int32>(Request.GetIntegerField(TEXT("to"))));
	if (!To)
		return TEXT("gift recipient is not replicated locally");
	Controller.GiftCommands->ServerGift(To, To->CommanderIndex, ResourceOf(Request), static_cast<int32>(Request.GetIntegerField(TEXT("amount"))));
	return FString();
}

static int32 FreeSlot(const ACommandGameState& State)
{
	for (int32 Slot = 0; Slot < EconomyPolicy::MaxRecipients; ++Slot)
		if (!Find(State, Slot))
			return Slot;
	return INDEX_NONE;
}

// giftHudTeammates: tops the team up to `count` teammates and sets their wallets and Fortify cooldowns.
static FString Teammates(UWorld& World, ACommandGameState& State, const ACommandPlayerController& Controller, const FJsonObject& Request)
{
	const ACommandPlayerState* Own = Controller.GetPlayerState<ACommandPlayerState>();
	const int32 Count = static_cast<int32>(Request.GetIntegerField(TEXT("count")));
	if (!Own || Count < 0 || Count > TeamPanelPolicy::MaxTeammates)
		return TEXT("teammate fixture count out of bounds");
	TArray<const ACommandPlayerState*, TInlineAllocator<TeamPanelPolicy::MaxTeammates>> Mates;
	UGiftCommandComponent::Teammates(State, Own, Mates);
	while (Mates.Num() < Count)
	{
		const int32 Slot = FreeSlot(State);
		ACommandPlayerState* Wallet = Slot == INDEX_NONE ? nullptr : World.SpawnActor<ACommandPlayerState>();
		if (!Wallet)
			return TEXT("teammate fixture wallet spawn failed");
		Wallet->CommanderIndex = Slot;
		Wallet->TeamIndex = Own->TeamIndex;
		Wallet->SetPlayerName(FString::Printf(TEXT("Fixture C%d"), Slot + 1));
		State.AddPlayerState(Wallet);
		UGiftCommandComponent::Teammates(State, Own, Mates);
	}
	const float Now = State.GetServerWorldTimeSeconds();
	for (int32 Index = 0; Index < Mates.Num(); ++Index)
	{
		ACommandPlayerState* Wallet = const_cast<ACommandPlayerState*>(Mates[Index]);
		Wallet->Resources = static_cast<int32>(Request.GetIntegerField(TEXT("power")));
		Wallet->Data = static_cast<int32>(Request.GetIntegerField(TEXT("data")));
		Wallet->FortifyReadyAt = Index == 0 ? Now + 47.f : 0.f;
		Wallet->ForceNetUpdate();
	}
	return FString();
}

// giftHudGift: an authority gift from `giver` to `to`, funding the sender first when `fund` is set.
static FString Gift(ACommandGameState& State, const FJsonObject& Request)
{
	ACommandPlayerState* From = Find(State, static_cast<int32>(Request.GetIntegerField(TEXT("giver"))));
	ACommandPlayerState* To = Find(State, static_cast<int32>(Request.GetIntegerField(TEXT("to"))));
	const int32 Amount = static_cast<int32>(Request.GetIntegerField(TEXT("amount")));
	const EEconomyResource Resource = ResourceOf(Request);
	if (!From || !To || Amount < 1 || Amount > 1000)
		return TEXT("gift fixture commanders or amount unavailable");
	bool bFund = false;
	Request.TryGetBoolField(TEXT("fund"), bFund);
	if (bFund)
		(Resource == EEconomyResource::Power ? From->Resources : From->Data) += Amount;
	const FCommandResult Result = FCommandService::Gift(From, To, Resource, Amount);
	return Result.IsAccepted() ? FString() : Result.Message;
}

// giftFund: sets one commander's Power and Data.
static FString Fund(ACommandGameState& State, const FJsonObject& Request)
{
	ACommandPlayerState* Wallet = Find(State, static_cast<int32>(Request.GetIntegerField(TEXT("owner"))));
	const int32 Power = static_cast<int32>(Request.GetIntegerField(TEXT("power")));
	const int32 Data = static_cast<int32>(Request.GetIntegerField(TEXT("data")));
	if (!Wallet || Power < 0 || Power > 1000 || Data < 0 || Data > 1000)
		return TEXT("gift fixture wallet unavailable or out of bounds");
	Wallet->Resources = Power;
	Wallet->Data = Data;
	Wallet->ForceNetUpdate();
	return FString();
}

// giftHudLayout: the open panel sits in the alert column and clear of the footer and Pause.
static FString CheckLayout(ACommandPlayerController& Controller)
{
	int32 Width, Height;
	Controller.GetViewportSize(Width, Height);
	const FContext Context = MakeContext(&Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	if (!Controller.IsTeamPanelOpen() || Layout.TeamPanel.W != 390.f)
		return TEXT("Team panel is not open at its 390 px width");
	if (Layout.TeamPanel.Bottom() > Layout.Build.Y || Layout.TeamPanel.Intersects(Layout.Build)
		|| Layout.TeamPanel.Intersects(Layout.Pause) || Layout.TeamPanel.Intersects(Layout.Objectives)
		|| Layout.TeamPanel.Intersects(Layout.ForceBar) || Layout.TeamPanel.Intersects(Layout.Minimap))
		return TEXT("Team panel overlaps the footer, Pause or the strip");
	if (Layout.Alerts.H != 0.f)
		return TEXT("the alert feed still takes the panel's column");
	return FString();
}

bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error)
{
	FString Action;
	Request->TryGetStringField(TEXT("action"), Action);
	if (!Action.StartsWith(TEXT("gift")))
		return false;
	ACommandPlayerController* Controller = LocalController(&World);
	ACommandGameState* State = World.GetGameState<ACommandGameState>();
	if (!Controller || !State)
	{
		Error = TEXT("Team panel world unavailable");
		return true;
	}
	if (Action == TEXT("giftSend"))
		Error = Send(*State, *Controller, *Request);
	else if (Action == TEXT("giftHudLayout"))
		Error = CheckLayout(*Controller);
	else if (!bAuthorityFixtures || World.GetNetMode() != NM_ListenServer || !World.GetAuthGameMode())
		Error = TEXT("Team panel fixture requires opted-in authority host");
	else if (Action == TEXT("giftHudTeammates"))
		Error = Teammates(World, *State, *Controller, *Request);
	else if (Action == TEXT("giftHudGift"))
		Error = Gift(*State, *Request);
	else if (Action == TEXT("giftFund"))
		Error = Fund(*State, *Request);
	else if (Action == TEXT("giftHudClear"))
	{
		State->GiftLog.Reset();
		State->ForceNetUpdate();
	}
	else
		Error = TEXT("unknown Team panel action");
	return true;
}

void Snapshot(UWorld& World, const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	auto Team = Object();
	Number(Team, TEXT("serverNow"), State.GetServerWorldTimeSeconds());
	if (const ACommandPlayerController* Controller = LocalController(&World))
	{
		const TeamPanelPolicy::FFlow& Flow = Controller->GetTeamFlow();
		Team->SetBoolField(TEXT("open"), Flow.bOpen);
		Number(Team, TEXT("teammate"), Flow.Teammate);
		Team->SetStringField(TEXT("resource"), TeamPanelPolicy::ResourceName(Flow.Resource));
		Number(Team, TEXT("amount"), Flow.Amount);
		Number(Team, TEXT("scroll"), Flow.LogScroll);
		Team->SetBoolField(TEXT("unseen"), Controller->HasUnseenGift());
		FString Refusal;
		float Opacity;
		Team->SetStringField(TEXT("refusal"), Controller->GetTeamRefusal(Refusal, Opacity) ? Refusal : FString());
	}
	auto Commanders = TArray<TSharedPtr<FJsonValue>>();
	for (const APlayerState* Player : State.PlayerArray)
		if (const auto* Wallet = Cast<ACommandPlayerState>(Player); IsValid(Wallet))
		{
			auto Entry = Object();
			Number(Entry, TEXT("index"), Wallet->CommanderIndex);
			Number(Entry, TEXT("team"), Wallet->TeamIndex);
			Number(Entry, TEXT("power"), Wallet->Resources);
			Number(Entry, TEXT("data"), Wallet->Data);
			Commanders.Add(MakeShared<FJsonValueObject>(Entry));
		}
	Team->SetArrayField(TEXT("commanders"), Commanders);
	auto Log = TArray<TSharedPtr<FJsonValue>>();
	for (const FGiftLogEntry& Gift : State.GiftLog)
	{
		auto Entry = Object();
		Number(Entry, TEXT("sender"), Gift.SenderSlot);
		Number(Entry, TEXT("recipient"), Gift.RecipientSlot);
		Entry->SetStringField(TEXT("resource"), Gift.Resource == EEconomyResource::Power ? TEXT("power") : TEXT("data"));
		Number(Entry, TEXT("amount"), Gift.Amount);
		Log.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Team->SetArrayField(TEXT("log"), Log);
	Result->SetObjectField(TEXT("team"), Team);
}
}
#endif
