#include "HUDPanels.h"
#include "CommandGameState.h"
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

static void DrawHQBar(const FPainter& Paint, const FRect& Rect, const TCHAR* Label, const AHeadquarters* HQ, const FLinearColor& Color)
{
	const int32 Health = IsValid(HQ) ? HQ->Health : 0;
	const int32 Max = IsValid(HQ) ? FMath::Max(1, HQ->MaxHealth()) : 1;
	Paint.Bar(Rect, static_cast<float>(Health) / Max, Color.CopyWithNewOpacity(.62f));
	Paint.Outline(Rect, Color.CopyWithNewOpacity(.55f));
	Paint.TextIn(Label, Rect, 9.f, Palette::Text, true, EAlign::Left, 7.f);
	TStringBuilder<32> Value;
	Value.Appendf(TEXT("%d / %d"), Health, Max);
	Paint.TextIn(Value.ToView(), Rect, 9.f, Palette::Text, true, EAlign::Right, 7.f);
}

static void DrawLatestObjective(const FPainter& Paint, const FContext& Context, const FRect& Strip)
{
	const FRect Contributors = ObjectiveContributors(Strip);
	const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(Context.State);
	if (!Announcer || Announcer->GetEvents().IsEmpty())
	{
		Paint.TextIn(TEXT("No objective alerts yet"), Contributors, 9.f, Palette::Muted);
		return;
	}
	const FObjectiveEvent& Latest = Announcer->GetEvents().Last();
	const AnnouncerPolicy::FDefinition* Definition = AnnouncerPolicy::Find(Latest.Id);
	TStringBuilder<256> Title;
	Title << (Definition ? Definition->Text : TEXT("Objective update"));
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
	DrawHQBar(Paint, { Strip.X + Pad, HQY, HQWidth, 20.f }, TEXT("Hardline"),
		Context.State->FriendlyHeadquarters, Palette::Friendly);
	DrawHQBar(Paint, { Strip.X + Pad + HQWidth + Gap, HQY, HQWidth, 20.f }, TEXT("The Lattice"),
		Context.State->EnemyHeadquarters, Palette::Enemy);
	TStringBuilder<32> Regions;
	Regions.Appendf(TEXT("Regions %d / %d"), FriendlyRegions, Context.State->Regions.Num());
	Paint.Text(Regions.ToView(), Strip.X + Pad, HQY + 23.f, 9.f, Palette::Friendly);
	Regions.Reset();
	Regions.Appendf(TEXT("Regions %d / %d"), EnemyRegions, Context.State->Regions.Num());
	Paint.Text(Regions.ToView(), Strip.X + Pad + HQWidth + Gap, HQY + 23.f, 9.f, Palette::Enemy);
	DrawLatestObjective(Paint, Context, Strip);
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
		TStringBuilder<128> Economy;
		Economy.Appendf(TEXT("C%d   %d Power  +%d/s   Forces %d   Regions %d/%d"),
			Context.Wallet->CommanderIndex + 1, Context.Balance, Context.Wallet->GetIncomePerSecond(),
			Forces.ConfiguredForces, Forces.ControlledRegions, Context.State->Regions.Num());
		Paint.TextIn(Economy.ToView(), { Top.X + Pad, Top.Y, Top.W - 2.f * Pad - 230.f, Top.H },
			10.f, Palette::Gold, true);
	}
	else
		Paint.TextIn(TEXT("Syncing commander and wallet..."), Top, 10.f, Palette::Warn, false, EAlign::Left, Pad);
	Paint.TextIn(TEXT("Space: alerts   F: selection"), { Top.Right() - Pad - 225.f, Top.Y, 225.f, Top.H },
		9.f, Palette::Muted, false, EAlign::Right);
}

}
