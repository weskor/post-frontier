#include "HUDPanels.h"
#include "Rules/JevThreatPolicy.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "PressurePanels.h"
#include "PressureView.h"

namespace CommandHUDPanels
{
namespace
{
constexpr float TimelineHeight = 40.f;
constexpr float TimelineMaxWidth = 700.f;
constexpr float TimelineMinWidth = 240.f;
constexpr float TimelineHeaderWidth = 84.f;
constexpr float MemoRowHeight = 38.f;
constexpr float MemoMaxWidth = 430.f;
constexpr float MemoTextInset = 40.f;
constexpr float MemoLineStep = 12.5f;

// Machine skin (Art/UI/STYLE.md): pearl text, cyan trim, the one red lens. The Machine's
// information is never drawn in the offline panels' blue-grey.
const FLinearColor Pearl(.918f, .945f, .965f);
const FLinearColor Cyan(.42f, .90f, 1.f);
const FLinearColor Lens(1.f, .23f, .19f);
const FLinearColor MachinePanel(.024f, .071f, .102f, .86f);

FLinearColor Faded(const FLinearColor& Color, float Alpha, float Scale = 1.f)
{
	return Color.CopyWithNewOpacity(Color.A * Scale * Alpha);
}

const TCHAR* VerbName(JevPlanner::EVerb Verb)
{
	return Verb == JevPlanner::EVerb::Attack ? TEXT("Attack") : Verb == JevPlanner::EVerb::Retreat ? TEXT("Retreat")
																								   : TEXT("Move & Hold");
}

FLinearColor VerbColor(JevPlanner::EVerb Verb, bool bEscalated)
{
	return bEscalated ? Palette::Warn : Verb == JevPlanner::EVerb::Attack ? Lens
		: Verb == JevPlanner::EVerb::Retreat                              ? Palette::Gold
																		  : Cyan;
}

// Right-aligned in Rect; returns the width drawn. Muted countdowns are JEV's first plans while planning: nothing runs yet.
float DrawCountdown(const FPainter& Paint, const FRect& Rect, float Seconds, bool bMuted)
{
	TStringBuilder<16> Count;
	if (Seconds > 0.f)
		JevIntent::AppendCountdown(Count, Seconds);
	else
		Count << TEXT("ARRIVED");
	return Paint.Text(Count.ToView(), Rect.Right(), Rect.Y, Seconds > 0.f ? 10.5f : 8.5f,
		Seconds > 0.f ? (bMuted ? Palette::Muted : Pearl) : Palette::Faint, true, EAlign::Right, Rect.W);
}

// Two lines at most: the first breaks at the last whole word that fits, the second clips.
void DrawWrapped(const FPainter& Paint, FStringView Text, const FRect& Rect, float Size, const FLinearColor& Color)
{
	if (!Paint.Measure.IsValid() || Paint.TextWidth(Text, Size) <= Rect.W)
	{
		Paint.Text(Text, Rect.X, Rect.Y, Size, Color, false, EAlign::Left, Rect.W);
		return;
	}
	const int32 Fit = Paint.Measure->FindLastWholeCharacterIndexBeforeOffset(
						  Text, Paint.Info(Size, false), FMath::FloorToInt(Rect.W * Paint.Scale))
		+ 1;
	int32 Break = FMath::Clamp(Fit, 0, Text.Len());
	if (Break < Text.Len() && Text[Break] != TEXT(' '))
		for (int32 Index = Break; Index > 0; --Index)
			if (Text[Index - 1] == TEXT(' '))
			{
				Break = Index - 1;
				break;
			}
	FStringView Rest = Text.RightChop(Break);
	while (!Rest.IsEmpty() && Rest[0] == TEXT(' '))
		Rest.RightChopInline(1);
	FStringView First = Text.Left(Break);
	while (!First.IsEmpty() && First[First.Len() - 1] == TEXT(' '))
		First.LeftChopInline(1);
	Paint.Text(First, Rect.X, Rect.Y, Size, Color, false, EAlign::Left, Rect.W);
	Paint.Text(Rest, Rect.X, Rect.Y + MemoLineStep, Size, Color, false, EAlign::Left, Rect.W);
}

void DrawTimelineCell(const FPainter& Paint, const FContext& Context, const FRect& Cell, const JevIntent::FTimelineEntry& Entry)
{
	const bool bMuted = Context.bPlanning;
	const float X = Cell.X + 7.f;
	const float CountWidth = DrawCountdown(Paint, { Cell.X, Cell.Y + 5.f, Cell.W - 7.f, 14.f }, Entry.Seconds, bMuted);
	Paint.Text(JevIntentView::RegionName(*Context.State, Entry.Target), X, Cell.Y + 4.5f, 9.5f, bMuted ? Palette::Muted : Pearl, true,
		EAlign::Left, Cell.W - 21.f - CountWidth);
<<<<<<< HEAD
	const float TagWidth = Paint.Text(JevVerbTag(Entry.Verb, Entry.bEscalated), X, Cell.Y + 22.f, 8.f,
		Faded(VerbColor(Entry.Verb, Entry.bEscalated), 1.f, bMuted ? .6f : 1.f), true, EAlign::Left, Cell.W - 14.f);
=======
	const TCHAR* Tag = Entry.bCut ? JevThreat::CellTag : JevVerbTag(Entry.Verb, Entry.bEscalated);
	const float TagWidth = Paint.Text(Tag, X, Cell.Y + 22.f, 8.f,
		VerbColor(Entry.Verb, Entry.bEscalated), true, EAlign::Left, Cell.W - 14.f);
>>>>>>> 5dd0a0d (Split-Brain Cut review: cap semantics, tagged cells and badges, solo voice line, true-clock capture, one measured pair)
	TStringBuilder<24> Size;
	Size.Appendf(TEXT("~%d units"), Entry.SizeBand);
	const float SizeX = X + TagWidth + 6.f;
	Paint.Text(Size.ToView(), SizeX, Cell.Y + 22.f, 8.5f, Palette::Muted, false, EAlign::Left, Cell.Right() - 6.f - SizeX);
}

// The next release, pinned left with cyan trim: its name, the countdown and a tag from what it adds.
void DrawReleaseCell(const FPainter& Paint, const FRect& Cell, const JevIntent::FTimelineEntry& Entry)
{
	const FRect Trim{ Cell.X + 1.f, Cell.Y + 3.f, Cell.W - 2.f, Cell.H - 6.f };
	Paint.Fill(Trim, Faded(Cyan, 1.f, .12f));
	Paint.Outline(Trim, Cyan);
	const float X = Cell.X + 8.f;
	TStringBuilder<16> Count;
	JevIntent::AppendCountdown(Count, Entry.Seconds);
	const float CountWidth = Paint.Text(Count.ToView(), Cell.Right() - 8.f, Cell.Y + 6.f, 10.5f, Pearl, true, EAlign::Right, Cell.W - 16.f);
	TStringBuilder<32> Name;
	JevIntent::AppendReleaseName(Name, Entry.Release);
	FString Title(Name.ToView());
	if (JevRelease::VersionOf(Entry.Release) == JevRelease::EVersion::Overrun)
		Title.ToUpperInline();
	Title += TEXT(" RELEASE");
	Paint.Text(Title, X, Cell.Y + 5.5f, 9.5f, Pearl, true, EAlign::Left, Cell.W - 24.f - CountWidth);
	TStringBuilder<48> Tag;
	JevIntent::AppendReleaseTag(Tag, Entry.Release, Entry.CounterArmor);
	Paint.Text(Tag.ToView(), X, Cell.Y + 22.f, 8.f, Cyan, true, EAlign::Left, Cell.W - 16.f);
}

void DrawEmptyTimeline(const FPainter& Paint, const FContext& Context, const FRect& Bar)
{
	const UPressureView* View = UPressureView::Get(Context.State);
	TStringBuilder<96> Text;
	JevIntent::AppendEmptyTimeline(Text, View ? View->Release() : JevIntent::FReleaseView(), JevIntentView::Now(*Context.State));
	const float X = Bar.X + TimelineHeaderWidth + 8.f;
	Paint.TextIn(Text.ToView(), { X, Bar.Y, Bar.Right() - X - 8.f, Bar.H }, 10.f, Faded(Pearl, 1.f, .8f));
}

// Under the title: the plan count, or while planning that nothing of JEV's runs yet; two short lines fit the 84 px header.
void DrawTimelineCount(const FPainter& Paint, const FContext& Context, const FRect& Bar, const JevIntent::FTimeline& Timeline)
{
	if (Context.bPlanning)
	{
		Paint.Text(TEXT("frozen:"), Bar.X + 17.f, Bar.Y + 15.f, 8.f, Palette::Muted);
		Paint.Text(TEXT("starts 0:00"), Bar.X + 17.f, Bar.Y + 26.f, 8.f, Palette::Muted);
		return;
	}
	const int32 Total = Timeline.Num();
	const int32 Plans = Total - (Total > 0 && Timeline[0].Kind == JevIntent::EEntryKind::Release ? 1 : 0);
	TStringBuilder<24> Count;
	if (Total > JevIntent::TimelineEntries)
		Count.Appendf(TEXT("+%d more"), Total - JevIntent::TimelineEntries);
	else
		Count.Appendf(TEXT("%d active"), Plans);
	Paint.Text(Count.ToView(), Bar.X + 17.f, Bar.Y + 21.f, 9.f, Total > JevIntent::TimelineEntries ? Palette::Warn : Palette::Muted);
}

void DrawTimeline(const FPainter& Paint, const FContext& Context, const FRect& Bar, const FJevIntentModel& Model)
{
	Paint.Fill(Bar, MachinePanel);
	Paint.Outline(Bar, Faded(Cyan, 1.f, .55f));
	Paint.Fill({ Bar.X + 7.f, Bar.Y + 7.f, 5.f, 5.f }, Lens);
	Paint.Text(TEXT("JEV PLANS"), Bar.X + 17.f, Bar.Y + 4.f, 8.5f, Cyan, true);
	const int32 Total = Model.Timeline.Num();
	DrawTimelineCount(Paint, Context, Bar, Model.Timeline);
	if (Total == 0)
		DrawEmptyTimeline(Paint, Context, Bar);
	for (int32 Index = 0; Index < FMath::Min(Total, JevIntent::TimelineEntries); ++Index)
	{
		const FRect Cell = JevTimelineCell(Bar, Index);
		const JevIntent::FTimelineEntry& Entry = Model.Timeline[Index];
		Paint.Fill({ Cell.X, Bar.Y + 6.f, 1.f, Bar.H - 12.f }, Faded(Cyan, 1.f, .3f));
		if (Entry.Kind == JevIntent::EEntryKind::Release)
			DrawReleaseCell(Paint, Cell, Entry);
		else
			DrawTimelineCell(Paint, Context, Cell, Entry);
	}
}

void DrawMemoRow(const FPainter& Paint, const FJevMemoRow& Row)
{
	Paint.Fill(Row.Rect, Faded(MachinePanel, Row.Alpha));
	Paint.Outline(Row.Rect, Faded(Cyan, Row.Alpha, .5f));
	Paint.Fill({ Row.Rect.X, Row.Rect.Y, 3.f, Row.Rect.H }, Faded(Lens, Row.Alpha));
	Paint.Text(TEXT("JEV"), Row.Rect.X + 9.f, Row.Rect.Y + 6.f, 8.5f, Faded(Cyan, Row.Alpha), true);
	DrawWrapped(Paint, Row.Memo->Text,
		{ Row.Rect.X + MemoTextInset, Row.Rect.Y + 6.f, Row.Rect.W - MemoTextInset - 8.f, 0.f }, 9.5f, Faded(Pearl, Row.Alpha));
}
}

const TCHAR* JevVerbTag(JevPlanner::EVerb Verb, bool bEscalated)
{
	return bEscalated ? TEXT("ESCALATED") : Verb == JevPlanner::EVerb::Attack ? TEXT("ATTACK")
		: Verb == JevPlanner::EVerb::Retreat                                  ? TEXT("RETREAT")
																			  : TEXT("MOVE & HOLD");
}

void BuildJevIntentModel(const FContext& Context, FJevIntentModel& Model)
{
	Model.Plans.Reset();
	Model.Timeline.Reset();
	Model.Badges.Reset();
	Model.Now = 0.f;
	if (!Context.State)
		return;
	JevIntentView::Snapshot(*Context.State, Model.Plans);
	Model.Now = JevIntentView::Now(*Context.State);
	const UPressureView* Pressure = UPressureView::Get(Context.State);
	JevIntent::BuildTimeline(Model.Plans, Pressure ? Pressure->Release() : JevIntent::FReleaseView(), Model.Now, Model.Timeline);
	JevIntent::BuildBadges(Model.Plans, Model.Now, Model.Badges);
}

void ObserveJevIntent(const FContext& Context)
{
	if (!Context.State)
		return;
	if (UJevIntentFeed* Feed = UJevIntentFeed::Get(Context.State))
		Feed->Observe(*Context.State);
	if (UPressureView* Pressure = UPressureView::Get(Context.State))
		Pressure->Observe(*Context.State, Context.Wallet);
}

FRect JevTimelineRect(const FContext& Context, const FLayout& Layout, const FJevIntentModel&)
{
	// The bar keeps its height with no plans, so the memo feed beneath it never jumps.
	const float Width = FMath::Min(TimelineMaxWidth, Layout.Alerts.X - Gap - Margin);
	if (!Context.State || Width < TimelineMinWidth)
		return {};
	return { Margin, Layout.Objectives.Bottom() + Gap, Width, TimelineHeight };
}

FRect JevTimelineCell(const FRect& Bar, int32 Index)
{
	const float CellWidth = (Bar.W - TimelineHeaderWidth - 4.f) / JevIntent::TimelineEntries;
	return { Bar.X + TimelineHeaderWidth + Index * CellWidth, Bar.Y, CellWidth, Bar.H };
}

int32 JevMemoRows(const FContext& Context, const FLayout& Layout, const FJevIntentModel& Model,
	FJevMemoRow (&Rows)[JevIntent::MemoVisible])
{
	const UJevIntentFeed* Feed = Context.State ? UJevIntentFeed::Get(Context.State) : nullptr;
	if (!Feed)
		return 0;
	JevIntent::FVisibleMemos Visible;
	Feed->GetFeed().Visible(Model.Now, Visible);
	const FRect Timeline = JevTimelineRect(Context, Layout, Model);
	const float Width = FMath::Min(MemoMaxWidth, Layout.Alerts.X - Gap - Margin);
	float Y = (Timeline.W > 0.f ? Timeline.Bottom() : Layout.Objectives.Bottom()) + Gap;
	int32 Count = 0;
	for (const JevIntent::FVisibleMemo& Entry : Visible)
	{
		const FRect Rect{ Margin, Y, Width, MemoRowHeight };
		if (Width < TimelineMinWidth || Rect.Bottom() > FMath::Min(Layout.Feedback.Y, Layout.FortifyDock.Y) - Gap)
			break;
		Rows[Count++] = { Rect, Entry.Memo, Entry.Alpha };
		Y = Rect.Bottom() + RowGap;
	}
	return Count;
}

void ForEachJevIntentPanel(const FContext& Context, const FLayout& Layout, TFunctionRef<void(const FRect&)> Visit)
{
	FJevIntentModel Local;
	if (!Context.JevIntent)
		BuildJevIntentModel(Context, Local);
	const FJevIntentModel& Model = Context.JevIntent ? *Context.JevIntent : Local;
	if (const FRect Timeline = JevTimelineRect(Context, Layout, Model); Timeline.W > 0.f)
		Visit(Timeline);
	FJevMemoRow Rows[JevIntent::MemoVisible];
	const int32 Count = JevMemoRows(Context, Layout, Model, Rows);
	for (int32 Index = 0; Index < Count; ++Index)
		Visit(Rows[Index].Rect);
}

void JevBadgeLabel(const FContext& Context, const JevIntent::FRegionBadge& Badge, FStringBuilderBase& Label)
{
	const FString& Region = JevIntentView::RegionName(*Context.State, Badge.Region);
	if (Badge.bEscalated)
		Label.Appendf(TEXT("JEV  Escalated: defending %s"), *Region);
	else
	{
		Label.Appendf(TEXT("JEV  %s  %s  "), Badge.bCut ? JevThreat::Name : VerbName(Badge.Verb), *Region);
		JevIntent::AppendCountdown(Label, Badge.Seconds);
	}
	if (Badge.Plans > 1)
		Label.Appendf(TEXT("  x%d"), Badge.Plans);
}

void DrawJevIntent(const FPainter& Paint, const FContext& Context, const FLayout& Layout, const FJevIntentModel& Model)
{
	if (const FRect Timeline = JevTimelineRect(Context, Layout, Model); Timeline.W > 0.f)
		DrawTimeline(Paint, Context, Timeline, Model);
	FJevMemoRow Rows[JevIntent::MemoVisible];
	const int32 Count = JevMemoRows(Context, Layout, Model, Rows);
	for (int32 Index = Count - 1; Index >= 0; --Index)
		DrawMemoRow(Paint, Rows[Index]);
}

void DrawJevRegionBadges(const FPainter& Paint, const FContext& Context, const FLayout& Layout, const FJevIntentModel& Model)
{
	if (!Context.State || Layout.Scale <= 0.f)
		return;
	const float Line = Paint.LineHeight(10.f, true);
	TArray<FRect, TInlineAllocator<16>> Deposits;
	DepositLabelRects(Paint, Context, Deposits);
	for (const JevIntent::FRegionBadge& Badge : Model.Badges)
	{
		FVector2D Screen;
		if (!ProjectOverlay(Paint, Context, Context.State->GetRegionAnchor(Badge.Region) + FVector(0.f, 0.f, 110.f), Screen))
			continue;
		TStringBuilder<128> Label;
		JevBadgeLabel(Context, Badge, Label);
		const float Width = Paint.TextWidth(Label.ToView(), 10.f, true) + 22.f;
		// Above the region's name label, which hangs from the same projected point; clear of any deposit label, above it
		// when there is room, else beside it.
		const auto Fits = [&](const FRect& Candidate) { return OverlayFits(Paint, Candidate) && OverlayClearsPanels(Context, Layout, Candidate); };
		const FRect Rect = PlaceClearOf({ Screen.X - Width * .5f, Screen.Y - 2.f * Line - 29.f, Width, Line + 8.f }, Deposits, Fits);
		if (!Fits(Rect))
			continue;
		const FLinearColor Accent = Badge.bEscalated ? Palette::Warn : Lens;
		Paint.Fill(Rect, MachinePanel);
		Paint.Outline(Rect, Accent);
		Paint.Fill({ Rect.X + 6.f, Rect.Center().Y - 2.5f, 5.f, 5.f }, Accent);
		Paint.TextIn(Label.ToView(), { Rect.X + 14.f, Rect.Y, Rect.W - 14.f, Rect.H }, 10.f, Pearl, true, EAlign::Left, 4.f);
	}
}
}
