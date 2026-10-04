#include "CommandGameState.h"
#include "MapPresentation.h"
#include "MapRegion.h"
#include "MinimapMap.h"

namespace
{
const FLinearColor CutRed(1.f, .30f, .24f);
const FLinearColor TraitPale(.85f, .91f, .96f);
const FLinearColor TraitAmber(1.f, .70f, .28f);
constexpr float HatchSpacingPx = 4.5f;
constexpr double DashPx = 4., DashGapPx = 3.;

void DrawCutRegion(const AMapRegion& Region, const CommandMinimap::FMap& Map)
{
	TArray<FVector2D, TInlineAllocator<16>> Outline;
	for (const FVector2D& Point : Region.Polygon)
		Outline.Add(Map.Project(FVector(Point.X, Point.Y, 0.)));
	MapPresentation::ForEachHatch(Outline, HatchSpacingPx, [&](const FVector2D& A, const FVector2D& B) {
		Map.Line(A, B, CutRed.CopyWithNewOpacity(.55f), 1.f);
	});
	for (int32 Index = 0; Index < Outline.Num(); ++Index)
	{
		const FVector2D& A = Outline[Index];
		const FVector2D& B = Outline[(Index + 1) % Outline.Num()];
		const double Length = FVector2D::Distance(A, B);
		const FVector2D Direction = (B - A).GetSafeNormal();
		for (double At = 0.; At < Length; At += DashPx + DashGapPx)
			Map.Line(A + Direction * At, A + Direction * FMath::Min(At + DashPx, Length), CutRed, 1.5f);
	}
}

void DrawTrait(const AMapRegion& Region, const FVector2D& Node, const CommandMinimap::FMap& Map)
{
	const FVector2D Origin = Node + MapPresentation::MinimapGlyphOrigin();
	const FLinearColor Color = Region.GetTrait() == ERegionTrait::Hazard ? TraitAmber : TraitPale;
	for (const MapPresentation::FGlyphSegment& Segment : MapPresentation::TraitGlyph(Region.GetTrait()))
		Map.Line(Origin + Segment.A * MapPresentation::MinimapGlyphSize, Origin + Segment.B * MapPresentation::MinimapGlyphSize, Color, 1.f);
}
}

void MapView::DrawMinimapMarks(const ACommandGameState& State, const CommandMinimap::FMap& Map)
{
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region))
			continue;
		const int32 Controller = State.GetRegionController(Region->RegionIndex);
		if (IsCutOff(State, Controller, Region->RegionIndex))
			DrawCutRegion(*Region, Map);
		if (Region->GetTrait() != ERegionTrait::None)
			DrawTrait(*Region, Map.Project(State.GetRegionAnchor(Region->RegionIndex)), Map);
	}
}
