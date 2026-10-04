#include "PlanningPanel.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Engine/World.h"
#include "PressurePanels.h"
#include "Rules/PlanningHudPolicy.h"

namespace CommandHUDPanels
{
namespace
{
constexpr float ChipRowHeight = 21.f;
constexpr float ChipSpacing = 6.f;
constexpr float ClockOverhang = 30.f;

// Dashes that would cross a world label are left out, so the ghost never strikes through text.
void DashedOutline(const FPainter& Paint, const FRect& Rect, const FLinearColor& Color, TConstArrayView<FRect> Avoid)
{
	constexpr float Dash = 7.f, Gap = 5.f, Thickness = 2.f;
	const auto Put = [&](const FRect& Segment) {
		for (const FRect& Label : Avoid)
			if (Segment.Intersects(Label))
				return;
		Paint.Fill(Segment, Color);
	};
	for (float X = Rect.X; X < Rect.Right(); X += Dash + Gap)
	{
		const float Width = FMath::Min(Dash, Rect.Right() - X);
		Put({ X, Rect.Y, Width, Thickness });
		Put({ X, Rect.Bottom() - Thickness, Width, Thickness });
	}
	for (float Y = Rect.Y; Y < Rect.Bottom(); Y += Dash + Gap)
	{
		const float Height = FMath::Min(Dash, Rect.Bottom() - Y);
		Put({ Rect.X, Y, Thickness, Height });
		Put({ Rect.Right() - Thickness, Y, Thickness, Height });
	}
}

// The screen box of the footprint square around Center; false when any corner is off the projection.
bool GhostBox(const FPainter& Paint, const FContext& Context, const FVector& Center, float Half, FRect& Out)
{
	FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D Max(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
	for (const FVector2D Corner : { FVector2D(-Half, -Half), FVector2D(Half, -Half), FVector2D(Half, Half), FVector2D(-Half, Half) })
	{
		FVector2D Screen;
		if (!ProjectOverlay(Paint, Context, Center + FVector(Corner.X, Corner.Y, 0.f), Screen))
			return false;
		Min = FVector2D(FMath::Min(Min.X, Screen.X), FMath::Min(Min.Y, Screen.Y));
		Max = FVector2D(FMath::Max(Max.X, Screen.X), FMath::Max(Max.Y, Screen.Y));
	}
	Out = { static_cast<float>(Min.X), static_cast<float>(Min.Y), static_cast<float>(Max.X - Min.X), static_cast<float>(Max.Y - Min.Y) };
	return true;
}

bool IsClear(const FPainter& Paint, const FContext& Context, const FLayout& Layout, TConstArrayView<FRect> Labels, const FRect& Plate)
{
	if (!OverlayFits(Paint, Plate) || !OverlayClearsPanels(Context, Layout, Plate))
		return false;
	for (const FRect& Label : Labels)
		if (Plate.Intersects(Label))
			return false;
	return true;
}

// The plate sits on the ghost's centre; when a deposit label (the Drill Rig's ghost is a deposit) or a panel is in the way
// it goes above, below or beside the box, and failing all of those it is moved clear of the labels.
FRect GhostPlate(const FPainter& Paint, const FContext& Context, const FLayout& Layout, TConstArrayView<FRect> Labels, const FRect& Box, float Width)
{
	constexpr float Height = 34.f;
	const FRect Centred{ Box.Center().X - Width * .5f, Box.Center().Y - Height * .5f, Width, Height };
	const FRect Candidates[] = { Centred, { Centred.X, Box.Y - Height - 4.f, Width, Height }, { Centred.X, Box.Bottom() + 4.f, Width, Height },
		{ Box.Right() + 4.f, Centred.Y, Width, Height }, { Box.X - Width - 4.f, Centred.Y, Width, Height } };
	for (const FRect& Candidate : Candidates)
		if (IsClear(Paint, Context, Layout, Labels, Candidate))
			return Candidate;
	return PlaceClearOf(Centred, Labels, [&](const FRect& Candidate) {
		return OverlayFits(Paint, Candidate) && OverlayClearsPanels(Context, Layout, Candidate);
	});
}

void DrawGhost(const FPainter& Paint, const FContext& Context, const FLayout& Layout, TConstArrayView<FRect> Labels,
	const FVector& Center, float Half, FStringView Title)
{
	FRect Box;
	if (!GhostBox(Paint, Context, Center, Half, Box) || !OverlayFits(Paint, Box))
		return;
	DashedOutline(Paint, Box, Palette::Warn, Labels);
	constexpr FStringView Note = TEXTVIEW("auto-placed at 0:00 if unplaced");
	const float Width = FMath::Max(Paint.TextWidth(Title, 9.5f, true), Paint.TextWidth(Note, 8.5f)) + 16.f;
	const FRect Plate = GhostPlate(Paint, Context, Layout, Labels, Box, Width);
	if (!OverlayFits(Paint, Plate) || !OverlayClearsPanels(Context, Layout, Plate))
		return;
	Paint.Fill(Plate, Palette::Panel);
	Paint.Text(Title, Plate.Center().X, Plate.Y + 4.f, 9.5f, Palette::Warn, true, EAlign::Center, Plate.W);
	Paint.Text(Note, Plate.Center().X, Plate.Y + 19.f, 8.5f, Palette::Muted, false, EAlign::Center, Plate.W);
}

void DrawChip(const FPainter& Paint, const FRect& Chip, FStringView Text, const FLinearColor& Color, bool bLocal)
{
	Paint.Fill(Chip, Color.CopyWithNewOpacity(.14f));
	Paint.Outline(Chip, bLocal ? Palette::Friendly : Color.CopyWithNewOpacity(.7f), bLocal ? 2.f : 1.f);
	Paint.TextIn(Text, Chip, 9.f, Color, true, EAlign::Center, 6.f);
}
}

void DrawPlanningClock(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	if (!Context.State || !Context.State->GetWorld())
		return;
	const double Remaining = Context.State->Planning.SecondsRemaining;
	const FRect Slot = BattleClockRect(Layout);
	const FRect Rect{ Slot.X - ClockOverhang, Slot.Y + 4.f, Slot.W + ClockOverhang, Slot.H - 8.f };
	const FLinearColor Amber = Palette::Warn.CopyWithNewOpacity(PlanningHud::PulseOpacity(Remaining, Context.State->GetWorld()->GetRealTimeSeconds()));
	Paint.Fill(Rect, Amber.CopyWithNewOpacity(.12f));
	Paint.Outline(Rect, Amber, 2.f);
	TStringBuilder<32> Text;
	PlanningHud::AppendClock(Text, Remaining);
	Paint.TextIn(Text.ToView(), Rect, 11.f, Amber, true, EAlign::Center, 4.f);
}

void DrawPlanningStrip(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	if (!Context.State)
		return;
	const FRect& Strip = Layout.Objectives;
	FRect Contributors = ObjectiveContributors(Strip);
	// Pause's READY slot and the TEAM opener sit over the strip's right end.
	Contributors.W = FMath::Max(0.f, Layout.TeamButton.X - Gap - Contributors.X);
	Paint.Text(TEXT("Place your kit, pick a type, queue a first order. Battle starts when everyone is Ready."),
		Contributors.X, Strip.Y + 6.f, 9.f, Palette::Text, false, EAlign::Left, Contributors.W);
	const FPlanningView View = ReadPlanning(Context);
	float X = Contributors.X;
	for (const FPlanningKit& Kit : Context.State->Planning.Kits)
	{
		if (!IsValid(Kit.Commander))
			continue;
		const PlanningPolicy::FKitFill Need = PlanningPolicy::Fill(IsValid(Kit.Barracks), IsValid(Kit.Rig), View.bRigSite);
		const PlanningHud::EChip State = PlanningHud::ChipState(Kit.bReady, !Need.bPlaceBarracks && !Need.bPlaceRig);
		TStringBuilder<32> Text;
		PlanningHud::AppendChip(Text, Kit.Commander->CommanderIndex, State);
		const FRect Chip{ X, Contributors.Y, Paint.TextWidth(Text.ToView(), 9.f, true) + 24.f, ChipRowHeight };
		if (Chip.Right() > Contributors.Right())
			break;
		DrawChip(Paint, Chip, Text.ToView(), State == PlanningHud::EChip::Ready ? Palette::Good : State == PlanningHud::EChip::Placed ? Palette::Text
																																	  : Palette::Warn,
			Kit.Commander == Context.Wallet);
		X = Chip.Right() + ChipSpacing;
	}
}

void DrawPlanningGhosts(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	const FPlanningView View = ReadPlanning(Context);
	if (!View.Kit)
		return;
	TArray<FRect, TInlineAllocator<16>> Labels;
	DepositLabelRects(Paint, Context, Labels);
	const FPlanningGhosts& Ghosts = Context.Controller->GetPlanningGhosts();
	for (const bool bRig : { false, true })
		if (!(bRig ? View.bRigPlaced : View.bBarracksPlaced) && (bRig ? Ghosts.bRig : Ghosts.bBarracks))
		{
			TStringBuilder<64> Title;
			Title << PlanningPieceName(Context, bRig) << TEXT(" default spot");
			DrawGhost(Paint, Context, Layout, Labels, bRig ? Ghosts.Rig : Ghosts.Barracks, bRig ? Ghosts.RigHalf : Ghosts.BarracksHalf, Title.ToView());
		}
}
}
