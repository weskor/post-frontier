#include "HUDPanels.h"
#include "CommandGameState.h"

namespace CommandHUDPanels
{
void DrawResult(const FPainter& Paint, const FContext& Context, const FRect& Panel)
{

	const bool bVictory = Context.State->MatchResult == EMatchResult::Victory;
	DrawScreenLine(Paint, Panel, bVictory ? TEXT("JEV's headquarters is destroyed.") : TEXT("Your headquarters is destroyed."), 1, bVictory ? Palette::Good : Palette::Bad);
	DrawScreenLine(Paint, Panel, TEXT("The match is finished. Gameplay commands are locked."), 3, Palette::Muted);
	DrawScreenLine(Paint, Panel, TEXT("Play Again starts a fresh match on this map."), 4, Palette::Friendly);
}

}
