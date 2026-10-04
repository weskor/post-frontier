#include "HUDPanels.h"
#include "Content/MatchContent.h"

namespace CommandHUDPanels
{
struct FCommandText
{
	TStringBuilder<48> Left;
	TStringBuilder<48> Right;
	FLinearColor Accent = Palette::Friendly;
	FLinearColor RightColor = Palette::Faint;
};

static bool RecipeText(const FContext& Context, const FButton& Button, int32 RecipeIndex, FCommandText& Text)
{
	const UMatchContent* Content = MatchContent(Context);
	const UArmyUnitDefinition* Definition = Content ? Content->Unit(RecipeIndex) : nullptr;
	if (!Definition)
		return false;
	Text.Left << Definition->DisplayName.ToString().ToUpper();
	Text.Accent = Definition->Accent;
	if (Button.Block == EBlock::None || Button.bActive)
	{
		Text.Right.Appendf(TEXT("%d  \u00B7  %d/%.1fs"), Definition->Capacity, Definition->UnitCost, Definition->UnitDuration);
		Text.RightColor = Palette::Gold;
	}
	return true;
}

static void ProductionText(const FContext& Context, const FButton& Button, FCommandText& Text)
{
	const ACommandBuilding* Building = Context.Building;
	const bool bEnabled = Building->bProductionEnabled;
	Text.Left << (!Building->bForceConfigured ? TEXT("START & LOCK") : bEnabled ? TEXT("PAUSE")
																				: TEXT("RESUME"));
	if (!Building->bForceConfigured)
	{
		const UArmyUnitDefinition* Recipe = ProductionDefinition(Context);
		const int32 Fee = Recipe ? ACommandBuilding::GetConfigurationCost(*Recipe) : 0;
		if (Fee > 0 && Button.Block != EBlock::Funds)
			Text.Right.Appendf(TEXT("+%d fee"), Fee);
	}
	else if (Button.Block != EBlock::Terminal && Button.Block != EBlock::Upgrading)
		Text.Right << (bEnabled ? TEXT("enabled") : TEXT("paused"));
	Text.Accent = bEnabled ? Palette::Warn : Palette::Good;
	Text.RightColor = Palette::Muted;
}

static bool CommandText(const FContext& Context, const FButton& Button, FCommandText& Text)
{
	const ACommandBuilding* Building = Context.Building;
	const int32 RecipeIndex = RecipeSlot(Button.Action);
	if (RecipeIndex != INDEX_NONE)
		return RecipeText(Context, Button, RecipeIndex, Text);
	switch (Button.Action)
	{
	case EHUDAction::SelectForce:
		Text.Left << TEXT("SELECT FORCE");
		Text.Accent = Palette::Friendly;
		break;
	case EHUDAction::ToggleProduction:
		ProductionText(Context, Button, Text);
		break;
	case EHUDAction::CancelConstruction:
		Text.Left << TEXT("CANCEL BUILD");
		Text.Accent = Palette::Bad;
		Text.Right.Appendf(TEXT("refund %d"), FMath::FloorToInt((Building->GetDefinition() ? Building->GetDefinition()->BuildCost : 0) * (1.f - Building->ConstructionProgress)));
		Text.RightColor = Palette::Gold;
		break;
	default:
		return false;
	}
	return true;
}

void DrawCommandRow(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	if (!Context.Building)
		return;
	FCommandText Text;
	if (!CommandText(Context, Button, Text))
		return;
	const FRect& Rect = Button.Rect;
	const float TextScale = FMath::Min(1.f, Rect.H / RowHeight);
	const bool bOn = Button.Available();
	const bool bActiveRecipeOrOrder = Button.bActive && Button.Action != EHUDAction::ToggleProduction;
	Paint.Fill(Rect, bActiveRecipeOrOrder ? Tint(Text.Accent, .2f, .96f) : !bOn ? Palette::CardOff
			: bHover                                                            ? Palette::CardHover
																				: Palette::Card);
	Paint.Fill({ Rect.X, Rect.Y, 3.f, Rect.H }, Text.Accent.CopyWithNewOpacity(bOn || Button.bActive ? 1.f : .3f));
	Paint.Outline(Rect, bActiveRecipeOrOrder ? Text.Accent.CopyWithNewOpacity(.85f) : bOn && bHover ? Text.Accent.CopyWithNewOpacity(.7f)
																									: Palette::Edge);
	// A picker chip is half a column wide and 28 px tall: the name sits over the price instead of beside it.
	if (RecipeSlot(Button.Action) != INDEX_NONE && Rect.W < 150.f)
	{
		Paint.Text(Text.Left.ToView(), Rect.X + 11.f, Rect.Y + 3.f, 9.5f, bOn || Button.bActive ? Palette::Text : Palette::Muted, true, EAlign::Left, Rect.W - 16.f);
		if (Text.Right.Len() > 0)
			Paint.Text(Text.Right.ToView(), Rect.X + 11.f, Rect.Y + 16.f, 8.5f, Text.RightColor, false, EAlign::Left, Rect.W - 16.f);
		return;
	}
	float RightWidth = 0.f;
	if (Text.Right.Len() > 0)
		RightWidth = Paint.TextIn(Text.Right.ToView(), Rect, 9.f * TextScale, Text.RightColor, false, EAlign::Right, 9.f);
	else if (!bOn)
	{
		const float Y = Rect.Y + (Rect.H - Paint.LineHeight(9.f * TextScale)) * .5f;
		RightWidth = DrawBlockReason(Paint, Button, Rect.Right() - 9.f, Y, 8.f * TextScale, Rect.W * .64f, EAlign::Right);
	}
	Paint.TextIn(Text.Left.ToView(), Rect, 10.f * TextScale, bOn || Button.bActive ? Palette::Text : Palette::Muted, true, EAlign::Left, 11.f,
		Rect.W - 22.f - RightWidth - 8.f);
}

}
