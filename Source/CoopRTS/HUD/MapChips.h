#pragma once

#include "HUDTypes.h"
#include "Rules/MapPresentationPolicy.h"

// Canvas pieces of the step 1b map surfaces: trait glyphs, the chain-break glyph and the CUT OFF chip
// (ui.md surfaces 2 and 7). Their geometry comes from Rules/MapPresentationPolicy.
namespace CommandHUDPanels
{
// Strokes in the unit square scaled into Rect.
void DrawGlyph(const FPainter& Paint, TConstArrayView<MapPresentation::FGlyphSegment> Glyph, const FRect& Rect,
	const FLinearColor& Color, float Thickness = 1.5f);
// Hazard is amber, the other traits take the plate's text colour; the silhouette carries the meaning.
FLinearColor TraitColor(ERegionTrait Trait);
// The red chip at Rect's centre column: chain-break glyph and the word.
void DrawCutOffChip(const FPainter& Paint, const FRect& Rect);
float CutOffChipWidth(const FPainter& Paint);
// A Drill Rig the supply cut left offline: the chain-break glyph on a dark tile, centred at Center.
void DrawOfflineRigGlyph(const FPainter& Paint, const FVector2D& Center);
}
