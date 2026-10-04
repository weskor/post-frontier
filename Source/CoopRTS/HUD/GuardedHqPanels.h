#pragma once

#include "HUDTypes.h"

class ACommandGameState;
class AHeadquarters;

// Surface 9 (Docs/Design/ui.md): the objective strip's HQ bars with their node pips, the immune hatch, the offline
// hold bar and its three state lines, the HQ world labels and the standing objective sentence.
// Every number is read from HqHoldPolicy and the HQ; nothing here decides a rule.
namespace CommandHUDPanels
{
// The side's name as the strip and the feed print it: Hardline or The Lattice.
const TCHAR* HqName(int32 Team);
// Appends seconds as m:ss.
void AppendClock(FStringBuilderBase& Out, int32 Seconds);
// The strip's HQ bar: health, hatched with a shield glyph while a node stands, and the hold bar while offline.
void DrawHqBar(const FPainter& Paint, const FRect& Rect, const AHeadquarters* HQ, const FLinearColor& Color);
// The row under the bar: the hold's state line while the HQ is offline, otherwise Regions text is the caller's
// and this draws `Nodes` with one pip per node at the row's right end. False when it drew nothing.
bool DrawHoldLine(const FPainter& Paint, const FRect& Row, const AHeadquarters* HQ);
void DrawNodePips(const FPainter& Paint, const FRect& Row, const AHeadquarters* HQ);
// Diagonal hatch over a bar, the immune marker.
void DrawImmuneHatch(const FPainter& Paint, const FRect& Rect);
void DrawShieldGlyph(const FPainter& Paint, const FVector2D& Center, const FLinearColor& Color);
// The world label of an HQ: `LATTICE HQ 900/900 · immune (2 nodes)`, or its hold while offline.
void AppendHqLabel(FStringBuilderBase& Out, const AHeadquarters& HQ);
// The strip's text while no objective event exists yet.
void AppendStandingSentence(FStringBuilderBase& Out, const ACommandGameState& State);
}
