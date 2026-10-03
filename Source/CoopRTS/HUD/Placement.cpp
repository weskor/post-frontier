#include "HUDPanels.h"
#include "ArmyGroup.h"
#include "CommandPlayerController.h"

namespace CommandHUDPanels
{
struct FModeGeometry
{
	float X;
	float Row1;
	float Row2;
	float KeysRight;
	float KeysWidth;
	float TextWidth;
};

static void DrawPlacementMode(const FPainter& Paint, const FContext& Context, const FRect& Mode, const FModeGeometry& Geometry)
{
	const ACommandPlayerController* Controller = Context.Controller;
	const auto& [X, Row1, Row2, KeysRight, KeysWidth, TextWidth] = Geometry;
	const UBuildingDefinition* Placement = Controller->GetPlacementDefinition();
	Paint.Fill({ Mode.X, Mode.Y, 4.f, Mode.H }, Placement ? Placement->Accent : Palette::Muted);
	TStringBuilder<128> Title;
	Title << TEXT("PLACE ") << (Placement ? Placement->DisplayName.ToString().ToUpper() : TEXT("BUILDING"));
	const float TitleWidth = Paint.Text(Title.ToView(), X, Row1, 12.5f, Palette::Text, true, EAlign::Left, TextWidth);
	TStringBuilder<32> Cost;
	const int32 Price = Placement ? Placement->BuildCost : 0;
	Cost.Appendf(TEXT("%d  \u00B7  %.0fs build"), Price, Placement ? Placement->BuildDuration : 0.f);
	Paint.TextOnBaseline(Cost.ToView(), X + TitleWidth + 12.f, Row1 + Paint.Ascent(12.5f, true), 10.f,
		Context.Balance >= Price ? Palette::Gold : Palette::Warn, true);
	FVector Location;
	FString Reason;
	bool bCanPlace = false;
	const bool bGround = Controller->GetPlacementPreview(Location, Reason, bCanPlace);
	const FLinearColor Color = bCanPlace ? Palette::Good : Palette::Warn;
	Paint.Fill({ X, Row2 + 4.f, 9.f, 9.f }, Color);
	Paint.Text(bGround ? FStringView(Reason) : FStringView(TEXT("Point at ground to place.")), X + 16.f, Row2, 10.5f, Color,
		false, EAlign::Left, TextWidth - 16.f);
	const float PlaceWidth = Paint.DrawKey(KeysRight - KeysWidth, Row1, TEXT("LMB"), TEXT("Place"));
	Paint.DrawKey(KeysRight - KeysWidth + PlaceWidth + 12.f, Row1, TEXT("RMB / Esc"), TEXT("Cancel"));
	Paint.DrawKey(KeysRight - KeysWidth, Row2, TEXT("Shift+LMB"), TEXT("places another"));
}

static void DrawOrderMode(const FPainter& Paint, const FContext& Context, const FRect& Mode, const FModeGeometry& Geometry)
{
	const ACommandPlayerController* Controller = Context.Controller;
	const auto& [X, Row1, Row2, KeysRight, KeysWidth, TextWidth] = Geometry;
	const EForceVerb Verb = Controller->GetPendingVerb();
	Paint.Fill({ Mode.X, Mode.Y, 4.f, Mode.H }, OrderColor(Verb));
	TStringBuilder<32> Title;
	Title.Appendf(TEXT("ISSUE %s"), OrderTitle(Verb));
	Paint.Text(Title.ToView(), X, Row1, 12.5f, Palette::Text, true);
	Paint.Text(TEXT("Pick a region on ground or minimap for this barracks' force."), X, Row2, 10.f,
		Palette::Muted, false, EAlign::Left, TextWidth);
	Paint.DrawKey(KeysRight - KeysWidth, Row1, TEXT("LMB"), TEXT("Assign"));
	Paint.DrawKey(KeysRight - KeysWidth, Row2, TEXT("RMB / Esc"), TEXT("Cancel"));
}

static void DrawHiddenMode(const FPainter& Paint, const FContext& Context, const FRect& Mode, const FModeGeometry& Geometry)
{
	const auto& [X, Row1, Row2, KeysRight, KeysWidth, TextWidth] = Geometry;
	Paint.Fill({ Mode.X, Mode.Y, 4.f, Mode.H }, Palette::Edge);
	Paint.Text(TEXT("COMMAND DECK HIDDEN"), X, Row1, 11.5f, Palette::Muted, true);
	TStringBuilder<64> Selection;
	if (Context.Building)
	{
		Selection.Appendf(TEXT("Selected: your %s"), Context.Building->GetDefinition() ? *Context.Building->GetDefinition()->DisplayName.ToString().ToUpper() : TEXT("BUILDING"));
		if (!Context.Building->IsComplete())
			Selection.Appendf(TEXT("  \u00B7  %d%% built"), FMath::FloorToInt(FMath::Clamp(Context.Building->ConstructionProgress, 0.f, 1.f) * 100.f));
		else if (Context.Building->IsProducer())
			Selection << TEXT("  \u00B7  ") << StatusText(Context.Building->GetProductionState());
	}
	else if (Context.Force)
	{
		const bool bOwned = Context.Force->GetOwningPlayerState() == Context.Wallet;
		Selection.Appendf(TEXT("%s force %d"), bOwned ? TEXT("Selected: your") : TEXT("Read only: teammate's"),
			Context.Force->ForceNumber);
		if (bOwned && Context.Controller->GetSelectedForces().Num() > 1)
			Selection.Appendf(TEXT("  \u00B7  %d selected"), Context.Controller->GetSelectedForces().Num());
	}
	else
		Selection << TEXT("Click badge/unit  \u00B7  Shift / box: several  \u00B7  1-4 (solo 1-5)");
	Paint.Text(Selection.ToView(), X, Row2, 10.f, Palette::Text, false, EAlign::Left, TextWidth);
	Paint.DrawKey(KeysRight - KeysWidth, Row1, TEXT("F4"), TEXT("Show deck"));
	Paint.DrawKey(KeysRight - KeysWidth, Row2, TEXT("F"), TEXT("Centre selection"));
}

void DrawModeBar(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	const FRect& Mode = Layout.Bottom;
	const ACommandPlayerController* Controller = Context.Controller;
	Paint.Panel(Mode);
	const float X = Mode.X + Pad + 6.f;
	const float Row1 = Mode.Y + 10.f;
	const float Row2 = Mode.Y + 36.f;
	const float KeysRight = Mode.Right() - Pad;
	float KeysWidth;
	if (Controller->IsPlacingBuilding())
		KeysWidth = FMath::Max(Paint.KeyWidth(TEXT("LMB"), TEXT("Place")) + 12.f + Paint.KeyWidth(TEXT("RMB / Esc"), TEXT("Cancel")),
			Paint.KeyWidth(TEXT("Shift+LMB"), TEXT("places another")));
	else if (Controller->IsAssigningOrder())
		KeysWidth = FMath::Max(Paint.KeyWidth(TEXT("LMB"), TEXT("Assign")), Paint.KeyWidth(TEXT("RMB / Esc"), TEXT("Cancel")));
	else
		KeysWidth = FMath::Max(Paint.KeyWidth(TEXT("F4"), TEXT("Show deck")), Paint.TextWidth(TEXT("Build bar always visible"), 8.f));
	const float TextWidth = KeysRight - KeysWidth - 16.f - X;
	const FModeGeometry Geometry{ X, Row1, Row2, KeysRight, KeysWidth, TextWidth };
	if (Controller->IsPlacingBuilding())
		DrawPlacementMode(Paint, Context, Mode, Geometry);
	else if (Controller->IsAssigningOrder())
		DrawOrderMode(Paint, Context, Mode, Geometry);
	else
		DrawHiddenMode(Paint, Context, Mode, Geometry);
}

}
