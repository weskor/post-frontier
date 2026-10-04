#include "PressurePanels.h"

#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "DepositSite.h"
#include "GameState/GameStateEconomy.h"
#include "Headquarters.h"
#include "MapPresentation.h"
#include "MapRegion.h"
#include "PressureView.h"
#include "Rules/EconomyPolicy.h"

namespace CommandHUDPanels
{
namespace
{
const FLinearColor ChipRed(.30f, .045f, .045f, .94f);
const FLinearColor ChipRedText(1.f, .80f, .78f);
const FLinearColor StunYellow(1.f, .84f, .22f);
const FLinearColor StunBack(.12f, .095f, .012f, .95f);
const FLinearColor ClockTime(.90f, .94f, 1.f);
// Bold 10 px text averages about this wide per character; only the chip's click area depends on it.
constexpr float ChipCharWidth = 6.2f;
constexpr float ChipInset = 34.f;
constexpr float StunChipWidth = 84.f;
constexpr float StunChipHeight = 18.f;
// An offline rig's glyph tile hangs this far left of its deposit label (MapChips: centred 16 px out, 24 px wide).
constexpr float OfflineGlyphReach = 28.f;
constexpr float SeparationGap = 2.f;

// Two links with a gap between them.
void DrawChainBreak(const FPainter& Paint, float X, float CenterY, const FLinearColor& Color)
{
	Paint.Outline({ X, CenterY - 3.f, 7.f, 6.f }, Color);
	Paint.Outline({ X + 10.f, CenterY - 3.f, 7.f, 6.f }, Color);
	Paint.Fill({ X + 7.f, CenterY - 4.f, 1.f, 2.f }, Color);
	Paint.Fill({ X + 9.f, CenterY + 2.f, 1.f, 2.f }, Color);
}

void DrawBolt(const FPainter& Paint, float X, float Y, const FLinearColor& Color)
{
	Paint.Fill({ X + 4.f, Y, 3.f, 2.f }, Color);
	Paint.Fill({ X + 3.f, Y + 2.f, 3.f, 2.f }, Color);
	Paint.Fill({ X + 1.f, Y + 4.f, 6.f, 2.f }, Color);
	Paint.Fill({ X + 3.f, Y + 6.f, 3.f, 2.f }, Color);
	Paint.Fill({ X + 2.f, Y + 8.f, 3.f, 2.f }, Color);
}
}

PressureHud::FCutLoss ReadCutLoss(const FContext& Context, TArray<int32>* CutRegions)
{
	PressureHud::FCutLoss Loss;
	if (CutRegions)
		CutRegions->Reset();
	if (!Context.State || !Context.Wallet)
		return Loss;
	const ACommandGameState& State = *Context.State;
	const TArray<ACommandPlayerState*> Roster = FGameStateEconomy::Roster(State);
	// An empty mask means the main fell: that is the end of the battle, not a supply cut.
	if (State.GetConnectedMask(0) == 0 || !Roster.Contains(Context.Wallet))
		return Loss;
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region) || !MapView::IsCutOff(State, 0, Region->RegionIndex))
			continue;
		++Loss.Regions;
		Loss.DataPerSecond += Region->RegionRole == ERegionRole::Reward ? EconomyPolicy::RewardRegionDataRate : 0;
		if (CutRegions)
			CutRegions->Add(Region->RegionIndex);
	}
	if (CutRegions)
		CutRegions->Sort();
	int32 Offline = 0;
	for (const ADepositSite* Deposit : State.Deposits)
		if (IsValid(Deposit) && Deposit->Remaining > 0 && MapView::IsRigOffline(State, *Deposit) && Deposit->Extractor->TeamIndex == 0)
			Offline += Deposit->RatePerSecond();
	// The pool splits evenly among the roster, so a cut costs each commander an equal share.
	Loss.PowerPerSecond = static_cast<double>(Offline) / Roster.Num();
	return Loss;
}

FRect BattleClockRect(const FLayout& Layout)
{
	return { Layout.Top.Right() - Pad - TopHintWidth - Gap - ClockWidth, Layout.Top.Y, ClockWidth, Layout.Top.H };
}

FRect CutChipRect(const FLayout& Layout, const PressureHud::FCutLoss& Loss)
{
	if (!Loss.Any())
		return {};
	TStringBuilder<96> Text;
	PressureHud::AppendCutChip(Text, Loss);
	const float X = Layout.Top.X + Pad + EconomyTextWidth + Gap;
	const float Width = FMath::Min(FMath::Min(Text.Len() * ChipCharWidth + ChipInset, CutChipMaxWidth), BattleClockRect(Layout).X - Gap - X);
	return Width > 0.f ? FRect{ X, Layout.Top.Y, Width, Layout.Top.H } : FRect();
}

