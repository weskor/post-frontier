#pragma once
#include "HUDTypes.h"
#include "ForceOrders.h"
#include "Rules/ForceCardPolicy.h"

namespace CommandHUDPanels
{
constexpr float ForceBarHeight = 188.f;
constexpr float ForceCardMaxWidth = 300.f;
constexpr float TeammateCardWidth = 360.f;
struct FForceCard
{
	const AArmyGroup* Force = nullptr;
	const UArmyUnitDefinition* Definition = nullptr;
	const ACommandBuilding* Producer = nullptr;
	int32 Joined = 0;
	int32 Travelling = 0;
	int32 Capacity = 0;
	bool bOwned = false;
	bool bHighlighted = false;
	float ProductionProgress = 0.f;
	// Tier-2 branch (ui.md surface 5): members that took it so far while others still wear the old form, and whether
	// the producer's branch could be bought now.
	bool bRefitting = false;
	bool bBranchAffordable = false;
	BranchPolicy::FRefitProgress Refit;
	TStringBuilder<96> RefitLine;
	ForceCardPolicy::EState State = ForceCardPolicy::EState::Holding;
	TStringBuilder<128> Title;
	TStringBuilder<256> Order;
	TStringBuilder<256> Status;
	TStringBuilder<128> Production;
};
// Fills Out with the local commander's selectable forces in number order.
void CollectOwnForces(const FContext& Context, TArray<AArmyGroup*, TInlineAllocator<6>>& Out);
void ForEachForceCard(const FContext& Context, const FLayout& Layout,
	TFunctionRef<void(AArmyGroup*, const FRect&)> Visit);
void ReadForceCard(const FContext& Context, const AArmyGroup& Force, int32 ETA, FForceCard& Card);
void FormatForceCardStatus(const ForceCardPolicy::FState& State, FStringBuilderBase& Text);
const TCHAR* ForceTargetRule(const UArmyUnitDefinition* Definition);
void ForEachForceCardButton(const FForceCard& Card, const FRect& Rect,
	TFunctionRef<void(const FButton&, FStringView)> Visit);
EHUDAction HitTestForceCard(const FForceCard& Card, const FRect& Rect, const FVector2D& Point);
void DrawForceCard(const FPainter& Paint, const FForceCard& Card, const FRect& Rect, const FVector2D& Mouse);
void DrawForceBar(const FPainter& Paint, const FContext& Context, const FLayout& Layout);
}
