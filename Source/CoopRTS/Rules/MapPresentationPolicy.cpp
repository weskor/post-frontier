#include "MapPresentationPolicy.h"

namespace
{
using namespace MapPresentation;

constexpr float MinimumPlateWidth = 96.f;
constexpr float PlatePad = 8.f;
constexpr float GlyphGap = 6.f;
constexpr float PlateTopPad = 4.f;
constexpr float BarHeight = 4.f;
constexpr float BarGap = 3.f;
constexpr float ChipsBelowPlate = 4.f;
constexpr int32 MaxHatchLines = 512;

FGlyphSegment Segment(float AX, float AY, float BX, float BY)
{
	return { FVector2D(AX, AY), FVector2D(BX, BY) };
}

TArray<FGlyphSegment> HighGround()
{
	// Two stacked chevrons.
	return { Segment(.08f, .52f, .5f, .1f), Segment(.5f, .1f, .92f, .52f), Segment(.08f, .9f, .5f, .48f),
		Segment(.5f, .48f, .92f, .9f) };
}

TArray<FGlyphSegment> Cover()
{
	// A brick wall: outline, one course line and staggered joints.
	return { Segment(.08f, .18f, .92f, .18f), Segment(.92f, .18f, .92f, .82f), Segment(.92f, .82f, .08f, .82f),
		Segment(.08f, .82f, .08f, .18f), Segment(.08f, .5f, .92f, .5f), Segment(.5f, .18f, .5f, .5f),
		Segment(.3f, .5f, .3f, .82f), Segment(.7f, .5f, .7f, .82f) };
}

TArray<FGlyphSegment> Open()
{
	// Two opposed arrows.
	return { Segment(.08f, .32f, .92f, .32f), Segment(.7f, .1f, .92f, .32f), Segment(.92f, .32f, .7f, .54f),
		Segment(.92f, .7f, .08f, .7f), Segment(.3f, .48f, .08f, .7f), Segment(.08f, .7f, .3f, .92f) };
}

TArray<FGlyphSegment> Hazard()
{
	// A warning triangle with its exclamation mark.
	return { Segment(.5f, .08f, .95f, .9f), Segment(.95f, .9f, .05f, .9f), Segment(.05f, .9f, .5f, .08f),
		Segment(.5f, .38f, .5f, .62f), Segment(.5f, .73f, .5f, .78f) };
}

TArray<FGlyphSegment> Chain()
{
	// Two links either side of a slash.
	return { Segment(.04f, .34f, .38f, .34f), Segment(.38f, .34f, .38f, .66f), Segment(.38f, .66f, .04f, .66f),
		Segment(.04f, .66f, .04f, .34f), Segment(.62f, .34f, .96f, .34f), Segment(.96f, .34f, .96f, .66f),
		Segment(.96f, .66f, .62f, .66f), Segment(.62f, .66f, .62f, .34f), Segment(.5f, .1f, .5f, .9f) };
}
}

float MapPresentation::ChangeAge(float Now, float ChangedAt)
{
	if (ChangedAt <= 0.f)
		return -1.f;
	const float Age = Now - ChangedAt;
	return Age < -ClockSlackSeconds ? -1.f : FMath::Max(0.f, Age);
}

bool MapPresentation::FlashPlays(float Now, float ChangedAt)
{
	const float Age = ChangeAge(Now, ChangedAt);
	return Age >= 0.f && Age < FlashWindowSeconds;
}

bool MapPresentation::FlashLit(float Age)
{
	if (Age < 0.f || Age >= FlashSeconds)
		return false;
	const float Cycle = FlashSeconds / FlashCount;
	return FMath::Fmod(Age, Cycle) < Cycle * .5f;
}

float MapPresentation::SparkAlpha(float Age)
{
	return Age < 0.f || Age >= SparkSeconds ? 0.f : 1.f - Age / SparkSeconds;
}

bool MapPresentation::IsCutOff(const FRegionLink& Region)
{
	return Region.bControlled && !Region.bConnected;
}

MapPresentation::ECable MapPresentation::ClassifyCable(const FRegionLink& A, const FRegionLink& B)
{
	if (A.bConnected && B.bConnected)
		return ECable::Live;
	return IsCutOff(A) && IsCutOff(B) ? ECable::Beyond : ECable::None;
}

uint64 MapPresentation::NewlyCutOff(uint64 PreviousConnected, uint64 Connected, uint64 Controlled)
{
	return Connected ? PreviousConnected & ~Connected & Controlled : 0;
}

float MapPresentation::FCutObserver::FlashAge(int32 Region, float Now) const
{
	if (Region < 0 || Region >= ObservedRegions || Started[Region] < 0.f)
		return -1.f;
	return FMath::Max(0.f, Now - Started[Region]);
}