void DrawDataGlyph(const FPainter& Paint, float X, float Y, const FLinearColor& Color)
{
	Paint.Outline({ X, Y, 9.f, 9.f }, Color);
	Paint.Fill({ X + 3.f, Y + 3.f, 3.f, 3.f }, Color);
}

void DrawCutChip(const FPainter& Paint, const FLayout& Layout, const PressureHud::FCutLoss& Loss)
{
	const FRect Hit = CutChipRect(Layout, Loss);
	if (Hit.W <= 0.f)
		return;
	const FRect Chip{ Hit.X, Hit.Y + (Hit.H - ChipHeight) * .5f, Hit.W, ChipHeight };
	Paint.Fill(Chip, ChipRed);
	Paint.Outline(Chip, Palette::Bad);
	DrawChainBreak(Paint, Chip.X + 7.f, Chip.Center().Y, Palette::Bad);
	TStringBuilder<96> Text;
	PressureHud::AppendCutChip(Text, Loss);
	Paint.TextIn(Text.ToView(), { Chip.X + 28.f, Chip.Y, Chip.W - 32.f, Chip.H }, 9.5f, ChipRedText, true);
}

void DrawBattleClock(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	const UPressureView* View = UPressureView::Get(Context.State);
	if (!View || !View->Release().bKnown)
		return;
	const JevIntent::FReleaseView& Release = View->Release();
	const FRect Rect = BattleClockRect(Layout);
	TStringBuilder<24> Name;
	Name << TEXT("JEV ");
	JevIntent::AppendReleaseName(Name, Release.Current);
	TStringBuilder<16> Time;
	JevIntent::AppendElapsed(Time, JevIntent::MatchSeconds(Release, Context.State->GetServerWorldTimeSeconds()));
	const float TimeWidth = Paint.TextIn(Time.ToView(), Rect, 13.f, ClockTime, true, EAlign::Right);
	Paint.TextIn(Name.ToView(), { Rect.X, Rect.Y, Rect.W - TimeWidth - 8.f, Rect.H }, 9.f, Palette::Muted);
}

EHUDAction HitTestPressure(const FContext& Context, const FLayout& Layout, const FVector2D& VirtualPoint)
{
	if (!Context.Controller || Context.Controller->GetUIScreen() != ECommandScreen::Game || !Context.State)
		return EHUDAction::None;
	if (CutChipRect(Layout, ReadCutLoss(Context)).Contains(VirtualPoint))
		return EHUDAction::JevCutChip;
	FJevIntentModel Local;
	if (!Context.JevIntent)
		BuildJevIntentModel(Context, Local);
	const FJevIntentModel& Model = Context.JevIntent ? *Context.JevIntent : Local;
	const FRect Bar = JevTimelineRect(Context, Layout, Model);
	if (Bar.W <= 0.f)
		return EHUDAction::None;
	for (int32 Index = 0; Index < FMath::Min(Model.Timeline.Num(), JevIntent::TimelineEntries); ++Index)
		if (JevTimelineCell(Bar, Index).Contains(VirtualPoint))
			return static_cast<EHUDAction>(static_cast<int32>(EHUDAction::JevTimelineCell0) + Index);
	return EHUDAction::None;
}

bool PressureFocusTarget(const FContext& Context, EHUDAction Action, FVector& World)
{
	if (!Context.State)
		return false;
	if (Action == EHUDAction::JevCutChip)
	{
		TArray<int32> Cut;
		ReadCutLoss(Context, &Cut);
		UPressureView* View = UPressureView::Get(Context.State);
		const int32 Region = View ? View->AdvanceCutFocus(Cut) : INDEX_NONE;
		if (Region == INDEX_NONE)
			return false;
		World = Context.State->GetRegionAnchor(Region);
		return true;
	}
	const int32 Cell = static_cast<int32>(Action) - static_cast<int32>(EHUDAction::JevTimelineCell0);
	if (Cell < 0 || Cell >= JevIntent::TimelineEntries)
		return false;
	FJevIntentModel Local;
	if (!Context.JevIntent)
		BuildJevIntentModel(Context, Local);
	const FJevIntentModel& Model = Context.JevIntent ? *Context.JevIntent : Local;
	if (Cell >= Model.Timeline.Num())
		return false;
	const JevIntent::FTimelineEntry& Entry = Model.Timeline[Cell];
	if (Entry.Kind == JevIntent::EEntryKind::Release)
	{
		// A release has no place of its own: it focuses JEV's main.
		const AHeadquarters* Main = Context.State->EnemyHeadquarters.Get();
		if (!IsValid(Main))
			return false;
		World = Main->GetActorLocation();
		return true;
	}
	if (Entry.Target == INDEX_NONE)
		return false;
	World = Context.State->GetRegionAnchor(Entry.Target);
	return true;
}

