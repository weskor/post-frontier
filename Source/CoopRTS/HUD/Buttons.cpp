#include "HUDPanels.h"
#include "CommandGameState.h"

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
	case EHUDAction::Fortify:
		DrawFortifyDock(Paint, Context, Button, bHover);
		break;
	case EHUDAction::ActivePause: {
		Paint.Fill(Button.Rect, Button.Available() ? (bHover ? Palette::CardHover : Palette::Panel) : Palette::CardOff);
		Paint.Outline(Button.Rect, Button.bActive ? Palette::Warn : Palette::Edge);
		TStringBuilder<64> Label;
		if (Button.bActive && Context.State && Context.State->GetNetMode() != NM_Standalone)
			Label.Appendf(TEXT("PAUSED %ds  [P] Resume"), FMath::CeilToInt(Context.State->GetPauseSecondsRemaining()));
		else
			Label << (Button.bActive ? TEXT("PAUSED  [P] Resume") : Button.Available() ? TEXT("[P] Pause")
																					   : TEXT("[P] Team pause spent"));
		Paint.TextIn(Label.ToView(), Button.Rect, 10.f, Button.Available() ? Palette::Text : Palette::Muted, true, EAlign::Center);
		break;
	}
	case EHUDAction::PingTeammateForce:
		Paint.Fill(Button.Rect, !Button.Available() ? Palette::CardOff : bHover ? Palette::CardHover
																				: Palette::Card);
		Paint.Outline(Button.Rect, Palette::Friendly);
		Paint.TextIn(TEXT("Need help here [G]"), Button.Rect, 11.f,
			Button.Available() ? Palette::Text : Palette::Muted, true, EAlign::Center);
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