bool MapPresentation::FCutObserver::IsSnapped(int32 CutRegion, int32 OtherRegion) const
{
	if (CutRegion < 0 || CutRegion >= ObservedRegions || OtherRegion < 0 || OtherRegion >= ObservedRegions)
		return false;
	const uint64 Other = uint64(1) << OtherRegion;
	return (LastCutOff & (uint64(1) << CutRegion)) && (CutFrom[CutRegion] & Other) && !(LastHeld & Other);
}

void MapPresentation::Observe(FCutObserver& Observer, const FObservation& In)
{
	const bool bFirst = !Observer.bSeen;
	// Two changes in one server tick share a stamp, so the mask itself is compared too.
	const bool bChanged = !bFirst && (In.ChangedAt != Observer.SeenChangedAt || In.Connected != Observer.PreviousConnected);
	// First sight (a late join) takes every region that is cut now; later, only those that just left the mask.
	uint64 Newly = 0;
	if (In.Connected)
		Newly = bFirst ? In.CutOff : bChanged ? NewlyCutOff(Observer.PreviousConnected, In.Connected, In.Held) & In.CutOff
											  : 0;
	const bool bPlays = FlashPlays(In.Now, In.ChangedAt);
	for (int32 Region = 0; Region < ObservedRegions; ++Region)
	{
		const uint64 Bit = uint64(1) << Region;
		if (!(In.CutOff & Bit))
			Observer.Started[Region] = -1.f;
		else if ((Newly & Bit) && bPlays)
			Observer.Started[Region] = In.Now;
		if (!(Newly & Bit) || !In.Neighbours)
			continue;
		Observer.CutFrom[Region] = (bFirst ? In.Opponent : Observer.PreviousConnected) & In.Neighbours[Region];
	}
	Observer.LastHeld = In.Held;
	Observer.LastCutOff = In.CutOff;
	Observer.bSeen = true;
	Observer.PreviousConnected = In.Connected;
	Observer.SeenChangedAt = In.ChangedAt;
}

float MapPresentation::PulseAge(float Now, float CastTime)
{
	if (CastTime <= 0.f)
		return -1.f;
	const float Age = Now - CastTime;
	if (Age < -ClockSlackSeconds || Age >= PulseRingSeconds + PulseSparkSeconds)
		return -1.f;
	return FMath::Max(0.f, Age);
}

MapPresentation::FPulseRing MapPresentation::PulseRing(float Age, float FullRadius)
{
	if (Age < 0.f || FullRadius <= 0.f || Age >= PulseRingSeconds + PulseFadeSeconds)
		return {};
	const float Alpha = Age < PulseRingSeconds ? 1.f : 1.f - (Age - PulseRingSeconds) / PulseFadeSeconds;
	return { true, FullRadius * FMath::Min(1.f, Age / PulseRingSeconds), Alpha };
}

float MapPresentation::PulseSparkAlpha(float Age, float Distance, float FullRadius)
{
	if (Age < 0.f || FullRadius <= 0.f || Distance < 0.f || Distance > FullRadius)
		return 0.f;
	const float SparkAge = Age - PulseRingSeconds * Distance / FullRadius;
	return SparkAge < 0.f || SparkAge >= PulseSparkSeconds ? 0.f : 1.f - SparkAge / PulseSparkSeconds;
}

const TCHAR* MapPresentation::TraitWord(ERegionTrait Trait)
{
	switch (Trait)
	{
	case ERegionTrait::HighGround:
		return TEXT("HIGH GROUND");
	case ERegionTrait::Cover:
		return TEXT("COVER");
	case ERegionTrait::Open:
		return TEXT("OPEN");
	case ERegionTrait::Hazard:
		return TEXT("HAZARD");
	default:
		return TEXT("");
	}
}

TConstArrayView<MapPresentation::FGlyphSegment> MapPresentation::TraitGlyph(ERegionTrait Trait)
{
	static const TArray<FGlyphSegment> High = HighGround(), Wall = Cover(), Arrows = Open(), Warning = Hazard();
	switch (Trait)
	{
	case ERegionTrait::HighGround:
		return High;
	case ERegionTrait::Cover:
		return Wall;
	case ERegionTrait::Open:
		return Arrows;
	case ERegionTrait::Hazard:
		return Warning;
	default:
		return {};
	}
}

TConstArrayView<MapPresentation::FGlyphSegment> MapPresentation::ChainBreakGlyph()
{
	static const TArray<FGlyphSegment> Links = Chain();
	return Links;
}

FVector2D MapPresentation::MinimapGlyphOrigin()
{
	// The glyph's bottom-right corner sits just outside the ring's diagonal reach.
	const float Corner = MinimapNodeClearRadius * UE_INV_SQRT_2 + 1.f;
	return FVector2D(-Corner - MinimapGlyphSize, -Corner - MinimapGlyphSize);
}

float MapPresentation::PlateTopAboveAnchor(float NameLine)
{
	return NameLine + 18.f;
}