void DrawStunChip(const FPainter& Paint, const FRect& Plate, const PressureHud::FStunChip& Stun)
{
	if (!Stun.bShown)
		return;
	// Beside the plate, level with its top: above it sits the producer's force badge.
	FRect Chip{ Plate.Right() + 4.f, Plate.Y, StunChipWidth, StunChipHeight };
	if (!OverlayFits(Paint, Chip))
		Chip.X = Plate.X - 4.f - StunChipWidth;
	if (!OverlayFits(Paint, Chip))
		return;
	Paint.Fill(Chip, StunBack);
	Paint.Outline(Chip, StunYellow);
	DrawBolt(Paint, Chip.X + 5.f, Chip.Y + 3.f, StunYellow);
	TStringBuilder<24> Text;
	PressureHud::AppendStunChip(Text, Stun.Remaining);
	Paint.TextIn(Text.ToView(), { Chip.X + 17.f, Chip.Y, Chip.W - 19.f, Chip.H - 3.f }, 9.f, StunYellow, true);
	Paint.Bar({ Chip.X + 2.f, Chip.Bottom() - 4.f, Chip.W - 4.f, 2.f }, Stun.Drain, StunYellow);
}

PressureHud::FStunChip BuildingStun(const FContext& Context, const ACommandBuilding& Building)
{
	if (!Context.State)
		return {};
	const UPressureView* View = UPressureView::Get(Context.State);
	return PressureHud::StunChip(Context.State->GetServerWorldTimeSeconds(), Building.StunEndServerTime,
		View ? View->Stuns().Peak(Building.GetUniqueID()) : 0.f);
}

void DepositLabelRects(const FPainter& Paint, const FContext& Context, TArray<FRect, TInlineAllocator<16>>& Out)
{
	Out.Reset();
	if (!Context.State)
		return;
	const float Line = Paint.LineHeight(10.f, true);
	for (const ADepositSite* Deposit : Context.State->Deposits)
	{
		FVector2D Screen;
		if (!IsValid(Deposit) || !ProjectOverlay(Paint, Context, Deposit->GetActorLocation() + FVector(0.f, 0.f, 100.f), Screen))
			continue;
		// The plate SectorOverlays fills for this deposit, and the offline rig's glyph hanging off its left edge.
		FRect Back{ Screen.X - 74.f, Screen.Y, 148.f, Line + 16.f };
		if (!OverlayFits(Paint, Back))
			continue;
		if (MapView::IsRigOffline(*Context.State, *Deposit))
		{
			Back.X -= OfflineGlyphReach;
			Back.W += OfflineGlyphReach;
		}
		Out.Add(Back);
	}
}

FRect PlaceClearOf(FRect Rect, TConstArrayView<FRect> Obstacles, TFunctionRef<bool(const FRect&)> Fits)
{
	const auto Covers = [&Obstacles](const FRect& Candidate) {
		return Obstacles.ContainsByPredicate([&Candidate](const FRect& Obstacle) { return Candidate.Intersects(Obstacle); });
	};
	const auto Usable = [&](const FRect& Candidate) { return !Covers(Candidate) && Fits(Candidate); };
	if (!Covers(Rect))
		return Rect;
	// Straight up, over everything it covered.
	FRect Above = Rect;
	for (int32 Pass = 0; Pass < Obstacles.Num(); ++Pass)
	{
		const FRect* Hit = Obstacles.FindByPredicate([&Above](const FRect& Obstacle) { return Above.Intersects(Obstacle); });
		if (!Hit)
			break;
		Above.Y = Hit->Y - Above.H - SeparationGap;
	}
	if (Usable(Above))
		return Above;
	// Where up is taken (a panel, the screen edge): beside the first obstacle it covered, right then left.
	if (const FRect* Hit = Obstacles.FindByPredicate([&Rect](const FRect& Obstacle) { return Rect.Intersects(Obstacle); }))
		for (const float X : { Hit->Right() + SeparationGap, Hit->X - SeparationGap - Rect.W })
			if (const FRect Beside{ X, Rect.Y, Rect.W, Rect.H }; Usable(Beside))
				return Beside;
	// Nowhere clear: it stays where its region's stack puts it.
	return Rect;
}
}
