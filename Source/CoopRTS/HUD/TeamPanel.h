#pragma once

#include "HUDTypes.h"
#include "Rules/TeamPanelPolicy.h"

// The Team panel (ui.md surface 3): the TEAM opener, the 390 px panel that replaces the alert feed while open,
// its buttons and drawing. Layout, hit testing and drawing all read TeamGeometry, so a click always lands on what is drawn.
namespace CommandHUDPanels
{
constexpr float TeamButtonWidth = 96.f;

struct FTeamGeometry
{
	FRect Panel;
	FRect Header;
	FRect Close;
	// One per teammate; with no teammates the first is the note line.
	FRect Rows[TeamPanelPolicy::MaxTeammates];
	FRect Label;
	FRect Power;
	FRect Data;
	FRect Presets[TeamPanelPolicy::PresetCount];
	FRect Minus;
	FRect Amount;
	FRect Plus;
	FRect Send;
	FRect Refusal;
	FRect LogLabel;
	FRect LogLines[TeamPanelPolicy::LogRows];
	FRect LogUp;
	FRect LogDown;
	// The gift controls exist only with a teammate to send to.
	bool bGift = false;
	float Height = 0.f;
};

// Panel is the rectangle's origin and width; the height follows from the teammate count.
FTeamGeometry TeamGeometry(const FRect& Panel, int32 Teammates);
// How many teammates the panel lists for this viewer.
int32 TeammateCount(const FContext& Context);
// The opener, and while the panel is open every button inside it.
void ForEachTeamButton(const FContext& Context, const FLayout& Layout, TFunctionRef<void(const FButton&)> Visit);
bool IsTeamAction(EHUDAction Action);
// The panel frame, header, labels, amount, refusal and log. Buttons are drawn by DrawTeamButton.
void DrawTeamPanel(const FPainter& Paint, const FContext& Context, const FLayout& Layout);
void DrawTeamButton(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover);
// The recipient's top-bar flash for the newest gift to the viewer, on the battle clock the log is timed on.
TeamPanelPolicy::FFlash GiftFlash(const FContext& Context);
// The Send verdict the panel shows for the viewer's current flow.
TeamPanelPolicy::FSendInput TeamSendInput(const FContext& Context);
}
