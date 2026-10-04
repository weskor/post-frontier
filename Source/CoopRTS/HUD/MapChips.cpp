#include "MapChips.h"

namespace CommandHUDPanels
{
namespace
{
const FLinearColor ChipBack(.16f, .02f, .02f, .96f);
constexpr float ChipGlyphSize = 12.f;
constexpr float ChipTextSize = 9.f;
constexpr float RigGlyphSize = 18.f;
}

void DrawGlyph(const FPainter& Paint, TConstArrayView<MapPresentation::FGlyphSegment> Glyph, const FRect& Rect,
	const FLinearColor& Color, float Thickness)
{
	const auto Place = [&](const FVector2D& Point) {
		return FVector2D(Rect.X + Point.X * Rect.W, Rect.Y + Point.Y * Rect.H) * Paint.Scale;
	};
	for (const MapPresentation::FGlyphSegment& Segment : Glyph)
	{
		FCanvasLineItem Line(Place(Segment.A), Place(Segment.B));
		Line.SetColor(Color);
		Line.LineThickness = Paint.Scale * Thickness;
		Paint.Canvas->DrawItem(Line);
	}
}

FLinearColor TraitColor(ERegionTrait Trait)
{
	return Trait == ERegionTrait::Hazard ? Palette::Warn : Palette::Text;
}

float CutOffChipWidth(const FPainter& Paint)
{
	return Paint.TextWidth(TEXT("CUT OFF"), ChipTextSize, true) + ChipGlyphSize + 18.f;
}

void DrawCutOffChip(const FPainter& Paint, const FRect& Rect)
{
	Paint.Fill(Rect, ChipBack);
	Paint.Outline(Rect, Palette::Bad);
	DrawGlyph(Paint, MapPresentation::ChainBreakGlyph(),
		{ Rect.X + 6.f, Rect.Center().Y - ChipGlyphSize * .5f, ChipGlyphSize, ChipGlyphSize }, Palette::Bad, 1.2f);
	Paint.TextIn(TEXT("CUT OFF"), { Rect.X + 8.f + ChipGlyphSize, Rect.Y, Rect.W - 8.f - ChipGlyphSize, Rect.H }, ChipTextSize,
		Palette::Bad, true, EAlign::Left, 4.f);
}

void DrawOfflineRigGlyph(const FPainter& Paint, const FVector2D& Center)
{
	const FRect Tile{ static_cast<float>(Center.X) - RigGlyphSize * .5f - 3.f, static_cast<float>(Center.Y) - RigGlyphSize * .5f - 3.f,
		RigGlyphSize + 6.f, RigGlyphSize + 6.f };
	Paint.Fill(Tile, ChipBack);
	Paint.Outline(Tile, Palette::Bad);
	DrawGlyph(Paint, MapPresentation::ChainBreakGlyph(), { Tile.X + 3.f, Tile.Y + 3.f, RigGlyphSize, RigGlyphSize }, Palette::Bad, 1.5f);
}
}
