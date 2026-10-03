#include "ForceBar.h"
#include "HUDPanels.h"
#include "ArmyGroup.h"
#include "CommandPlayerController.h"
#include "CommandGameState.h"
#include "Commands/OrderGraph.h"
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
	case EForceStatus::Marching: return ForceCardPolicy::EState::Marching;
	case EForceStatus::Withdrawing: return ForceCardPolicy::EState::Withdrawing;
	case EForceStatus::Retreating: return ForceCardPolicy::EState::Retreating;
	case EForceStatus::Refilling: return ForceCardPolicy::EState::Refilling;
	default: return ForceCardPolicy::EState::Holding;
	}
}
}

FRect ForceBarRect(const FLayout& Layout)
{
	return { Margin, Layout.Height - Margin - ForceBarHeight, Layout.Width - 2.f * Margin, ForceBarHeight };
}

void ForEachForceCard(const FContext& Context, const FLayout& Layout, TFunctionRef<void(AArmyGroup*, const FRect&)> Visit)
{
	if (!Context.Controller || !Context.Wallet || !Context.State)
		return;
	TArray<AArmyGroup*, TInlineAllocator<5>> Forces;
	for (TActorIterator<AArmyGroup> It(Context.Controller->GetWorld()); It; ++It)
		if (It->GetOwningPlayerState() == Context.Wallet && Context.Controller->IsSelectableForce(*It))
			Forces.Add(*It);
	Forces.Sort([](const AArmyGroup& A, const AArmyGroup& B) { return A.ForceNumber < B.ForceNumber; });
	const FRect Bar = ForceBarRect(Layout);
	const float Width = FMath::Min(300.f, (Bar.W - Gap * (FMath::Max(1, Forces.Num()) - 1)) / FMath::Max(1, Forces.Num()));
	for (int32 Index = 0; Index < Forces.Num(); ++Index)
		Visit(Forces[Index], { Bar.X + Index * (Width + Gap), Bar.Y, Width, Bar.H });
	if (CanPingInspectedForce(Context))
		Visit(const_cast<AArmyGroup*>(Context.Force), { Layout.Inspector.X, Layout.Inspector.Y, 360.f, ForceBarHeight });
}

static void ReadForceStatus(const FContext& Context, const AArmyGroup& Force, int32 ETA, FForceCard& Card)
{
	TStringBuilder<128> Target, Threat;
	const bool bWithdrawal = Force.Status == EForceStatus::Withdrawing
		|| (Force.Verb == EForceVerb::Attack && Force.Status == EForceStatus::Refilling && Force.ResumeCount > 0);
	Card.State = Force.bHoldResponding ? ForceCardPolicy::EState::Responding
		: bWithdrawal ? ForceCardPolicy::EState::Withdrawing
		: CardState(Force.Status);
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
	ForceCardPolicy::Status({ Card.State, Card.Joined, Card.Capacity, Force.ResumeCount, ETA, Target.ToView(), Threat.ToView() }, Card.Status);
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
	Card.Travelling = Force.GetAliveCount() - Card.Joined;
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
	Card.Title << (Card.Definition ? Card.Definition->DisplayName.ToString() : FString(TEXT("Force")));
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
		Card.Production << TEXT("Refill: ") << StatusText(Card.Producer->GetProductionState());
		if (Card.Travelling > 0)
			Card.Production.Appendf(TEXT(" \u00B7 %d travelling"), Card.Travelling);
	}
	else
		Card.Production << TEXT("Orphan \u00B7 no reinforcements");
}

