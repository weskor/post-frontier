#include "HUDPanels.h"
#include "CommandPlayerController.h"
#include "CommandGameState.h"
#include "CoopSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace CommandHUDPanels
{
static const TCHAR* ScreenButtonLabel(EHUDAction Action)
{
	switch (Action)
	{
	case EHUDAction::PlaySolo:
		return TEXT("PLAY VS JEV");
	case EHUDAction::HostCoop:
		return TEXT("HOST CO-OP / STEAM");
	case EHUDAction::MapV2:
		return TEXT("AVAILABILITY ZONE V2");
	case EHUDAction::MapClassic:
		return TEXT("AVAILABILITY ZONE");
	case EHUDAction::InviteFriends:
		return TEXT("INVITE FRIENDS");
	case EHUDAction::Resume:
		return TEXT("RESUME MATCH");
	case EHUDAction::Controls:
		return TEXT("HOW TO PLAY / CONTROLS");
	case EHUDAction::Audio:
		return TEXT("AUDIO");
	case EHUDAction::Back:
		return TEXT("BACK");
	case EHUDAction::MainMenu:
		return TEXT("RETURN TO MAIN MENU");
	case EHUDAction::Quit:
		return TEXT("QUIT");
	case EHUDAction::ConfirmLeave:
		return TEXT("LEAVE MATCH");
	case EHUDAction::ConfirmQuit:
		return TEXT("QUIT GAME");
	case EHUDAction::VolumeDown:
		return TEXT("VOLUME -10%");
	case EHUDAction::VolumeUp:
		return TEXT("VOLUME +10%");
	case EHUDAction::Menu:
		return TEXT("MENU / ESC");
	case EHUDAction::Restart:
		return TEXT("PLAY AGAIN");
	default:
		return TEXT("");
	}
}

void DrawScreenButton(const FPainter& Paint, const FButton& Button, bool bHover)
{
	Paint.Fill(Button.Rect, !Button.Available() ? Palette::CardOff : bHover ? Palette::CardHover
																			: Palette::Card);
	Paint.Outline(Button.Rect, bHover || Button.bActive ? Palette::Gold : Palette::Edge);
	Paint.TextIn(ScreenButtonLabel(Button.Action), Button.Rect, 12.f,
		!Button.Available() ? Palette::Muted : bHover || Button.bActive ? Palette::Gold
																		: Palette::Text,
		true, EAlign::Center);
}
void DrawScreenLine(const FPainter& Paint, const FRect& Panel, const TCHAR* Text, int32 Index, const FLinearColor& Color)
{
	Paint.Text(Text, Panel.X + 32.f, Panel.Y + 118.f + Index * 27.f, 11.f, Color, false, EAlign::Left, Panel.W - 64.f);
}

static const TCHAR* ScreenTitle(const FContext& Context, ECommandScreen Screen)
{
	const TCHAR* Title = TEXT("POST-FRONTIER");
	if (Screen == ECommandScreen::Pause)
		Title = TEXT("MATCH MENU");
	else if (Screen == ECommandScreen::Controls)
		Title = TEXT("HOW TO PLAY");
	else if (Screen == ECommandScreen::Audio)
		Title = TEXT("AUDIO");
	else if (Screen == ECommandScreen::ConfirmLeave)
		Title = TEXT("LEAVE THIS MATCH?");
	else if (Screen == ECommandScreen::ConfirmQuit)
		Title = TEXT("QUIT THE GAME?");
	else if (Screen == ECommandScreen::Result)
		Title = Context.State->MatchResult == EMatchResult::Victory ? TEXT("VICTORY") : TEXT("DEFEAT");
	return Title;
}

static void DrawSessionStatus(const FPainter& Paint, const FContext& Context, const FRect& Panel, ECommandScreen Screen)
{

	if (Screen != ECommandScreen::Controls && Screen != ECommandScreen::Audio)
	{
		if (const UCoopSessionSubsystem* Session = Context.Controller->GetGameInstance()->GetSubsystem<UCoopSessionSubsystem>())
		{
			const FString Status = Session->GetStatus();
			DrawScreenLine(Paint, Panel, *Status, Screen == ECommandScreen::MainMenu ? 0 : 6, Palette::Friendly);
		}
	}
}

static void DrawMainMenuContent(const FPainter& Paint, const FRect& Panel)
{
	const float Center = Panel.Center().X;

	Paint.Text(TEXT("SOLO OR STEAM CO-OP  /  AVAILABILITY ZONE"), Center, Panel.Y + 82.f, 12.f, Palette::Friendly, true, EAlign::Center);
	DrawScreenLine(Paint, Panel, TEXT("Build a base. Give your forces orders. Break JEV's headquarters."), 1);
	DrawScreenLine(Paint, Panel, TEXT("Choose v2 (15 regions) or classic. Solo needs no connection."), 2, Palette::Muted);
	DrawScreenLine(Paint, Panel, TEXT("Start with 600 resources; your baseline income is +2 per second."), 3, Palette::Muted);
	DrawScreenLine(Paint, Panel, TEXT("Choose your map below, then play solo or host on Steam."), 4, Palette::Muted);
}

static void DrawControlsContent(const FPainter& Paint, const FRect& Panel)
{

	DrawScreenLine(Paint, Panel, TEXT("1  Build a Barracks on green grid preview cells (220 resources)."), 0);
	DrawScreenLine(Paint, Panel, TEXT("2  Select it when complete. Choose a force type, then Start."), 1);
	DrawScreenLine(Paint, Panel, TEXT("3  Select forces; right-click a region to Move & Hold, a hostile base to Attack."), 2);
	DrawScreenLine(Paint, Panel, TEXT("4  Controlled regions allow building. Extractors (160) earn private Power."), 3);
	DrawScreenLine(Paint, Panel, TEXT("5  Build a Workshop (190), buy one specialization (150)."), 4);
	DrawScreenLine(Paint, Panel, TEXT("6  Attack advances to your region; Retreat regroups in safe territory."), 5, Palette::Gold);
	DrawScreenLine(Paint, Panel, TEXT("Click badge/unit: force. Building: production; double-click: force."), 6, Palette::Friendly);
	DrawScreenLine(Paint, Panel, TEXT("Shift-click: toggle. Drag box: badges. 1-4 (solo 1-5): force."), 7);
	DrawScreenLine(Paint, Panel, TEXT("F or double-tap number: centre. Space: latest / older alert."), 8);
	DrawScreenLine(Paint, Panel, TEXT("Arrows/edge/middle drag: pan. Wheel: zoom. F4: deck. Esc: cancel/menu."), 9);
	DrawScreenLine(Paint, Panel, TEXT("A then region: Attack. R: Retreat. Shift: queue. Building + RMB: rally."), 10, Palette::Muted);
	DrawScreenLine(Paint, Panel, TEXT("G: ping ground / minimap; teammate force: Need help here."), 11, Palette::Friendly);
	DrawScreenLine(Paint, Panel, TEXT("Siege costs 180 once to configure; replacements cost per unit."), 12, Palette::Muted);
}

static void DrawAudioContent(const FPainter& Paint, const FContext& Context, const FRect& Panel)
{
	const float Center = Panel.Center().X;
	const float Left = Panel.X + 32.f;
	const float Width = Panel.W - 64.f;

	DrawScreenLine(Paint, Panel, TEXT("MASTER VOLUME  /  saved automatically"), 0, Palette::Muted);
	TStringBuilder<32> Value;
	Value.Appendf(TEXT("%d%%"), FMath::RoundToInt(Context.Controller->GetMasterVolume() * 100.f));
	Paint.Text(Value.ToView(), Center, Panel.Y + 168.f, 24.f, Palette::Text, true, EAlign::Center);
	Paint.Bar({ Left, Panel.Y + 220.f, Width, 12.f }, Context.Controller->GetMasterVolume(), Palette::Friendly);
	DrawScreenLine(Paint, Panel, TEXT("0% mutes gameplay and interface sounds."), 9, Palette::Muted);
}

static void DrawPauseContent(const FPainter& Paint, const FContext& Context, const FRect& Panel)
{

	DrawScreenLine(Paint, Panel, Context.Controller->GetWorld()->IsPaused() ? TEXT("Solo match paused. JEV and your economy are stopped.") : TEXT("Online match continues while this menu is open."),
		1, Palette::Friendly);
	DrawScreenLine(Paint, Panel, TEXT("Resume keeps your buildings, orders and progress."), 3, Palette::Muted);
	DrawScreenLine(Paint, Panel, TEXT("Leaving discards this match. There is no save / load yet."), 4, Palette::Warn);
}

static void DrawConfirmationContent(const FPainter& Paint, const FContext& Context, const FRect& Panel)
{

	DrawScreenLine(Paint, Panel, Context.Controller->IsMenuWorld() ? TEXT("Close Post-Frontier?") : TEXT("Current match progress will be discarded."),
		1, Palette::Warn);
	if (Context.Controller->GetNetMode() == NM_ListenServer)
		DrawScreenLine(Paint, Panel, TEXT("You are the host: leaving ends the match for everyone."), 3, Palette::Warn);
	DrawScreenLine(Paint, Panel, TEXT("Back or Esc cancels; no action is taken until you confirm."), 5, Palette::Muted);
}

void DrawScreen(const FPainter& Paint, const FContext& Context, const FLayout& Layout, EHUDAction Hover)
{
	const ECommandScreen Screen = Context.Controller->GetUIScreen();
	const FRect& Panel = Layout.Screen;
	Paint.Fill({ 0, 0, Layout.Width, Layout.Height }, FLinearColor(.006f, .012f, .020f, Screen == ECommandScreen::MainMenu ? 1.f : .92f));
	Paint.Panel(Panel);
	Paint.Fill({ Panel.X, Panel.Y, Panel.W, 3.f }, Palette::Gold);
	const float Center = Panel.Center().X;
	Paint.Text(ScreenTitle(Context, Screen), Center, Panel.Y + 32.f, 28.f, Palette::Gold, true, EAlign::Center);
	DrawSessionStatus(Paint, Context, Panel, Screen);
	switch (Screen)
	{
	case ECommandScreen::MainMenu:
		DrawMainMenuContent(Paint, Panel);
		break;
	case ECommandScreen::Controls:
		DrawControlsContent(Paint, Panel);
		break;
	case ECommandScreen::Audio:
		DrawAudioContent(Paint, Context, Panel);
		break;
	case ECommandScreen::Pause:
		DrawPauseContent(Paint, Context, Panel);
		break;
	case ECommandScreen::ConfirmLeave:
	case ECommandScreen::ConfirmQuit:
		DrawConfirmationContent(Paint, Context, Panel);
		break;
	case ECommandScreen::Result:
		DrawResult(Paint, Context, Panel);
		break;
	default:
		break;
	}
	ForEachButton(Context, Layout, [&](const FButton& Button) { DrawScreenButton(Paint, Button, Button.Action == Hover); });
}

}
