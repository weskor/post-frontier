#include "HUDPanels.h"
#include "PressurePanels.h"
#include "CapturePoint.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "DepositSite.h"
#include "MapChips.h"
#include "MapPresentation.h"
#include "MapRegion.h"
#include "CommandPlayerController.h"

namespace CommandHUDPanels
{
namespace
{
FRect AtAnchor(const MapPresentation::FRectF& Rect, const FVector2D& Anchor)
{
	return { static_cast<float>(Anchor.X) + Rect.X, static_cast<float>(Anchor.Y) + Rect.Y, Rect.W, Rect.H };
}

MapPresentation::FPlateInput PlateInput(const FPainter& Paint, FStringView Name, ERegionTrait Trait)
{
	const bool bTrait = Trait != ERegionTrait::None;
	const FStringView Word = MapPresentation::TraitWord(Trait);
	return { Paint.TextWidth(Name, 10.f, true), Paint.LineHeight(10.f, true),
		bTrait ? Paint.TextWidth(Word, MapPresentation::TraitWordSize, true) : 0.f,
		bTrait ? Paint.LineHeight(MapPresentation::TraitWordSize, true) : 0.f, bTrait };
}

// The glyph left of the name and the trait word under it, so nothing is hover-only.
void DrawPlateTrait(const FPainter& Paint, const MapPresentation::FPlate& Layout, const FVector2D& Screen, ERegionTrait Trait)
{
	const FLinearColor Color = TraitColor(Trait);
	DrawGlyph(Paint, MapPresentation::TraitGlyph(Trait), AtAnchor(Layout.Glyph, Screen), Color);
	Paint.TextIn(MapPresentation::TraitWord(Trait), AtAnchor(Layout.Word, Screen), MapPresentation::TraitWordSize,
		Color.CopyWithNewOpacity(.85f), true, EAlign::Left);
}

void DrawCaptureSites(const FPainter& Paint, const FContext& Context)
{
	for (const ACapturePoint* Site : Context.State->CaptureSites)
	{
		if (!IsValid(Site))
			continue;
		FVector2D Screen;
		if (!ProjectOverlay(Paint, Context, Site->GetActorLocation() + FVector(0.f, 0.f, 110.f), Screen))
			continue;
		// The region's display name, as the JEV timeline and memos print it; the anchor number only where no region contains it.
		const AMapRegion* Region = Context.State->FindRegionAt(Site->GetActorLocation());
		TStringBuilder<64> Label;
		if (Region)
			Label << Region->DisplayName.ToString();
		else
			Label.Appendf(TEXT("REGION %d"), Site->SiteIndex + 1);
		const ERegionTrait Trait = Region ? Region->GetTrait() : ERegionTrait::None;
		const MapPresentation::FPlate Layout = MapPresentation::LayoutPlate(PlateInput(Paint, Label.ToView(), Trait));
		const FRect Back = AtAnchor(Layout.Plate, Screen);
		if (!OverlayFits(Paint, Back))
			continue;
		const FLinearColor OwnerColor = Site->ControllingTeam == 0 ? Palette::Good
			: Site->ControllingTeam == 5                           ? Palette::Bad
																   : Palette::Gold;
		const FLinearColor CaptureColor = Site->CaptureProgress > 0.f ? Palette::Good
			: Site->CaptureProgress < 0.f                             ? Palette::Bad
																	  : OwnerColor;
		Paint.Fill(Back, FLinearColor(.005f, .008f, .012f, .95f));
		if (Trait == ERegionTrait::None)
			Paint.TextIn(Label.ToView(), AtAnchor(Layout.Name, Screen), 10.f, OwnerColor, true, EAlign::Center);
		else
		{
			DrawPlateTrait(Paint, Layout, Screen, Trait);
			Paint.TextIn(Label.ToView(), AtAnchor(Layout.Name, Screen), 10.f, OwnerColor, true, EAlign::Left);
		}
		Paint.Bar(AtAnchor(Layout.Bar, Screen), FMath::Abs(Site->CaptureProgress), CaptureColor);
	}
}

void DrawDeposits(const FPainter& Paint, const FContext& Context)
{
	for (const ADepositSite* Deposit : Context.State->Deposits)
	{
		FRect Back;
		if (!IsValid(Deposit) || !DepositLabelRect(Paint, Context, *Deposit, Back))
			continue;
		const bool bTaken = IsValid(Deposit->Extractor);
		const bool bEmpty = Deposit->Remaining <= 0;
		const bool bOffline = MapView::IsRigOffline(*Context.State, *Deposit);
		const FLinearColor Color = bOffline ? Palette::Bad : bEmpty ? Palette::Muted
			: bTaken                                                ? Palette::Gold
																	: Palette::Good;
		TStringBuilder<80> Label;
		if (bOffline)
			MapPresentation::AppendOfflineDepositLabel(Label, Deposit->bRich, Deposit->Remaining);
		else
			Label.Appendf(TEXT("%s  %d  +%d/s  %s"), Deposit->bRich ? TEXT("RICH") : TEXT("POWER"),
				Deposit->Remaining, bEmpty ? 0 : Deposit->RatePerSecond(),
				bEmpty ? TEXT("EMPTY") : bTaken ? TEXT("TAKEN")
												: TEXT("FREE"));
		Paint.Fill(Back, FLinearColor(.005f, .008f, .012f, .95f));
		Paint.TextIn(Label.ToView(), Back, 8.f, Color, true, EAlign::Center);
		// Beside the label, clear of the structure's own name and health bar above the rig.
		if (bOffline)
			DrawOfflineRigGlyph(Paint, FVector2D(Back.X - OfflineGlyphOffset, Back.Y + Back.H * .5f));
	}
}

// The chip stack under a region's plate: CUT OFF, then the Fortify badge.
void DrawRegionChips(const FPainter& Paint, const FContext& Context, const AMapRegion& Region)
{
	const ACommandGameState& State = *Context.State;
	const int32 Controller = State.GetRegionController(Region.RegionIndex);
	const bool bCutOff = MapView::IsCutOff(State, Controller, Region.RegionIndex);
	const bool bFortified = Region.IsFortifyActive();
	if (!bCutOff && !bFortified)
		return;
	FVector2D Screen;
	if (!ProjectOverlay(Paint, Context, State.GetRegionAnchor(Region.RegionIndex) + FVector(0.f, 0.f, 110.f), Screen))
		return;
	const MapPresentation::FPlate Layout = MapPresentation::LayoutPlate(PlateInput(Paint, FStringView(), Region.GetTrait()));
	if (bCutOff)
	{
		const float Width = CutOffChipWidth(Paint);
		const FRect Chip{ static_cast<float>(Screen.X) - Width * .5f,
			static_cast<float>(Screen.Y) + MapPresentation::ChipTop(Layout.ChipTop, true, bFortified, MapPresentation::EChip::CutOff),
			Width, MapPresentation::CutOffChipHeight };
		if (OverlayFits(Paint, Chip))
			DrawCutOffChip(Paint, Chip);
	}
	if (bFortified)
	{
		// The badge hangs 2 px below the point it is given; the stack places its top.
		const float Top = MapPresentation::ChipTop(Layout.ChipTop, bCutOff, true, MapPresentation::EChip::Fortified);
		DrawFortifyBadge(Paint, Region, FVector2D(Screen.X, Screen.Y + Top + 2.f));
	}
}
}

void DrawSectorOverlays(const FPainter& Paint, const FContext& Context)
{
	if (!Context.State)
		return;
	DrawDeposits(Paint, Context);
	DrawCaptureSites(Paint, Context);
	for (const AMapRegion* Region : Context.State->Regions)
		if (IsValid(Region))
			DrawRegionChips(Paint, Context, *Region);
}

}
