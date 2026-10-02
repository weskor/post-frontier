#include "HUDPanels.h"
#include "CommandGameState.h"

namespace CommandHUDPanels
{
void DrawResult(const FPainter& Paint, const FContext& Context, const FRect& Panel)
{
	auto Line = [&Paint, &Panel](const TCHAR* Text, int32 Index, const FLinearColor& Color = Palette::Text) {
		DrawScreenLine(Paint, Panel, Text, Index, Color);
	};
	const bool bVictory = Context.State->MatchResult == EMatchResult::Victory;
	Line(bVictory ? TEXT("JEV's headquarters is destroyed.") : TEXT("Your headquarters is destroyed."), 1, bVictory ? Palette::Good : Palette::Bad);
	Line(TEXT("The match is finished. Gameplay commands are locked."), 3, Palette::Muted);
	Line(TEXT("Play Again starts a fresh match on this map."), 4, Palette::Friendly);
}

}
