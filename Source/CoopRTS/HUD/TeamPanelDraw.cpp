#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "Commands/GiftCommandComponent.h"
#include "Rules/FortifyPolicy.h"
#include "Rules/JevIntent.h"
#include "TeamPanel.h"

namespace CommandHUDPanels
{
using namespace TeamPanelPolicy;

namespace
{
const FLinearColor Cyan(.42f, .90f, 1.f);
const FLinearColor TealFill(.07f, .26f, .31f, 1.f);
const FLinearColor PanelFill(.07f, .10f, .13f, .97f);
const FLinearColor FieldFill(.04f, .06f, .08f, 1.f);

using FMates = TArray<const ACommandPlayerState*, TInlineAllocator<MaxTeammates>>;

void DrawBox(const FPainter& Paint, const FRect& Rect, bool bActive, bool bHover, bool bDim)
{
	Paint.Fill(Rect, bDim ? Palette::CardOff : bActive ? TealFill
			: bHover                                   ? Palette::CardHover
													   : Palette::Key);
	Paint.Outline(Rect, bActive ? Cyan : Palette::KeyEdge, bActive ? 1.6f : 1.f);
}

void DrawOpener(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	const FRect& Rect = Button.Rect;
	Paint.Fill(Rect, bHover ? Palette::CardHover : Palette::Panel);
	Paint.Outline(Rect, Button.bActive ? Cyan : Palette::Edge);
	const float Width = Paint.KeyWidth(TEXT("Tab"), TEXT("TEAM"));
	Paint.DrawKey(Rect.X + (Rect.W - Width) * .5f, Rect.Y + (Rect.H - KeyHeight) * .5f, TEXT("Tab"), TEXT("TEAM"));
	if (Context.Controller && Context.Controller->HasUnseenGift())
	{
		const FRect Dot{ Rect.Right() - 14.f, Rect.Y + 3.f, 10.f, 10.f };
		Paint.Fill(Dot, Palette::Gold);
		Paint.Outline(Dot, Palette::Panel);
	}
}

void DrawFortifyChip(const FPainter& Paint, const FContext& Context, const ACommandPlayerState& Wallet, const FRect& Row)
{
	TStringBuilder<32> Text;
	const float Now = Context.State->GetServerWorldTimeSeconds();
	AppendFortifyChip(Text, Wallet.FortifyReadyAt, Now);
	const bool bReady = Wallet.FortifyReadyAt <= Now;
	const float Width = Paint.TextWidth(Text.ToView(), 8.5f, true) + 14.f;
	const FRect Chip{ Row.Right() - Width - 6.f, Row.Y + 5.5f, Width, 17.f };
	Paint.Fill(Chip, bReady ? FLinearColor(.05f, .20f, .25f, 1.f) : FLinearColor(.17f, .17f, .17f, 1.f));
	Paint.Outline(Chip, bReady ? Cyan : Palette::Faint);
	Paint.TextIn(Text.ToView(), Chip, 8.5f, bReady ? Palette::Text : Palette::Muted, true, EAlign::Center);
}

void DrawTeammateRow(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	FMates Mates;
	UGiftCommandComponent::Teammates(*Context.State, Context.Wallet, Mates);
	const int32 Index = static_cast<int32>(Button.Action) - static_cast<int32>(EHUDAction::TeamRow0);
	if (!Mates.IsValidIndex(Index))
		return;
	const ACommandPlayerState& Wallet = *Mates[Index];
	const FRect& Rect = Button.Rect;
	DrawBox(Paint, Rect, Button.bActive, bHover, false);
	const FLinearColor Color = AArmyUnit::GetCommanderColor(Wallet.CommanderIndex);
	Paint.Fill({ Rect.X, Rect.Y, 5.f, Rect.H }, Color);
	TStringBuilder<32> Name;
	AppendCommander(Name, Wallet.CommanderIndex);
	Paint.TextIn(Name.ToView(), { Rect.X + 12.f, Rect.Y, 26.f, Rect.H }, 11.f, Color, true);
	TStringBuilder<48> Power, Data;
	Power.Appendf(TEXT("%d Power "), Wallet.Resources);
	AppendRate(Power, Context.State->GetPowerRate(&Wallet));
	Data.Appendf(TEXT("%d Data "), Wallet.Data);
	AppendRate(Data, Context.State->GetDataRate(&Wallet));
	Paint.TextIn(Power.ToView(), { Rect.X + 40.f, Rect.Y, 96.f, Rect.H }, 10.f, Palette::Gold, true);
	Paint.TextIn(Data.ToView(), { Rect.X + 138.f, Rect.Y, 90.f, Rect.H }, 10.f, Palette::Text, true);
	DrawFortifyChip(Paint, Context, Wallet, Rect);
}

void DrawGiftControl(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	const FFlow& Flow = Context.Controller->GetTeamFlow();
	const int32 Ordinal = static_cast<int32>(Button.Action);
	const int32 Preset = Ordinal - static_cast<int32>(EHUDAction::TeamPreset0);
	const bool bDim = !Button.Available();
	TStringBuilder<64> Label;
	if (Button.Action == EHUDAction::TeamPower || Button.Action == EHUDAction::TeamData)
		Label << (Button.Action == EHUDAction::TeamPower ? TEXT("POWER") : TEXT("DATA"));
	else if (Preset == AllPreset)
		Label << TEXT("ALL");
	else if (Preset >= 0 && Preset < AllPreset)
		Label.Appendf(TEXT("%d"), PresetAmount(Flow.Resource, Preset, 0));
	else if (Button.Action == EHUDAction::TeamStepDown)
		Label << TEXT("\u2212");
	else if (Button.Action == EHUDAction::TeamStepUp)
		Label << TEXT("+");
	else
		AppendSendLabel(Label, Flow);
	const bool bSend = Button.Action == EHUDAction::TeamSend;
	DrawBox(Paint, Button.Rect, Button.bActive || (bSend && !bDim), bHover, bDim);
	Paint.TextIn(Label.ToView(), Button.Rect, bSend ? 11.f : 10.f, bDim ? Palette::Muted : Palette::Text, true, EAlign::Center);
}

void DrawLogButton(const FPainter& Paint, const FButton& Button, bool bHover)
{
	DrawBox(Paint, Button.Rect, false, bHover, !Button.Available());
	Paint.TextIn(Button.Action == EHUDAction::TeamLogUp ? TEXT("\u25B2") : TEXT("\u25BC"), Button.Rect, 10.f,
		Button.Available() ? Palette::Text : Palette::Faint, true, EAlign::Center);
}
}

void DrawTeamButton(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	if (!Context.Controller || !Context.State)
		return;
	switch (Button.Action)
	{
	case EHUDAction::TeamToggle:
		DrawOpener(Paint, Context, Button, bHover);
		break;
	case EHUDAction::TeamClose:
		Paint.DrawKey(Button.Rect.X + 4.f, Button.Rect.Y + 4.f, TEXT("Tab"), TEXT("Close"));
		break;
	case EHUDAction::TeamRow0:
	case EHUDAction::TeamRow1:
	case EHUDAction::TeamRow2:
		DrawTeammateRow(Paint, Context, Button, bHover);
		break;
	case EHUDAction::TeamLogUp:
	case EHUDAction::TeamLogDown:
		DrawLogButton(Paint, Button, bHover);
		break;
	default:
		DrawGiftControl(Paint, Context, Button, bHover);
	}
}

static void DrawGiftReadout(const FPainter& Paint, const FContext& Context, const FTeamGeometry& G)
{
	const FFlow& Flow = Context.Controller->GetTeamFlow();
	TStringBuilder<64> Label;
	if (Flow.Teammate == INDEX_NONE)
		Label << TEXT("GIFT: pick a teammate");
	else
	{
		Label << TEXT("GIFT TO ");
		AppendCommander(Label, Flow.Teammate);
	}
	Paint.Text(Label.ToView(), G.Label.X, G.Label.Y + 3.f, 9.f, Palette::Muted, true);
	Paint.Fill(G.Amount, FieldFill);
	Paint.Outline(G.Amount, Palette::KeyEdge);
	TStringBuilder<16> Amount;
	Amount.Appendf(TEXT("%d"), Flow.Amount);
	Paint.TextIn(Amount.ToView(), G.Amount, 12.f, Palette::Gold, true, EAlign::Center);
	FString Refusal;
	float Opacity;
	if (Context.Controller->GetTeamRefusal(Refusal, Opacity))
		Paint.Text(Refusal, G.Refusal.X, G.Refusal.Y + 2.f, 9.5f, Palette::Bad.CopyWithNewOpacity(Opacity), true, EAlign::Left,
			G.Refusal.W);
}

static void DrawGiftLog(const FPainter& Paint, const FContext& Context, const FTeamGeometry& G)
{
	Paint.Text(TEXT("GIFT LOG"), G.LogLabel.X, G.LogLabel.Y + 2.f, 9.f, Palette::Muted, true);
	TeamPanelPolicy::FLogBuffer Log;
	UGiftCommandComponent::ReadLog(*Context.State, Log);
	if (Log.IsEmpty())
		Paint.TextIn(TEXT("No gifts yet"), G.LogLines[0], 9.f, Palette::Faint);
	for (int32 Row = 0; Row < LogRows; ++Row)
	{
		const int32 Index = LogEntryIndex(Log.Num(), Context.Controller->GetTeamFlow().LogScroll, Row);
		if (Index == INDEX_NONE)
			continue;
		TStringBuilder<32> Time;
		JevIntent::AppendElapsed(Time, Log[Index].Time);
		TStringBuilder<64> Text;
		AppendLogText(Text, Log[Index]);
		Paint.TextIn(Time.ToView(), { G.LogLines[Row].X, G.LogLines[Row].Y, 40.f, G.LogLines[Row].H }, 9.f, Palette::Muted);
		Paint.TextIn(Text.ToView(), { G.LogLines[Row].X + 44.f, G.LogLines[Row].Y, G.LogLines[Row].W - 44.f, G.LogLines[Row].H },
			9.5f, Palette::Text);
	}
}

void DrawTeamPanel(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	if (!Context.Controller || !Context.State || !Context.Wallet || !Context.Controller->IsTeamPanelOpen()
		|| Layout.TeamPanel.W <= 0.f)
		return;
	FMates Mates;
	UGiftCommandComponent::Teammates(*Context.State, Context.Wallet, Mates);
	const FTeamGeometry G = TeamGeometry(Layout.TeamPanel, Mates.Num());
	Paint.Fill(G.Panel, PanelFill);
	Paint.Outline(G.Panel, Cyan, 1.5f);
	Paint.Text(TEXT("TEAM"), G.Panel.X + 10.f, G.Panel.Y + 6.f, 12.f, Palette::Text, true);
	double Power = Context.State->GetPowerRate(Context.Wallet), Data = Context.State->GetDataRate(Context.Wallet);
	for (const ACommandPlayerState* Mate : Mates)
	{
		Power += Context.State->GetPowerRate(Mate);
		Data += Context.State->GetDataRate(Mate);
	}
	TStringBuilder<96> Pool;
	AppendPool(Pool, Power, Data, Mates.Num() + 1);
	Paint.Text(Pool.ToView(), G.Panel.X + 58.f, G.Panel.Y + 8.f, 9.f, Palette::Muted, false, EAlign::Left, G.Close.X - G.Panel.X - 62.f);
	if (Mates.IsEmpty())
		Paint.TextIn(TEXT("No teammates in this battle"), G.Rows[0], 10.f, Palette::Muted, false, EAlign::Left, 10.f);
	else
		DrawGiftReadout(Paint, Context, G);
	DrawGiftLog(Paint, Context, G);
}
}