const TCHAR* ForceVerbRule(EForceVerb Verb)
{
	switch (Verb)
	{
	case EForceVerb::Attack: return TEXT("Fight + chase; withdraw; resume at 80%.");
	case EForceVerb::Retreat: return TEXT("+25% sprint, no fire; refill then hold.");
	default: return TEXT("Fight en route; hold; never auto-retreat.");
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
		Card.Force->Verb == EForceVerb::Attack, 0 }, TEXT("Attack [A]"));
	Visit({ EHUDAction::ForceCardRetreat, { X + (Width + 4.f) * .5f, Rect.Y + 125.f, (Width - 4.f) * .5f, 22.f }, EBlock::None,
		Card.Force->Verb == EForceVerb::Retreat, 0 }, TEXT("Retreat [R]"));
	if (Card.Producer)
		Visit({ EHUDAction::ForceCardProduction, { Rect.Right() - 65.f, Rect.Y + 101.f, 59.f, 18.f }, EBlock::None, false, 0 },
			Card.Producer->bProductionEnabled ? TEXT("Pause") : TEXT("Resume"));
	constexpr ERetreatThreshold Values[] = { ERetreatThreshold::Never, ERetreatThreshold::Percent25, ERetreatThreshold::Percent40, ERetreatThreshold::Percent60 };
	constexpr EHUDAction Actions[] = { EHUDAction::ForceCardNever, EHUDAction::ForceCard25, EHUDAction::ForceCard40, EHUDAction::ForceCard60 };
	const TCHAR* Labels[] = { TEXT("Never"), TEXT("25%"), TEXT("40%"), TEXT("60%") };
	for (int32 Index = 0; Index < 4; ++Index)
		Visit({ Actions[Index], { X + Index * (Width + 4.f) * .25f, Rect.Y + 164.f, (Width - 12.f) * .25f, 18.f }, EBlock::None,
			Card.Force->RetreatThreshold == Values[Index], 0 }, Labels[Index]);
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

void DrawForceCard(const FPainter& Paint, const FForceCard& Card, const FRect& Rect, const FVector2D& Mouse)
{
	const int32 Owner = IsValid(Card.Force->GetOwningPlayerState()) ? Card.Force->GetOwningPlayerState()->CommanderIndex : 0;
	const FLinearColor Accent = AArmyUnit::GetCommanderColor(Owner);
	Paint.Fill(Rect, Rect.Contains(Mouse) ? Palette::CardHover : Palette::Card);
	Paint.Outline(Rect, Card.bHighlighted ? Accent : Palette::Edge, Card.bHighlighted ? 2.f : 1.f);
	const float X = Rect.X + 6.f, Width = Rect.W - 12.f;
	Paint.Text(Card.Title.ToView(), X, Rect.Y + 5.f, 9.5f, Accent, true, EAlign::Left, Width - 37.f);
	TStringBuilder<32> Strength;
	Strength.Appendf(TEXT("%d/%d"), Card.Joined, Card.Capacity);
	Paint.Text(Strength.ToView(), Rect.Right() - 6.f, Rect.Y + 5.f, 10.f, Palette::Text, true, EAlign::Right);
	Paint.Text(Card.Order.ToView(), X, Rect.Y + 24.f, 9.f, OrderColor(Card.Force->Verb), true, EAlign::Left, Width);
	Paint.Text(Card.Status.ToView(), X, Rect.Y + 42.f, 8.f, Card.State == ForceCardPolicy::EState::Withdrawing ? Palette::Warn : Palette::Text,
		false, EAlign::Left, Width);
	Paint.Text(ForceVerbRule(Card.Force->Verb), X, Rect.Y + 60.f, 8.f, Palette::Muted, false, EAlign::Left, Width);
	Paint.Text(TEXT("Structure order first; keep target in range."), X, Rect.Y + 74.f, 7.8f, Palette::Faint, false, EAlign::Left, Width);
	Paint.Text(ForceTargetRule(Card.Definition), X, Rect.Y + 87.f, 7.8f, Palette::Faint, false, EAlign::Left, Width);
	Paint.Text(Card.Production.ToView(), X, Rect.Y + 101.f, 8.f, Palette::Muted, false, EAlign::Left, Width - (Card.bOwned && Card.Producer ? 63.f : 0.f));
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
