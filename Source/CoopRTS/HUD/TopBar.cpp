#include "HUDPanels.h"
#include "PressurePanels.h"
#include "CommandGameState.h"
#include "GuardedHqPanels.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "ObjectiveAnnouncer.h"
#include "Rules/AnnouncerPolicy.h"

namespace CommandHUDPanels
{
FRect ObjectiveContributors(const FRect& Strip)
{
	const float HQWidth = FMath::Min(225.f, Strip.W * .24f);
	const float X = Strip.X + Pad + 2.f * (HQWidth + Gap);
	return { X, Strip.Y + 27.f, Strip.Right() - Pad - X, Strip.H - 27.f - Pad };
}

int32 ObjectiveForceColumns(const FRect& Strip)
{
	return FMath::Max(1, FMath::FloorToInt((ObjectiveContributors(Strip).W + RowGap) / (90.f + RowGap)));
}

static void DrawLatestObjective(const FPainter& Paint, const FContext& Context, const FRect& Strip)
{
	const FRect Contributors = ObjectiveContributors(Strip);
	const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(Context.State);
	if (!Announcer || Announcer->GetEvents().IsEmpty())
	{
		TStringBuilder<192> Standing;
		if (Context.State)
			AppendStandingSentence(Standing, *Context.State);
		Paint.TextIn(Standing.ToView(), Contributors, 9.f, Palette::Muted);
		return;
	}
	const FObjectiveEvent& Latest = Announcer->GetEvents().Last();
	const AnnouncerPolicy::FDefinition* Definition = AnnouncerPolicy::Find(Latest.Id);
	TStringBuilder<256> Title;
	Title << (Definition ? Definition->Text : TEXT("Objective update"));
	// Node loss and revival carry their number in the event's tier: nodes left, restored percent.
	const FString Id = Latest.Id.ToString();
	if (Id.EndsWith(TEXT("_node_lost")) || Id.EndsWith(TEXT("_hq_online")))
	{
		Title.RemoveSuffix(1); // The feed's full stop.
		if (Id.EndsWith(TEXT("_node_lost")))
			Title.Appendf(TEXT(" \u00B7 %d left"), Latest.DamageTier);
		else
			Title.Appendf(TEXT(" at %d%% HP"), Latest.DamageTier);
	}
	if (!Latest.RegionName.IsEmpty())
		Title << TEXT("  |  ") << ObjectiveRegionName(Latest.RegionName);
	Paint.Text(Title.ToView(), Contributors.X, Strip.Y + 5.f, 9.f, Palette::Text, true, EAlign::Left, Contributors.W);
	const int32 Columns = ObjectiveForceColumns(Strip);
	const int32 Capacity = Columns * MaxObjectiveForceRows;
	const int32 Count = FMath::Min(Latest.Forces.Num(), Capacity);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FRect Badge{ Contributors.X + (Index % Columns) * (90.f + RowGap),
			Contributors.Y + (Index / Columns) * (21.f + RowGap), 90.f, 21.f };
		if (Latest.Forces.Num() > Capacity && Index == Count - 1)
		{
			TStringBuilder<32> More;
			More.Appendf(TEXT("+%d forces"), Latest.Forces.Num() - Count + 1);
			Paint.TextIn(More.ToView(), Badge, 9.f, Palette::Muted);
		}
		else
			DrawObjectiveForceBadge(Paint, Context, Latest.Forces[Index], Badge);
	}
}