MapPresentation::FPlate MapPresentation::LayoutPlate(const FPlateInput& In)
{
	FPlate Out;
	const float Top = -PlateTopAboveAnchor(In.NameLine);
	if (!In.bTrait)
	{
		const float Width = FMath::Max(MinimumPlateWidth, In.NameWidth + 16.f);
		Out.Plate = { -Width * .5f, Top, Width, In.NameLine + 12.f };
		Out.Name = { Out.Plate.X, Top + 2.f, Width, In.NameLine };
		Out.Bar = { Out.Plate.X + 4.f, Top + In.NameLine + 4.f, Width - 8.f, BarHeight };
	}
	else
	{
		const float TextWidth = FMath::Max(In.NameWidth, In.WordWidth);
		const float Width = FMath::Max(MinimumPlateWidth, 2.f * PlatePad + TraitGlyphSize + GlyphGap + TextWidth);
		const float TextHeight = In.NameLine + In.WordLine;
		const float Content = FMath::Max(TraitGlyphSize, TextHeight);
		Out.Plate = { -Width * .5f, Top, Width, PlateTopPad + Content + BarGap + BarHeight + PlateTopPad };
		Out.Glyph = { Out.Plate.X + PlatePad, Top + PlateTopPad + (Content - TraitGlyphSize) * .5f, TraitGlyphSize, TraitGlyphSize };
		const float TextTop = Top + PlateTopPad + (Content - TextHeight) * .5f;
		Out.Name = { Out.Glyph.Right() + GlyphGap, TextTop, TextWidth, In.NameLine };
		Out.Word = { Out.Name.X, Out.Name.Bottom(), TextWidth, In.WordLine };
		Out.Bar = { Out.Plate.X + 4.f, Top + PlateTopPad + Content + BarGap, Width - 8.f, BarHeight };
	}
	Out.ChipTop = Out.Plate.Bottom() + ChipsBelowPlate;
	return Out;
}

float MapPresentation::ChipHeight(EChip Chip)
{
	return Chip == EChip::CutOff ? CutOffChipHeight : FortifiedChipHeight;
}

float MapPresentation::ChipTop(float FirstTop, bool bCutOff, bool bFortified, EChip Chip)
{
	if (Chip == EChip::CutOff)
		return bCutOff ? FirstTop : -1.f;
	if (!bFortified)
		return -1.f;
	return bCutOff ? FirstTop + CutOffChipHeight + ChipGap : FirstTop;
}

void MapPresentation::ForEachHatch(TConstArrayView<FVector2D> Polygon, float Spacing,
	TFunctionRef<void(const FVector2D&, const FVector2D&)> Visit)
{
	if (Polygon.Num() < 3 || Spacing <= 0.f)
		return;
	double Low = TNumericLimits<double>::Max(), High = TNumericLimits<double>::Lowest();
	for (const FVector2D& Point : Polygon)
	{
		Low = FMath::Min(Low, Point.X + Point.Y);
		High = FMath::Max(High, Point.X + Point.Y);
	}
	const int32 First = FMath::CeilToInt(Low / Spacing), Last = FMath::FloorToInt(High / Spacing);
	if (Last - First > MaxHatchLines)
		return;
	for (int32 Line = First; Line <= Last; ++Line)
	{
		const double Sum = static_cast<double>(Line) * Spacing;
		TArray<double, TInlineAllocator<16>> Crossings;
		for (int32 Index = 0, Previous = Polygon.Num() - 1; Index < Polygon.Num(); Previous = Index++)
		{
			const FVector2D& P = Polygon[Previous];
			const FVector2D& Q = Polygon[Index];
			const double FP = P.X + P.Y - Sum, FQ = Q.X + Q.Y - Sum;
			if ((FP < 0.) != (FQ < 0.))
				Crossings.Add(P.X + (Q.X - P.X) * (FP / (FP - FQ)));
		}
		Crossings.Sort();
		for (int32 Pair = 0; Pair + 1 < Crossings.Num(); Pair += 2)
			if (Crossings[Pair + 1] - Crossings[Pair] > UE_KINDA_SMALL_NUMBER)
				Visit(FVector2D(Crossings[Pair], Sum - Crossings[Pair]), FVector2D(Crossings[Pair + 1], Sum - Crossings[Pair + 1]));
	}
}

void MapPresentation::AppendCutFeedText(FStringBuilderBase& Out, FStringView Region, int32 RigsOffline)
{
	Out << TEXT("Supply cut: ") << Region << TEXT(" cut off");
	if (RigsOffline > 0)
		Out.Appendf(TEXT(" \u00B7 %d Drill Rig%s offline"), RigsOffline, RigsOffline == 1 ? TEXT("") : TEXT("s"));
}

void MapPresentation::AppendOfflineDepositLabel(FStringBuilderBase& Out, bool bRich, int32 Remaining)
{
	Out.Appendf(TEXT("%s %d \u00B7 OFFLINE"), bRich ? TEXT("RICH") : TEXT("POWER"), Remaining);
}
