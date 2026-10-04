#include "HUDPanels.h"
#include "CommandGameState.h"

namespace CommandHUDPanels
{
void DrawResult(const FPainter& Paint, const FContext& Context, const FRect& Panel)
{

	const bool bVictory = Context.State->MatchResult == EMatchResult::Victory;
	DrawScreenLine(Paint, Panel, bVictory ? TEXT("You held the Lattice's main: its headquarters is lost.") : TEXT("JEV held your main: your headquarters is lost."), 1, bVictory ? Palette::Good : Palette::Bad);
	DrawScreenLine(Paint, Panel, TEXT("The match is finished. Gameplay commands are locked."), 3, Palette::Muted);
	DrawScreenLine(Paint, Panel, TEXT("Play Again starts a fresh match on this map."), 4, Palette::Friendly);
}

}