static void DrawObjectiveStrip(const FPainter& Paint, const FContext& Context, const FRect& Strip)
{
	Paint.Panel(Strip);
	if (!Context.State)
	{
		Paint.TextIn(TEXT("Syncing objectives..."), Strip, 10.f, Palette::Warn, false, EAlign::Left, Pad);
		return;
	}
	int32 FriendlyRegions = 0;
	int32 EnemyRegions = 0;
	for (const AMapRegion* Region : Context.State->Regions)
	{
		if (!IsValid(Region))
			continue;
		const int32 Team = Context.State->GetRegionController(Region->RegionIndex);
		FriendlyRegions += Team == 0 ? 1 : 0;
		EnemyRegions += Team == 5 ? 1 : 0;
	}
	const float HQWidth = FMath::Min(225.f, Strip.W * .24f);
	const float HQY = Strip.Y + Pad;
	const FRect Hardline{ Strip.X + Pad, HQY, HQWidth, 20.f };
	const FRect Lattice{ Strip.X + Pad + HQWidth + Gap, HQY, HQWidth, 20.f };
	DrawHqBar(Paint, Hardline, Context.State->FriendlyHeadquarters, Palette::Friendly);
	DrawHqBar(Paint, Lattice, Context.State->EnemyHeadquarters, Palette::Enemy);
	// The row under each bar: the hold's state line while the HQ is offline, otherwise its regions and nodes.
	const auto DrawRow = [&](const FRect& Bar, int32 Regions, const AHeadquarters* HQ, const FLinearColor& Color) {
		const FRect Row{ Bar.X, Bar.Bottom() + 3.f, Bar.W, 14.f };
		if (DrawHoldLine(Paint, Row, HQ))
			return;
		TStringBuilder<32> Text;
		Text.Appendf(TEXT("Regions %d / %d"), Regions, Context.State->Regions.Num());
		Paint.Text(Text.ToView(), Row.X, Row.Y, 9.f, Color);
		DrawNodePips(Paint, Row, HQ);
	};
	DrawRow(Hardline, FriendlyRegions, Context.State->FriendlyHeadquarters, Palette::Friendly);
	DrawRow(Lattice, EnemyRegions, Context.State->EnemyHeadquarters, Palette::Enemy);
	DrawLatestObjective(Paint, Context, Strip);
}

// The economy line: Power stays gold; Data is white with a chip glyph and the word, since STYLE.md has no Data colour.
// Rates are exact shares of the team pool; only this text rounds them, to a tenth.
static void DrawEconomyLine(const FPainter& Paint, const FContext& Context, const FForces& Forces, const FRect& Top)
{
	const float Limit = Top.X + Pad + EconomyTextWidth;
	float X = Top.X + Pad;
	const auto Put = [&](FStringView Text, const FLinearColor& Color) {
		if (X < Limit)
			X += Paint.TextIn(Text, { X, Top.Y, Limit - X, Top.H }, 10.f, Color, true) + 14.f;
	};
	TStringBuilder<32> Commander;
	Commander.Appendf(TEXT("C%d"), Context.Wallet->CommanderIndex + 1);
	Put(Commander.ToView(), Palette::Gold);
	TStringBuilder<48> Power;
	Power.Appendf(TEXT("%d Power +"), Context.Balance);
	PressureHud::AppendRate(Power, Context.State->GetPowerRate(Context.Wallet));
	Power << TEXT("/s");
	Put(Power.ToView(), Palette::Gold);
	TStringBuilder<48> Data;
	Data.Appendf(TEXT("%d Data +"), Context.DataBalance);
	PressureHud::AppendRate(Data, Context.State->GetDataRate(Context.Wallet));
	Data << TEXT("/s");
	DrawDataGlyph(Paint, X, Top.Y + (Top.H - 9.f) * .5f, Palette::Text);
	X += 14.f;
	Put(Data.ToView(), Palette::Text);
	TStringBuilder<32> ForceCount;
	ForceCount.Appendf(TEXT("Forces %d"), Forces.ConfiguredForces);
	Put(ForceCount.ToView(), Palette::Gold);
	TStringBuilder<32> Regions;
	Regions.Appendf(TEXT("Regions %d/%d"), Forces.ControlledRegions, Context.State->Regions.Num());
	Put(Regions.ToView(), Palette::Gold);
}

void DrawTopBar(const FPainter& Paint, const FContext& Context, const FForces& Forces, const FLayout& Layout)
{
	const FRect& Top = Layout.Top;
	Paint.Panel(Top);
	DrawObjectiveStrip(Paint, Context, Layout.Objectives);
	if (!Context.State)
	{
		Paint.TextIn(TEXT("Syncing commander, wallet and territory..."), Top, 10.f, Palette::Warn, false, EAlign::Left, Pad);
		return;
	}
	if (Context.Wallet && Context.Wallet->CommanderIndex >= 0)
	{
		DrawEconomyLine(Paint, Context, Forces, Top);
		DrawCutChip(Paint, Layout, ReadCutLoss(Context));
	}
	else
		Paint.TextIn(TEXT("Syncing commander and wallet..."), Top, 10.f, Palette::Warn, false, EAlign::Left, Pad);
	DrawBattleClock(Paint, Context, Layout);
	Paint.TextIn(TEXT("Space: alerts   F: selection"), { Top.Right() - Pad - TopHintWidth, Top.Y, TopHintWidth, Top.H },
		9.f, Palette::Muted, false, EAlign::Right);
}

}
