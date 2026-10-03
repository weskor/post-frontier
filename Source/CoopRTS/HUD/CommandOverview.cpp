#include "HUDPanels.h"

namespace CommandHUDPanels
{
static void DrawNextStep(const FPainter& Paint, const FContext& Context, const FForces& Forces, const FRect& Next)
{
	const float Y = Next.Y + LabelHeight;
	// Next step is derived from the commander's real buildings, forces and sectors.
	const TCHAR* First;
	const TCHAR* Second;
	if (Context.bTerminal)
	{
		First = TEXT("Match over.");
		Second = TEXT("Press Enter for a fresh match.");
	}
	else if (Forces.Barracks == 0)
	{
		First = TEXT("Build a Barracks on");
		Second = TEXT("green preview cells.");
	}
	else if (Forces.CompletedBarracks == 0)
	{
		First = TEXT("Barracks under construction.");
		Second = TEXT("Plan its force type and goal.");
	}
	else if (Forces.ConfiguredForces == 0)
	{
		First = TEXT("Select your Barracks, choose");
		Second = TEXT("a permanent type and Start.");
	}
	else if (Forces.FreeDeposits > 0)
	{
		First = TEXT("Build an Extractor on a");
		Second = TEXT("free deposit for private income.");
	}
	else if (Forces.ControlledRegions <= 1)
	{
		First = TEXT("Choose Expand and pick");
		Second = TEXT("a region to capture it.");
	}
	else if (Forces.Workshops == 0)
	{
		First = TEXT("A Workshop unlocks one");
		Second = TEXT("paid specialization.");
	}
	else
	{
		First = TEXT("Set Barracks goals and push");
		Second = TEXT("toward the enemy HQ.");
	}
	ColumnLabel(Paint, Next, TEXT("NEXT STEP"));
	Paint.Text(First, Next.X, Y, 10.5f, Palette::Friendly, true, EAlign::Left, Next.W);
	Paint.Text(Second, Next.X, Y + 18.f, 10.5f, Palette::Friendly, true, EAlign::Left, Next.W);
	Paint.Text(TEXT("Click an owned building."), Next.X, Y + 46.f, 8.5f, Palette::Faint,
		false, EAlign::Left, Next.W);
}

static void DrawBaseSummary(const FPainter& Paint, const FForces& Forces, const FRect& Base)
{
	ColumnLabel(Paint, Base, TEXT("YOUR BASE"));
	TStringBuilder<64> Line;
	const float Y = Base.Y + LabelHeight;
	Line.Appendf(TEXT("Barracks %d  \u00B7  %d producing"), Forces.Barracks, Forces.Producing);
	Paint.Text(Line.ToView(), Base.X, Y, 10.f, Palette::Text, false, EAlign::Left, Base.W);
	Line.Reset();
	Line.Appendf(TEXT("Workshops %d  \u00B7  Extractors %d"), Forces.Workshops, Forces.Extractors);
	Paint.Text(Line.ToView(), Base.X, Y + 19.f, 10.f, Palette::Text, false, EAlign::Left, Base.W);
	Line.Reset();
	Line.Appendf(TEXT("%d under construction"), Forces.Constructing);
	Paint.Text(Line.ToView(), Base.X, Y + 38.f, 10.f, Forces.Constructing > 0 ? Palette::Warn : Palette::Muted, false, EAlign::Left, Base.W);
	Line.Reset();
	Line.Appendf(TEXT("Configured forces %d"), Forces.ConfiguredForces);
	Paint.Text(Line.ToView(), Base.X, Y + 57.f, 10.f, Palette::Text, false, EAlign::Left, Base.W);
}

static void DrawOverviewControls(const FPainter& Paint, const FRect& Controls)
{
	const float Y = Controls.Y + LabelHeight;
	ColumnLabel(Paint, Controls, TEXT("CONTROLS"));
	const float Half = Controls.W * .5f;
	Paint.DrawKey(Controls.X, Y, TEXT("LMB"), TEXT("Select"));
	Paint.DrawKey(Controls.X + Half, Y, TEXT("F"), TEXT("Selection"));
	Paint.DrawKey(Controls.X, Y + 22.f, TEXT("1-4 / 5"), TEXT("Force (5 solo)"));
	Paint.DrawKey(Controls.X, Y + 44.f, TEXT("Shift"), TEXT("Toggle"));
	Paint.DrawKey(Controls.X + Half, Y + 44.f, TEXT("G"), TEXT("Ping"));
	Paint.DrawKey(Controls.X + Half, Y + 65.f, TEXT("Space"), TEXT("Alerts"));
	Paint.Text(TEXT("Drag: box badges"), Controls.X, Y + 65.f, 9.f, Palette::Muted, false, EAlign::Left, Controls.W);
	Paint.DrawKey(Controls.X + Half, Y + 85.f, TEXT("F4"), TEXT("Deck"));
	Paint.Text(TEXT("Double-tap number: centre"), Controls.X, Y + 85.f, 8.f, Palette::Faint, false, EAlign::Left, Controls.W);
}

void DrawOverview(const FPainter& Paint, const FContext& Context, const FForces& Forces, const FRect& Inspector)
{
	const int32 Owner = Context.Wallet ? Context.Wallet->CommanderIndex : -1;
	TStringBuilder<48> Subtitle;
	if (Owner >= 0)
		Subtitle.Appendf(TEXT("C%d  \u00B7  nothing selected"), Owner + 1);
	else
		Subtitle << TEXT("commander slot syncing");
	DrawInspectorHeader(Paint, Inspector, Owner >= 0 ? AArmyUnit::GetCommanderColor(Owner) : Palette::Muted,
		TEXT("COMMAND OVERVIEW"), Subtitle.ToView(), Owner, 0, 0, FStringView(), Palette::Muted);
	const FRect Base = Column(Inspector, 0, 3);
	const FRect Next = Column(Inspector, 1, 3);
	const FRect Controls = Column(Inspector, 2, 3);
	DrawBaseSummary(Paint, Forces, Base);
	DrawNextStep(Paint, Context, Forces, Next);
	DrawOverviewControls(Paint, Controls);
}

}
