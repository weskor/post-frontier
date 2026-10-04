#pragma once

#include "JevIntentView.h"
#include "HUDTypes.h"
#include "InputCoreTypes.h"
#include "ForceOrders.h"

struct FObjectiveEvent;
struct FObjectiveForce;
class AMapRegion;

namespace CommandHUDPanels
{
struct FBuildHotkey
{
	const FKey& Key;
	const TCHAR* Letter;
	EHUDAction Action;
};
extern const FBuildHotkey BuildHotkeys[6];

FContext MakeContext(const ACommandPlayerController* Controller);
bool CanPingInspectedForce(const FContext& Context);
FLayout MakeLayout(const FContext& Context, float PixelWidth, float PixelHeight);
const UMatchContent* MatchContent(const FContext& Context);
const UArmyUnitDefinition* ProductionDefinition(const FContext& Context);
const TCHAR* StatusText(EProductionState State);
float BodyTop(const FRect& Inspector);
FRect Column(const FRect& Inspector, int32 Index, int32 Count);
FRect Row(const FRect& ColumnRect, int32 Index, int32 Count = 3);
FRect BuildCard(const FRect& Build, int32 Index, int32 Count);
FRect ResearchCard(const FRect& Inspector, int32 Index);
FRect CancelButton(const FRect& Inspector);
// Production panel, FORCE TYPE column (ui.md surface 5). The unlocked picker is a two-column grid of 28 px chips, because
// Row would shrink five rows to about 14 px; the locked row keeps Row 0 and the 30 px TIER 2 BRANCH area sits under it.
FRect RecipeChip(const FRect& ColumnRect, int32 Slot);
FRect BranchArea(const FRect& ColumnRect);
bool IsPanelPoint(const FContext& Context, const FLayout& Layout, const FVector2D& VirtualPoint);
bool OverlayClearsPanels(const FContext& Context, const FLayout& Layout, const FRect& Rect);
void ForEachForceBadge(const FPainter& Paint, const FContext& Context, const FLayout& Layout,
	TFunctionRef<void(AArmyGroup*, const FRect&)> Visit);
void ForEachButton(const FContext& Context, const FLayout& Layout, TFunctionRef<void(const FButton&)> Visit);
EHUDAction HitTest(const FContext& Context, const FLayout& Layout, const FVector2D& VirtualPoint);
const TCHAR* OrderTitle(EForceVerb Verb);
FLinearColor OrderColor(EForceVerb Verb);
const TCHAR* ForceStatusTitle(EForceStatus Status);
const TCHAR* ResearchName(EArmyDoctrine Doctrine);
FLinearColor Tint(const FLinearColor& Accent, float Amount, float Alpha);
bool ProjectOverlay(const FPainter& Paint, const FContext& Context, const FVector& Position, FVector2D& Screen);
bool OverlayFits(const FPainter& Paint, const FRect& Rect);
FForces CountForces(const FContext& Context);
void BlockReason(const FButton& Button, FStringBuilderBase& Reason);
float DrawBlockReason(const FPainter& Paint, const FButton& Button, float X, float Y, float Size, float MaxWidth,
	EAlign Align = EAlign::Left);
void DrawUnitHealthBars(const FPainter& Paint, const FContext& Context);
void DrawHeadquartersOverlays(const FPainter& Paint, const FContext& Context);
void DrawBuildingOverlays(const FPainter& Paint, const FContext& Context);
void DrawSectorOverlays(const FPainter& Paint, const FContext& Context);
void DrawForceLabels(const FPainter& Paint, const FContext& Context, const FLayout& Layout);
void DrawPingMarkers(const FPainter& Paint, const FContext& Context);
void DrawTopBar(const FPainter& Paint, const FContext& Context, const FForces& Forces, const FLayout& Layout);
FRect ObjectiveContributors(const FRect& Strip);
int32 ObjectiveForceColumns(const FRect& Strip);
FStringView ObjectiveRegionName(FStringView Name);
void DrawObjectiveForceBadge(const FPainter& Paint, const FContext& Context, const FObjectiveForce& Force,
	const FRect& Rect, float Alpha = 1.f);
void ForEachAlert(const FContext& Context, const FLayout& Layout,
	TFunctionRef<void(const FObjectiveEvent&, const FRect&, float)> Visit);
bool HitTestAlert(const FContext& Context, const FLayout& Layout, const FVector2D& VirtualPoint,
	FVector& OutWorld, int32& OutSequence);
void DrawObjectiveAlerts(const FPainter& Paint, const FContext& Context, const FLayout& Layout);
void DrawBuildCard(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover);
void DrawResearchCard(const FPainter& Paint, const FButton& Button, bool bHover);
void DrawCommandRow(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover);

// What the FORCE TYPE column shows for the selected producer's tier-2 branch.
enum class EBranchArea : uint8
{
	Hidden,
	// An unlocked type: the hint takes the free cell after the picker's chips.
	Hint,
	// The TIER 2 BRANCH button, ready or greyed with its reason.
	Button,
	Bar,
	Done
};
EBranchArea ReadBranchArea(const FContext& Context, BranchPolicy::FDecision& OutDecision);
// The picker types of the catalogue: base units only, in catalogue order.
int32 RecipeCount(const FContext& Context);
void DrawBranchStatus(const FPainter& Paint, const FContext& Context, const FRect& RecipeColumn);
void DrawBranchButton(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover);
void DrawButton(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover);
void DrawFortifyDock(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover);
void DrawFortifyCursor(const FPainter& Paint, const FContext& Context, const FLayout& Layout);
void DrawFortifyBadge(const FPainter& Paint, const AMapRegion& Region, const FVector2D& Screen);
void DrawBuildPanel(const FPainter& Paint, const FContext& Context, const FLayout& Layout);
void DrawInspectorHeader(const FPainter& Paint, const FRect& Inspector, const FLinearColor& Accent, FStringView Title,
	FStringView Subtitle, int32 Owner, int32 Health, int32 MaxHealth, FStringView Status, const FLinearColor& StatusColor);
void ColumnLabel(const FPainter& Paint, const FRect& ColumnRect, FStringView Label, FStringView Detail = FStringView(),
	const FLinearColor& DetailColor = Palette::Faint);
void DrawBuildingInspector(const FPainter& Paint, const FContext& Context, const FRect& Inspector);
void DrawForceInspector(const FPainter& Paint, const FContext& Context, const FRect& Inspector);
void DrawOverview(const FPainter& Paint, const FContext& Context, const FForces& Forces, const FRect& Inspector);
void DrawModeBar(const FPainter& Paint, const FContext& Context, const FLayout& Layout);
void DrawFeedback(const FPainter& Paint, const FContext& Context, const FLayout& Layout);
void DrawScreenButton(const FPainter& Paint, const FButton& Button, bool bHover);
void DrawScreen(const FPainter& Paint, const FContext& Context, const FLayout& Layout, EHUDAction Hover);
void DrawScreenLine(const FPainter& Paint, const FRect& Panel, const TCHAR* Text, int32 Index, const FLinearColor& Color = Palette::Text);
void DrawResult(const FPainter& Paint, const FContext& Context, const FRect& Panel);
void DrawMinimap(const FPainter& Paint, ACommandPlayerController* Controller, const FLayout& Layout);

// JEV intent display (timeline bar, memo feed, region badges); every value derives from the replicated plans.
struct FJevIntentModel
{
	JevIntentView::FPlans Plans;
	JevIntent::FTimeline Timeline;
	JevIntent::FBadges Badges;
	float Now = 0.f;
};
struct FJevMemoRow
{
	FRect Rect;
	const JevIntent::FMemo* Memo = nullptr;
	float Alpha = 1.f;
};
void BuildJevIntentModel(const FContext& Context, FJevIntentModel& Model);
// Posts memos for plans that appeared or changed since the last observation.
void ObserveJevIntent(const FContext& Context);
// Empty (zero width) while no plan is published.
FRect JevTimelineRect(const FContext& Context, const FLayout& Layout, const FJevIntentModel& Model);
// Rows that fit between the timeline and the feedback strip, newest first.
int32 JevMemoRows(const FContext& Context, const FLayout& Layout, const FJevIntentModel& Model,
	FJevMemoRow (&Rows)[JevIntent::MemoVisible]);
void ForEachJevIntentPanel(const FContext& Context, const FLayout& Layout, TFunctionRef<void(const FRect&)> Visit);
const TCHAR* JevVerbTag(JevPlanner::EVerb Verb, bool bEscalated);
// Text of a world-map badge: "JEV  Attack  <region>  0:20" or "JEV  Escalated: defending <region>".
void JevBadgeLabel(const FContext& Context, const JevIntent::FRegionBadge& Badge, FStringBuilderBase& Label);
void DrawJevIntent(const FPainter& Paint, const FContext& Context, const FLayout& Layout, const FJevIntentModel& Model);
void DrawJevRegionBadges(const FPainter& Paint, const FContext& Context, const FLayout& Layout, const FJevIntentModel& Model);
}
