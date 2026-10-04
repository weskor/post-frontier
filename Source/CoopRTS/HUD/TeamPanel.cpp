#include "TeamPanel.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "Commands/GiftCommandComponent.h"

namespace CommandHUDPanels
{
using namespace TeamPanelPolicy;

FTeamGeometry TeamGeometry(const FRect& Panel, int32 Teammates)
{
	FTeamGeometry G;
	const float X = Panel.X, W = Panel.W;
	G.bGift = Teammates > 0;
	G.Header = { X, Panel.Y, W, 26.f };
	// The drawing is a small keycap; the hit area is the 28 px minimum, ending where the first row begins.
	G.Close = { Panel.Right() - 76.f, Panel.Y, 72.f, 28.f };
	float Y = Panel.Y + 28.f;
	const int32 Rows = FMath::Clamp(Teammates, 1, MaxTeammates);
	for (int32 Index = 0; Index < Rows; ++Index, Y += 30.f)
		G.Rows[Index] = { X + 6.f, Y, W - 12.f, 28.f };
	if (G.bGift)
	{
		G.Label = { X + 10.f, Y, W - 20.f, 16.f };
		Y += 16.f;
		G.Power = { X + 6.f, Y, 66.f, 28.f };
		G.Data = { X + 76.f, Y, 66.f, 28.f };
		for (int32 Index = 0; Index < PresetCount; ++Index)
			G.Presets[Index] = { X + 152.f + Index * 56.f, Y, 52.f, 28.f };
		Y += 32.f;
		G.Minus = { X + 6.f, Y, 28.f, 30.f };
		G.Amount = { X + 36.f, Y, 64.f, 30.f };
		G.Plus = { X + 102.f, Y, 28.f, 30.f };
		G.Send = { X + 138.f, Y, W - 144.f, 30.f };
		Y += 34.f;
		G.Refusal = { X + 10.f, Y, W - 20.f, 16.f };
		Y += 16.f;
	}
	G.LogLabel = { X + 10.f, Y, W - 60.f, 14.f };
	Y += 14.f;
	for (int32 Index = 0; Index < LogRows; ++Index)
		G.LogLines[Index] = { X + 10.f, Y + Index * 19.f, W - 56.f, 19.f };
	G.LogUp = { Panel.Right() - 34.f, Y, 28.f, 28.f };
	G.LogDown = { Panel.Right() - 34.f, Y + 30.f, 28.f, 28.f };
	Y += 64.f;
	G.Panel = { X, Panel.Y, W, Y - Panel.Y };
	G.Height = G.Panel.H;
	return G;
}

int32 TeammateCount(const FContext& Context)
{
	if (!Context.State)
		return 0;
	TArray<const ACommandPlayerState*, TInlineAllocator<MaxTeammates>> Mates;
	UGiftCommandComponent::Teammates(*Context.State, Context.Wallet, Mates);
	return Mates.Num();
}

bool IsTeamAction(EHUDAction Action)
{
	return Action >= EHUDAction::TeamToggle && Action <= EHUDAction::TeamLogDown;
}

FSendInput TeamSendInput(const FContext& Context)
{
	if (!Context.State || !Context.Controller)
		return FSendInput();
	const FFlow& Flow = Context.Controller->GetTeamFlow();
	return UGiftCommandComponent::MakeSendInput(*Context.State, Context.Wallet, Flow.Teammate, Flow.Resource, Flow.Amount);
}

FFlash GiftFlash(const FContext& Context)
{
	if (!Context.State || !Context.Wallet)
		return FFlash();
	FLogBuffer Log;
	UGiftCommandComponent::ReadLog(*Context.State, Log);
	return TeamPanelPolicy::GiftFlash(Log, Context.Wallet->CommanderIndex,
		Context.State->GetServerWorldTimeSeconds() - Context.State->GetBattleClockStartServerTime());
}

static EHUDAction RowAction(int32 Row)
{
	return static_cast<EHUDAction>(static_cast<uint8>(EHUDAction::TeamRow0) + Row);
}

static void ForEachGiftControl(const FContext& Context, const FTeamGeometry& G, const FFlow& Flow,
	TFunctionRef<void(const FButton&)> Visit)
{
	const int32 Funds = Flow.Resource == EResource::Power ? (Context.Wallet ? Context.Wallet->Resources : 0)
														  : Context.DataBalance;
	const EBlock Send = Verdict(TeamSendInput(Context)) == ESendVerdict::Ok ? EBlock::None : EBlock::Funds;
	Visit({ EHUDAction::TeamPower, G.Power, EBlock::None, Flow.Resource == EResource::Power, 0 });
	Visit({ EHUDAction::TeamData, G.Data, EBlock::None, Flow.Resource == EResource::Data, 0 });
	for (int32 Index = 0; Index < PresetCount; ++Index)
		Visit({ static_cast<EHUDAction>(static_cast<uint8>(EHUDAction::TeamPreset0) + Index), G.Presets[Index], EBlock::None,
			Flow.Amount == PresetAmount(Flow.Resource, Index, Funds), 0 });
	Visit({ EHUDAction::TeamStepDown, G.Minus, EBlock::None, false, 0 });
	Visit({ EHUDAction::TeamStepUp, G.Plus, EBlock::None, false, 0 });
	Visit({ EHUDAction::TeamSend, G.Send, Send, false, 0 });
}

void ForEachTeamButton(const FContext& Context, const FLayout& Layout, TFunctionRef<void(const FButton&)> Visit)
{
	if (!Context.Controller)
		return;
	const FFlow& Flow = Context.Controller->GetTeamFlow();
	Visit({ EHUDAction::TeamToggle, Layout.TeamButton, EBlock::None, Flow.bOpen, 0 });
	if (!Flow.bOpen || Layout.TeamPanel.W <= 0.f || !Context.State)
		return;
	TArray<const ACommandPlayerState*, TInlineAllocator<MaxTeammates>> Mates;
	UGiftCommandComponent::Teammates(*Context.State, Context.Wallet, Mates);
	const FTeamGeometry G = TeamGeometry(Layout.TeamPanel, Mates.Num());
	Visit({ EHUDAction::TeamClose, G.Close, EBlock::None, false, 0 });
	for (int32 Row = 0; Row < Mates.Num(); ++Row)
		Visit({ RowAction(Row), G.Rows[Row], EBlock::None, Flow.Teammate == Mates[Row]->CommanderIndex, 0 });
	if (G.bGift)
		ForEachGiftControl(Context, G, Flow, Visit);
	TeamPanelPolicy::FLogBuffer Log;
	UGiftCommandComponent::ReadLog(*Context.State, Log);
	const int32 MaxScroll = FMath::Max(0, Log.Num() - LogRows);
	Visit({ EHUDAction::TeamLogUp, G.LogUp, Flow.LogScroll > 0 ? EBlock::None : EBlock::Chosen, false, 0 });
	Visit({ EHUDAction::TeamLogDown, G.LogDown, Flow.LogScroll < MaxScroll ? EBlock::None : EBlock::Chosen, false, 0 });
}
}
