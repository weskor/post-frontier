#include "ForceBar.h"
#include "HUDPanels.h"
#include "ArmyGroup.h"
#include "CommandPlayerController.h"
#include "CommandGameState.h"
#include "Commands/OrderGraph.h"
#include "Commands/BranchCommands.h"
#include "EngineUtils.h"
#include "Headquarters.h"
#include "MapRegion.h"

namespace CommandHUDPanels
{
namespace
{
void TargetName(const ACommandGameState* State, int32 Index, FStringBuilderBase& Text)
{
	const AMapRegion* Region = State ? ForceOrderGraph::Region(*State, Index) : nullptr;
	if (Region)
		Text << Region->DisplayName.ToString();
	else
		Text << TEXT("current position");
}
void OrderName(const FContext& Context, EForceVerb Verb, int32 Region, const AActor* Structure, FStringBuilderBase& Text)
{
	Text << OrderTitle(Verb);
	if (Verb != EForceVerb::Retreat)
	{
		Text << TEXT(" ");
		if (const ACommandBuilding* Building = Cast<ACommandBuilding>(Structure); IsValid(Building) && Building->GetDefinition())
			Text << Building->GetDefinition()->DisplayName.ToString();
		else if (IsValid(Structure) && Structure->IsA<AHeadquarters>())
			Text << TEXT("HQ");
		else
			TargetName(Context.State, Region, Text);
	}
}
ForceCardPolicy::EState CardState(EForceStatus Status)
{
	switch (Status)
	{
	case EForceStatus::Marching:
		return ForceCardPolicy::EState::Marching;
	case EForceStatus::Withdrawing:
		return ForceCardPolicy::EState::Withdrawing;
	case EForceStatus::Retreating:
		return ForceCardPolicy::EState::Retreating;
	case EForceStatus::Refilling:
		return ForceCardPolicy::EState::Refilling;
	default:
		return ForceCardPolicy::EState::Holding;
	}
}
}

void CollectOwnForces(const FContext& Context, TArray<AArmyGroup*, TInlineAllocator<6>>& Out)
{
	Out.Reset();
	if (!Context.Controller || !Context.Wallet)
		return;
	for (TActorIterator<AArmyGroup> It(Context.Controller->GetWorld()); It; ++It)
		if (It->GetOwningPlayerState() == Context.Wallet && Context.Controller->IsSelectableForce(*It))
			Out.Add(*It);
	Out.Sort([](const AArmyGroup& A, const AArmyGroup& B) { return A.ForceNumber < B.ForceNumber; });
}

void ForEachForceCard(const FContext& Context, const FLayout& Layout, TFunctionRef<void(AArmyGroup*, const FRect&)> Visit)
{
	if (!Context.Controller || !Context.Wallet || !Context.State)
		return;
	const int32 Count = Context.Forces.Num();
	if (Count > 0)
	{
		const FRect& Bar = Layout.ForceBar;
		const float Width = (Bar.W - Gap * (Count - 1)) / Count;
		for (int32 Index = 0; Index < Count; ++Index)
			Visit(Context.Forces[Index], { Bar.X + Index * (Width + Gap), Bar.Y, Width, Bar.H });
	}
	if (CanPingInspectedForce(Context))
		Visit(const_cast<AArmyGroup*>(Context.Force), { Layout.Inspector.X, Layout.Inspector.Y, TeammateCardWidth, Layout.Inspector.H });
}

static void ReadForceStatus(const FContext& Context, const AArmyGroup& Force, int32 ETA, FForceCard& Card)
{
	TStringBuilder<128> Target, Threat;
	Card.State = ForceCardPolicy::ResolveState(CardState(Force.Status), Force.Verb == EForceVerb::Attack, Force.bHoldResponding, Force.ResumeCount);
	const bool bWithdrawal = Card.State == ForceCardPolicy::EState::Withdrawing;
	const int32 Arrival = bWithdrawal || Force.Status == EForceStatus::Retreating ? Force.WaypointRegionIndex : Force.TargetRegionIndex;
	TargetName(Context.State, Arrival, Target);
	if (const ACommandBuilding* Asset = Cast<ACommandBuilding>(Force.HoldThreatenedAsset); IsValid(Asset) && Asset->GetDefinition())
	{
		if (Asset->Kind == EBuildingKind::Extractor)
			Threat << TEXT("Drill Rig");
		else
			Threat << Asset->GetDefinition()->DisplayName.ToString();
		Threat << TEXT(" under attack");
	}
	else if (Force.HoldThreatKind == EHoldThreatKind::Headquarters)
		Threat << TEXT("HQ under attack");
	else if (Force.HoldThreatKind == EHoldThreatKind::Force)
		Threat << TEXT("Force under attack");
	else
		Threat << TEXT("Hostiles in region");
	FormatForceCardStatus({ Card.State, Card.Joined, Card.Capacity, Force.ResumeCount, ETA, Target.ToView(), Threat.ToView() }, Card.Status);
}

// Production line of a producer-backed card (ui.md surface 2): a recruit held at the producer says why.
static void AppendRefillLine(const AArmyGroup& Force, EProductionState State, FStringBuilderBase& Text)
{
	Text << TEXT("Refill: ");
	if (Force.RecruitsWaiting > 0)
		Text.Appendf(TEXT("HELD \u00B7 %s \u00B7 %d recruit%s waiting"), Force.bSupplyCutOff ? TEXT("cut off") : TEXT("exit blocked"),
			Force.RecruitsWaiting, Force.RecruitsWaiting == 1 ? TEXT("") : TEXT("s"));
	else
		Text << StatusText(State);
	if (Force.RecruitsInTransit > 0)
		Text.Appendf(TEXT(" \u00B7 %d travelling"), Force.RecruitsInTransit);
}

// Tier-2 branch on the card (ui.md surface 5): how many members already took the branch, and whether it could be
// bought now. Information only; the producer's panel is where it is bought.
static void ReadBranch(const FContext& Context, const AArmyGroup& Force, FForceCard& Card)
{
	Card.RefitLine.Reset();
	Card.bRefitting = false;
	Card.bBranchAffordable = false;
	const ACommandBuilding* Producer = Card.Producer;
	if (!Producer)
		return;
	const UArmyUnitDefinition* Branch = Producer->Branch.Phase == EBranchPhase::Done ? Producer->GetBranchDefinition() : nullptr;
	if (Branch)
	{
		TArray<BranchPolicy::FMember, TInlineAllocator<6>> Living;
		for (const AArmyUnit* Unit : Force.GetUnits())
			if (IsValid(Unit) && Unit->IsAlive())
				Living.Add({ Unit->GetCompositionSlot(), Unit->GetUnitIndex() });
		Card.Refit = BranchPolicy::RefitProgress(Living, Producer->GetBranchUnitIndex());
		Card.bRefitting = !Card.Refit.IsComplete();
		if (Card.bRefitting)
			BranchPolicy::AppendRefitLine(Card.RefitLine, Card.Refit, Branch->DisplayName.ToString());
	}
	Card.bBranchAffordable = Card.bOwned && BranchPolicy::Evaluate(FBranchCommands::MakeInput(*Producer, Context.Wallet)).IsAccepted();
}

void ReadForceCard(const FContext& Context, const AArmyGroup& Force, int32 ETA, FForceCard& Card)
{
	Card.Title.Reset();
	Card.Order.Reset();
	Card.Status.Reset();
	Card.Production.Reset();
	Card.ProductionProgress = 0.f;
	Card.Force = &Force;
	Card.Joined = Force.GetJoinedCount();
	Card.Travelling = Force.GetPendingRecruitCount();
	Card.Capacity = Force.GetCapacity();
	Card.bOwned = Force.GetOwningPlayerState() == Context.Wallet;
	Card.bHighlighted = Context.Controller && Context.Controller->IsForceHighlighted(&Force);
	const ACommandBuilding* Producer = Force.GetProductionBuilding();
	Card.Producer = IsValid(Producer) && Producer->IsAlive() ? Producer : nullptr;
	Card.Definition = Card.Producer ? Card.Producer->GetProductionDefinition() : nullptr;
	if (!Card.Definition)
		for (const AArmyUnit* Unit : Force.GetUnits())
			if (IsValid(Unit) && Unit->IsAlive())
			{
				Card.Definition = Unit->GetDefinition();
				break;
			}
	Card.Title.Appendf(TEXT("%d  "), Force.ForceNumber);
	if (Card.Definition)
		Card.Title << Card.Definition->DisplayName.ToString();
	else
		Card.Title << TEXT("Force");
	if (!Card.bOwned && IsValid(Force.GetOwningPlayerState()))
		Card.Title.Appendf(TEXT(" \u00B7 C%d (read only)"), Force.GetOwningPlayerState()->CommanderIndex + 1);
	OrderName(Context, Force.Verb, Force.TargetRegionIndex, Force.TargetStructure, Card.Order);
	for (int32 Index = 1; Index < Force.Orders.Num(); ++Index)
	{
		Card.Order << TEXT(" \u2192 ");
		const FForceOrder& Order = Force.Orders[Index];
		OrderName(Context, Order.Verb, Order.RegionIndex, Order.Structure, Card.Order);
	}
	ReadForceStatus(Context, Force, ETA, Card);
	if (Card.Producer)
	{
		const float Duration = Card.Producer->GetProductionDuration();
		Card.ProductionProgress = Duration > 0.f ? FMath::Clamp(Card.Producer->ProductionProgressSeconds / Duration, 0.f, 1.f) : 0.f;
		AppendRefillLine(Force, Card.Producer->GetProductionState(), Card.Production);
	}
	else
		Card.Production << TEXT("Orphan \u00B7 no reinforcements");
	ReadBranch(Context, Force, Card);
}

static const TCHAR* ForceVerbRule(const FForceCard& Card)
{
	switch (Card.Force->Verb)
	{
	case EForceVerb::Attack:
		if (Card.Force->RetreatThreshold == ERetreatThreshold::Never)
			return TEXT("Fight + chase; no automatic withdrawal.");
		return Card.Producer ? TEXT("Fight + chase; withdraw; resume at 80%.") : TEXT("Fight + chase; withdraw to safety; no refill.");
	case EForceVerb::Retreat:
		return Card.Producer ? TEXT("+25% sprint; no fire; refill then hold/queue.") : TEXT("+25% sprint; no fire; then hold/queue.");
	default:
		return TEXT("Fight en route; hold; never auto-retreat.");
	}
}
const TCHAR* ForceTargetRule(const UArmyUnitDefinition* Definition)
{
	return Definition && Definition->DamageType == EDamageType::Demolition
		? TEXT("Acquire: structures first, then nearest.")
		: TEXT("Acquire: counter-class first, then nearest.");
}

void ForEachForceCardButton(const FForceCard& Card, const FRect& Rect, TFunctionRef<void(const FButton&, FStringView)> Visit)
{
	const float X = Rect.X + 6.f, Width = Rect.W - 12.f;
	if (!Card.bOwned)
	{
		Visit({ EHUDAction::PingTeammateForce, { X, Rect.Y + 125.f, Width, 22.f }, EBlock::None, false, 0 }, TEXT("Need help here [G]"));
		return;
	}
	Visit({ EHUDAction::ForceCardAttack, { X, Rect.Y + 125.f, (Width - 4.f) * .5f, 22.f }, EBlock::None,
			  Card.Force->Verb == EForceVerb::Attack, 0 },
		TEXT("Attack [A]"));
	Visit({ EHUDAction::ForceCardRetreat, { X + (Width + 4.f) * .5f, Rect.Y + 125.f, (Width - 4.f) * .5f, 22.f }, EBlock::None,
			  Card.Force->Verb == EForceVerb::Retreat, 0 },
		TEXT("Retreat [R]"));
	if (Card.Producer)
		Visit({ EHUDAction::ForceCardProduction, { Rect.Right() - 65.f, Rect.Y + 101.f, 59.f, 18.f }, EBlock::None, false, 0 },
			Card.Producer->bProductionEnabled ? TEXT("Pause") : TEXT("Resume"));
	constexpr ERetreatThreshold Values[] = { ERetreatThreshold::Never, ERetreatThreshold::Percent25, ERetreatThreshold::Percent40, ERetreatThreshold::Percent60 };
	constexpr EHUDAction Actions[] = { EHUDAction::ForceCardNever, EHUDAction::ForceCard25, EHUDAction::ForceCard40, EHUDAction::ForceCard60 };
	const TCHAR* Labels[] = { TEXT("Never"), TEXT("25%"), TEXT("40%"), TEXT("60%") };
	for (int32 Index = 0; Index < 4; ++Index)
		Visit({ Actions[Index], { X + Index * (Width + 4.f) * .25f, Rect.Y + 164.f, (Width - 12.f) * .25f, 18.f }, EBlock::None,
				  Card.Force->RetreatThreshold == Values[Index], 0 },
			Labels[Index]);
}

EHUDAction HitTestForceCard(const FForceCard& Card, const FRect& Rect, const FVector2D& Point)
{
	EHUDAction Result = EHUDAction::None;
	ForEachForceCardButton(Card, Rect, [&](const FButton& Button, FStringView) {
		if (Button.Rect.Contains(Point))
			Result = Button.Action;
	});
	return Result;
}

// Chips between the unit type and the strength (ui.md surfaces 2 and 5). Each is a word or a glyph plus a word, so no
// state rests on colour. T2 needs an unbought branch and REFIT a finished one, so at most two chips ever show.
// Returns the left edge of the chips, RightEdge when there are none.
static float DrawCardChips(const FPainter& Paint, const FForceCard& Card, const FRect& Rect, float RightEdge)
{
	TArray<TPair<FString, FLinearColor>, TInlineAllocator<2>> Chips;
	if (Card.Force->bSupplyCutOff)
		Chips.Emplace(TEXT("CUT OFF"), Palette::Bad);
	if (Card.bRefitting)
	{
		TStringBuilder<24> Text;
		BranchPolicy::AppendRefitChip(Text, Card.Refit);
		Chips.Emplace(FString(Text.ToView()), Palette::Warn);
	}
	if (Card.bBranchAffordable)
		Chips.Emplace(TEXT("\u25B2 T2"), Palette::Good);
	float Left = RightEdge;
	for (int32 Index = Chips.Num() - 1; Index >= 0; --Index)
	{
		const float Width = Paint.TextWidth(Chips[Index].Key, 7.5f, true) + 10.f;
		const FRect Chip{ Left - Width - 4.f, Rect.Y + 3.f, Width, 14.f };
		Paint.Fill(Chip, FLinearColor(Chips[Index].Value.R, Chips[Index].Value.G, Chips[Index].Value.B, .2f));
		Paint.Outline(Chip, Chips[Index].Value);
		Paint.TextIn(Chips[Index].Key, Chip, 7.5f, Chips[Index].Value, true, EAlign::Center);
		Left = Chip.X;
	}
	return Left;
}

void DrawForceCard(const FPainter& Paint, const FForceCard& Card, const FRect& Rect, const FVector2D& Mouse)
{
	const int32 Owner = IsValid(Card.Force->GetOwningPlayerState()) ? Card.Force->GetOwningPlayerState()->CommanderIndex : 0;
	const FLinearColor Accent = AArmyUnit::GetCommanderColor(Owner);
	Paint.Fill(Rect, Rect.Contains(Mouse) ? Palette::CardHover : Palette::Card);
	Paint.Outline(Rect, Card.bHighlighted ? Accent : Palette::Edge, Card.bHighlighted ? 2.f : 1.f);
	const float X = Rect.X + 6.f, Width = Rect.W - 12.f;
	TStringBuilder<32> Strength;
	Strength.Appendf(TEXT("%d/%d"), Card.Joined, Card.Capacity);
	const float StrengthLeft = Rect.Right() - 12.f - Paint.TextWidth(Strength.ToView(), 10.f);
	const float ChipsLeft = DrawCardChips(Paint, Card, Rect, StrengthLeft + 4.f);
	const float TitleWidth = ChipsLeft < StrengthLeft ? ChipsLeft - X - 4.f : Width - 37.f;
	Paint.Text(Card.Title.ToView(), X, Rect.Y + 5.f, 9.5f, Accent, true, EAlign::Left, TitleWidth);
	Paint.Text(Strength.ToView(), Rect.Right() - 6.f, Rect.Y + 5.f, 10.f, Palette::Text, true, EAlign::Right);
	Paint.Text(Card.Order.ToView(), X, Rect.Y + 24.f, 9.f, OrderColor(Card.Force->Verb), true, EAlign::Left, Width);
	Paint.Text(Card.Status.ToView(), X, Rect.Y + 42.f, 8.f, Card.State == ForceCardPolicy::EState::Withdrawing ? Palette::Warn : Palette::Text,
		false, EAlign::Left, Width);
	Paint.Text(ForceVerbRule(Card), X, Rect.Y + 60.f, 8.f, Palette::Muted, false, EAlign::Left, Width);
	if (Card.bRefitting)
		Paint.Text(Card.RefitLine.ToView(), X, Rect.Y + 74.f, 7.8f, Palette::Warn, false, EAlign::Left, Width);
	else
		Paint.Text(TEXT("Structure order first; keep target in range."), X, Rect.Y + 74.f, 7.8f, Palette::Faint, false, EAlign::Left, Width);
	Paint.Text(ForceTargetRule(Card.Definition), X, Rect.Y + 87.f, 7.8f, Palette::Faint, false, EAlign::Left, Width);
	// Narrow cards drop the "Refill: " label before they would clip the travelling count.
	FStringView Production = Card.Production.ToView();
	const float ProductionWidth = Width - (Card.bOwned && Card.Producer ? 63.f : 0.f);
	if (Production.StartsWith(TEXT("Refill: ")) && Paint.TextWidth(Production, 8.f) > ProductionWidth)
		Production.RightChopInline(8);
	Paint.Text(Production, X, Rect.Y + 101.f, 8.f, Palette::Muted, false, EAlign::Left, ProductionWidth);
	Paint.Bar({ X, Rect.Y + 120.f, Width, 3.f }, Card.ProductionProgress, Accent);
	TStringBuilder<64> Threshold;
	Threshold << TEXT("Retreat threshold \u00B7 Attack only");
	if (!Card.bOwned)
	{
		if (Card.Force->RetreatThreshold == ERetreatThreshold::Never)
			Threshold << TEXT(" \u00B7 Never");
		else
			Threshold.Appendf(TEXT(" \u00B7 %d%%"), static_cast<int32>(Card.Force->RetreatThreshold));
	}
	Paint.Text(Threshold.ToView(), X, Rect.Y + 150.f, 7.8f, Palette::Muted, false, EAlign::Left, Width);
	ForEachForceCardButton(Card, Rect, [&](const FButton& Button, FStringView Label) {
		Paint.Fill(Button.Rect, Button.Rect.Contains(Mouse) ? Palette::CardHover : Palette::Key);
		Paint.Outline(Button.Rect, Button.bActive ? Accent : Palette::KeyEdge);
		Paint.TextIn(Label, Button.Rect, 8.f, Palette::Text, Button.bActive, EAlign::Center);
	});
}

void DrawForceBar(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	Paint.Fill(Layout.ForceBar, Palette::Card);
	FVector2D Mouse(-1.f, -1.f);
	float X, Y;
	if (Context.Controller && Context.Controller->GetMousePosition(X, Y))
		Mouse = FVector2D(X, Y) / Layout.Scale;
	const ACommandHUD* HUD = Context.Controller ? Cast<ACommandHUD>(Context.Controller->GetHUD()) : nullptr;
	ForEachForceCard(Context, Layout, [&](AArmyGroup* Force, const FRect& Rect) {
		FForceCard Card;
		ReadForceCard(Context, *Force, HUD ? HUD->GetForceETA(*Force) : INDEX_NONE, Card);
		DrawForceCard(Paint, Card, Rect, Mouse);
	});
}
}
