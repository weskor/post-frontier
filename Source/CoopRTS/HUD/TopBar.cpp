#include "HUDPanels.h"
#include "CommandGameState.h"
#include "DepositSite.h"
#include "MapRegion.h"
#include "Headquarters.h"

namespace CommandHUDPanels
{
FForces CountForces(const FContext& Context)
{
	FForces Forces;
	if (!Context.State || !Context.Wallet)
		return Forces;
	for (const ACommandBuilding* Building : Context.State->Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive() || Building->TeamIndex != 0
			|| Building->OwningPlayerState != Context.Wallet)
			continue;
		if (!Building->IsComplete())
			++Forces.Constructing;
		const UBuildingDefinition* Definition = Building->GetDefinition();
		if (Building->IsProducer())
		{
			++Forces.Barracks;
			if (Building->IsComplete())
				++Forces.CompletedBarracks;
			if (Building->IsComplete() && Building->GetProductionState() == EProductionState::Producing)
				++Forces.Producing;
			if (Building->bForceConfigured)
				++Forces.ConfiguredForces;
		}
		else if (Definition && Definition->bOffersResearch)
			++Forces.Workshops;
		else if (Definition && Definition->bRequiresDeposit)
			++Forces.Extractors;
	}
	for (const AMapRegion* Region : Context.State->Regions)
		if (IsValid(Region) && Context.State->GetRegionController(Region->RegionIndex) == 0)
			++Forces.ControlledRegions;
	for (const ADepositSite* Deposit : Context.State->Deposits)
		if (IsValid(Deposit) && Deposit->Remaining > 0 && !IsValid(Deposit->Extractor)
			&& Context.State->GetRegionController(Deposit->RegionIndex) == 0
			&& !Context.State->IsRegionContested(Deposit->RegionIndex, 0))
			++Forces.FreeDeposits;
	return Forces;
}

void DrawHQBar(const FPainter& Paint, const FRect& Rect, const TCHAR* Label, const AHeadquarters* HQ, const FLinearColor& Color)
{
	const int32 Health = IsValid(HQ) ? HQ->Health : 0;
	const int32 Max = IsValid(HQ) ? FMath::Max(1, HQ->MaxHealth()) : 1;
	Paint.Bar(Rect, static_cast<float>(Health) / Max, Color.CopyWithNewOpacity(.62f));
	Paint.Outline(Rect, Color.CopyWithNewOpacity(.55f));
	Paint.TextIn(Label, Rect, 8.5f, Palette::Text, true, EAlign::Left, 7.f);
	TStringBuilder<32> Value;
	Value.Appendf(TEXT("%d / %d"), Health, Max);
	Paint.TextIn(Value.ToView(), Rect, 8.5f, Palette::Text, true, EAlign::Right, 7.f);
}

void DrawTopBar(const FPainter& Paint, const FContext& Context, const FForces& Forces, const FLayout& Layout)
{
	const FRect& Top = Layout.Top;
	Paint.Panel(Top);
	if (!Context.Wallet || !Context.State || Context.Wallet->CommanderIndex < 0)
	{
		Paint.TextIn(TEXT("Syncing commander, wallet and territory..."), Top, 10.f, Palette::Warn, false, EAlign::Left, Pad);
		return;
	}
	TStringBuilder<128> Economy;
	Economy.Appendf(TEXT("C%d   %d Power  +%d/s   Forces %d   Regions %d/%d"),
		Context.Wallet->CommanderIndex + 1, Context.Balance, Context.Wallet->GetIncomePerSecond(),
		Forces.ConfiguredForces, Forces.ControlledRegions, Context.State->Regions.Num());
	Paint.TextIn(Economy.ToView(), { Top.X + Pad, Top.Y, Top.W - 2.f * Pad - 360.f, Top.H },
		10.f, Palette::Gold, true);
	const float HQX = Top.Right() - Pad - 350.f;
	DrawHQBar(Paint, { HQX, Top.Y + 7.f, 170.f, 18.f }, TEXT("YOUR HQ"), Context.State->FriendlyHeadquarters, Palette::Friendly);
	DrawHQBar(Paint, { HQX + 180.f, Top.Y + 7.f, 170.f, 18.f }, TEXT("ENEMY HQ"), Context.State->EnemyHeadquarters, Palette::Enemy);
}

}
