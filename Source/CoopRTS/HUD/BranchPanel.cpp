#include "HUDPanels.h"
#include "Commands/BranchCommands.h"
#include "CommandPlayerController.h"
#include "Content/UnitDefinition.h"

// The tier-2 branch in the production panel's FORCE TYPE column (ui.md surface 5): the TIER 2 BRANCH button, its
// greyed states, the upgrade bar and the done state. Every text comes from BranchPolicy and the unit data.
namespace CommandHUDPanels
{
EBranchArea ReadBranchArea(const FContext& Context, BranchPolicy::FDecision& OutDecision)
{
	OutDecision = {};
	const ACommandBuilding* Building = Context.Building;
	if (!Building || !Building->IsProducer() || !Building->IsComplete())
		return EBranchArea::Hidden;
	OutDecision = BranchPolicy::Evaluate(FBranchCommands::MakeInput(*Building, Context.Wallet));
	if (Building->Branch.Phase != EBranchPhase::None)
	{
		if (!Building->GetBranchDefinition())
			return EBranchArea::Hidden;
		return Building->IsUpgrading() ? EBranchArea::Bar : EBranchArea::Done;
	}
	switch (OutDecision.Verdict)
	{
	case BranchPolicy::EVerdict::TypeNotLocked:
		return EBranchArea::Hint;
	case BranchPolicy::EVerdict::Accepted:
	case BranchPolicy::EVerdict::Stunned:
	case BranchPolicy::EVerdict::NeedResources:
		return EBranchArea::Button;
	default:
		return EBranchArea::Hidden;
	}
}

static void DrawHint(const FPainter& Paint, const FContext& Context, const FRect& RecipeColumn, const BranchPolicy::FDecision& Decision)
{
	// The picker leaves a cell free after its chips.
	const FRect Cell = RecipeChip(RecipeColumn, FMath::Min(RecipeCount(Context), 5));
	TStringBuilder<64> Reason;
	BranchPolicy::AppendReason(Reason, Decision);
	Paint.Text(TEXT("TIER 2 BRANCH"), Cell.X + 4.f, Cell.Y + 3.f, 8.f, Palette::Muted, true, EAlign::Left, Cell.W - 4.f);
	Paint.Text(Reason.ToView(), Cell.X + 4.f, Cell.Y + 15.f, 8.f, Palette::Faint, false, EAlign::Left, Cell.W - 4.f);
}

void DrawBranchStatus(const FPainter& Paint, const FContext& Context, const FRect& RecipeColumn)
{
	BranchPolicy::FDecision Decision;
	const EBranchArea Area = ReadBranchArea(Context, Decision);
	if (Area == EBranchArea::Hint)
	{
		DrawHint(Paint, Context, RecipeColumn, Decision);
		return;
	}
	const UArmyUnitDefinition* Branch = Context.Building ? Context.Building->GetBranchDefinition() : nullptr;
	if (!Branch || (Area != EBranchArea::Bar && Area != EBranchArea::Done))
		return;
	const FRect Rect = BranchArea(RecipeColumn);
	const FString Name = Branch->DisplayName.ToString();
	TStringBuilder<96> Text;
	if (Area == EBranchArea::Bar)
	{
		const float Progress = Context.Building->Branch.ProgressSeconds;
		Paint.Bar(Rect, Progress / BranchPolicy::UpgradeSeconds, Palette::Warn.CopyWithNewOpacity(.5f));
		Paint.Outline(Rect, Palette::Warn.CopyWithNewOpacity(.85f));
		BranchPolicy::AppendUpgradeText(Text, Name, Progress);
		Paint.TextIn(Text.ToView(), Rect, 9.5f, Palette::Text, true, EAlign::Center, 6.f);
		return;
	}
	Paint.Fill(Rect, Tint(Palette::Good, .12f, .9f));
	Paint.Outline(Rect, Palette::Good.CopyWithNewOpacity(.7f));
	BranchPolicy::AppendDoneText(Text, Name, Branch->BranchSummary.ToString());
	Paint.TextIn(Text.ToView(), Rect, 9.5f, Palette::Good, true, EAlign::Left, 9.f);
}

void DrawBranchButton(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	const UArmyUnitDefinition* Branch = Context.Building ? Context.Building->GetBranchDefinition() : nullptr;
	if (!Branch)
		return;
	BranchPolicy::FDecision Decision;
	ReadBranchArea(Context, Decision);
	// Greyed buttons stay clickable: the server's reason answers the click.
	const bool bReady = Decision.IsAccepted() && Button.Block == EBlock::None;
	const FRect& Rect = Button.Rect;
	Paint.Fill(Rect, !bReady ? Palette::CardOff : bHover ? Palette::CardHover
														 : Palette::Card);
	Paint.Fill({ Rect.X, Rect.Y, 3.f, Rect.H }, Branch->Accent.CopyWithNewOpacity(bReady ? 1.f : .3f));
	Paint.Outline(Rect, bReady && bHover ? Branch->Accent.CopyWithNewOpacity(.7f) : Palette::Edge);
	TStringBuilder<96> Title, Line;
	BranchPolicy::AppendBranchTitle(Title, Branch->DisplayName.ToString(), Branch->BranchSummary.ToString());
	Paint.Text(Title.ToView(), Rect.X + 9.f, Rect.Y + 3.f, 9.5f, bReady ? Palette::Text : Palette::Muted, true, EAlign::Left, Rect.W - 14.f);
	if (bReady)
	{
		Line << TEXT("Tier 2 \u00B7 ");
		BranchPolicy::AppendPrice(Line);
	}
	else
		BranchPolicy::AppendReason(Line, Decision);
	const FLinearColor LineColor = bReady ? Palette::Gold
		: Decision.Verdict == BranchPolicy::EVerdict::NeedResources ? Palette::Warn
																	: Palette::Faint;
	Paint.Text(Line.ToView(), Rect.X + 9.f, Rect.Y + 17.f, 8.5f, LineColor, false, EAlign::Left, Rect.W - 14.f);
}
}
