#include "HUDPanels.h"

namespace CommandHUDPanels
{
void DrawButton(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	if (BuildSlot(Button.Action) != INDEX_NONE)
	{
		DrawBuildCard(Paint, Context, Button, bHover);
		return;
	}
	switch (Button.Action)
	{
	case EHUDAction::Construction:
		Paint.Fill(Button.Rect, bHover ? Palette::CardHover : Palette::Panel);
		Paint.Outline(Button.Rect, Palette::Friendly);
		Paint.TextIn(TEXT("CONSTRUCTION"), Button.Rect, 10.f, Palette::Text, true, EAlign::Center);
		break;
	case EHUDAction::ResearchSiege:
	case EHUDAction::ResearchRepairs:
	case EHUDAction::ResearchEntrenched:
		DrawResearchCard(Paint, Button, bHover);
		break;
	default:
		DrawCommandRow(Paint, Context, Button, bHover);
	}
}

}
