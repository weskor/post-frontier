#include "HUDPanels.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "MapRegion.h"
#include "Rules/FortifyPolicy.h"

namespace CommandHUDPanels
{
void DrawFortifyDock(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	const float Now = Context.State ? Context.State->GetServerWorldTimeSeconds() : 0.f;
	const auto Dock = FortifyPolicy::Dock(Now, Context.Wallet ? Context.Wallet->FortifyReadyAt : 0.f, Context.DataBalance);
	// Nothing is spent or cast before 0:00: the dock is muted and says when it opens.
	const FLinearColor Color = Context.bPlanning                           ? Palette::Muted
		: Button.bActive || Dock.State == FortifyPolicy::EDockState::Ready ? Palette::Good
																		   : Palette::Warn;
	const FRect& Rect = Button.Rect;
	Paint.Fill(Rect, bHover ? Palette::CardHover : Palette::Panel);
	Paint.Outline(Rect, Color);
	Paint.Fill({ Rect.X, Rect.Y, 3.f, Rect.H }, Color);
	Paint.Text(TEXT("Fortify [H]"), Rect.X + 9.f, Rect.Y + 3.f, 10.f, Palette::Text, true);
	TStringBuilder<64> Status;
	if (Context.bPlanning)
		Status << TEXT("Opens at 0:00");
	else if (Button.bActive)
		Status << TEXT("Pick a region \u00B7 Esc");
	else if (Dock.State == FortifyPolicy::EDockState::Cooldown)
	{
		Status << TEXT("Ready in ");
		FortifyPolicy::AppendClock(Status, Dock.CooldownLeft);
	}
	else if (Dock.State == FortifyPolicy::EDockState::NeedData)
		FortifyPolicy::AppendDataShort(Status, Dock.DataShort);
	else
		Status.Appendf(TEXT("%d Data"), FortifyPolicy::DataCost);
	Paint.Text(Status.ToView(), Rect.X + 9.f, Rect.Y + 20.f, 8.5f, Color, false, EAlign::Left, Rect.W - 15.f);
	if (Dock.State == FortifyPolicy::EDockState::Cooldown)
		Paint.Bar({ Rect.X + 3.f, Rect.Bottom() - 3.f, Rect.W - 3.f, 3.f }, Dock.CooldownLeft / FortifyPolicy::CooldownSeconds, Color);
}

void DrawFortifyCursor(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	float X, Y;
	if (!Context.Controller->GetMousePosition(X, Y))
		return;
	const FFortifyPreview Preview = Context.Controller->GetFortifyPreview({ X, Y });
	const FStringView Name = ObjectiveRegionName(Preview.RegionName);
	TStringBuilder<256> Title;
	const bool bRefresh = Preview.IsAllowed() && Preview.Decision.bRefresh;
	if (!Preview.IsAllowed())
	{
		Title << TEXT("Not allowed: ");
		FortifyPolicy::AppendReason(Title, Preview.Decision, Name);
	}
	else if (bRefresh)
	{
		Title << TEXT("LMB: Refresh Fortify at ") << Name << TEXT(" \u00B7 ");
		FortifyPolicy::AppendClock(Title, Preview.Decision.RefreshLeft);
		Title << TEXT(" left");
	}
	else
		Title.Appendf(TEXT("LMB: Fortify %.*s \u00B7 %d Data"), Name.Len(), Name.GetData(), FortifyPolicy::DataCost);
	TStringBuilder<128> Effects;
	Effects.Appendf(TEXT("Anchor can't be captured \u00B7 allies take \u2212%.0f%% damage \u00B7 %.0f s"),
		(1.f - FortifyPolicy::IncomingMultiplier) * 100.f, FortifyPolicy::DurationSeconds);
	const float Width = FMath::Min(Layout.Width - 2.f * Margin,
		FMath::Max(Paint.TextWidth(Title.ToView(), 10.f, true), Paint.TextWidth(Effects.ToView(), 9.f)) + 2.f * Pad);
	const float Height = Preview.IsAllowed() ? 48.f : 28.f;
	const FRect Rect{ FMath::Clamp(X / Layout.Scale + 18.f, Margin, Layout.Width - Margin - Width),
		FMath::Clamp(Y / Layout.Scale + 18.f, Layout.Objectives.Bottom() + Gap, Layout.Height - Margin - Height), Width, Height };
	const FLinearColor Color = !Preview.IsAllowed() ? Palette::Bad : bRefresh ? Palette::Warn
																			  : Palette::Good;
	Paint.Fill(Rect, Palette::Panel);
	Paint.Outline(Rect, Color);
	Paint.Text(Title.ToView(), Rect.X + Pad, Rect.Y + 5.f, 10.f, Color, true, EAlign::Left, Rect.W - 2.f * Pad);
	if (Preview.IsAllowed())
		Paint.Text(Effects.ToView(), Rect.X + Pad, Rect.Y + 26.f, 9.f, Palette::Text, false, EAlign::Left, Rect.W - 2.f * Pad);
}

static void Shield(const FPainter& Paint, FVector2D Center, const FLinearColor& Color)
{
	const FVector2D Points[] = { { -7.f, -8.f }, { 7.f, -8.f }, { 6.f, 3.f }, { 0.f, 9.f }, { -6.f, 3.f } };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Points); ++Index)
	{
		FCanvasLineItem Line((Center + Points[Index]) * Paint.Scale,
			(Center + Points[(Index + 1) % UE_ARRAY_COUNT(Points)]) * Paint.Scale);
		Line.SetColor(Color);
		Line.LineThickness = Paint.Scale * 1.5f;
		Paint.Canvas->DrawItem(Line);
	}
}

void DrawFortifyBadge(const FPainter& Paint, const AMapRegion& Region, const FVector2D& Screen)
{
	const float Now = Region.GetServerNow();
	const float Left = FortifyPolicy::SecondsLeft(Region.GetFortify(), Now);
	if (Left <= 0.f)
		return;
	TStringBuilder<64> Label;
	Label.Appendf(TEXT("FORTIFIED C%d "), Region.FortifyCaster + 1);
	FortifyPolicy::AppendClock(Label, Left);
	const float Width = Paint.TextWidth(Label.ToView(), 9.f, true) + 42.f;
	const FRect Rect{ Screen.X - Width * .5f, Screen.Y - 2.f, Width, 28.f };
	if (!OverlayFits(Paint, Rect))
		return;
	const FLinearColor Cyan(.42f, .90f, 1.f);
	const float Pulse = Left <= FortifyPolicy::WarningSeconds ? .65f + .35f * FMath::Cos(Now * 2.f * PI) : 1.f;
	Paint.Fill(Rect, Palette::Panel);
	Paint.Outline(Rect, Cyan.CopyWithNewOpacity(.6f));
	Paint.Fill({ Rect.X, Rect.Y, 3.f, Rect.H }, AArmyUnit::GetCommanderColor(Region.FortifyCaster));
	Shield(Paint, { Rect.X + 17.f, Rect.Y + 12.f }, Cyan);
	Paint.Text(Label.ToView(), Rect.X + 32.f, Rect.Y + 4.f, 9.f, Cyan, true);
	Paint.Bar({ Rect.X + 3.f, Rect.Bottom() - 3.f, Rect.W - 3.f, 3.f }, Left / FortifyPolicy::DurationSeconds, Cyan.CopyWithNewOpacity(Pulse));
}
}
