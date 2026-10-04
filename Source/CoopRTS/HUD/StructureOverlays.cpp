#include "HUDPanels.h"
#include "PressurePanels.h"
#include "CommandGameState.h"
#include "FailoverNode.h"
#include "GuardedHqPanels.h"
#include "Headquarters.h"
#include "WorldOverlay.h"

namespace CommandHUDPanels
{
// Stun, when shown, adds the building's chip above its plate and freezes the construction bar to a desaturated grey.
static void DrawStructureOverlay(const FPainter& Paint, const FContext& Context, const FVector& Position,
	FStringView Label, int32 Health, int32 Maximum, const FLinearColor& Color,
	bool bConstructing = false, float Progress = 1.f, bool bImmune = false, const PressureHud::FStunChip& Stun = {})
{
	if (Maximum <= 0)
		return;
	FVector2D Screen;
	if (!ProjectOverlay(Paint, Context, Position, Screen))
		return;
	const float Line = Paint.LineHeight(10.f, true);
	const float Width = FMath::Clamp(Paint.TextWidth(Label, 10.f, true) + 12.f, 96.f, 280.f);
	const FRect Back{ Screen.X - Width * .5f, Screen.Y - Line - (bConstructing ? 20.f : 12.f) - 6.f,
		Width, Line + (bConstructing ? 20.f : 12.f) };
	if (!OverlayFits(Paint, Back))
		return;
	Paint.Fill(Back, FLinearColor(.005f, .008f, .012f, .95f));
	Paint.TextIn(Label, { Back.X + 4.f, Back.Y + 2.f, Width - 8.f, Line },
		10.f, Color, true, EAlign::Center);
	const FRect Bar{ Back.X + 4.f, Back.Y + Line + 4.f, Width - 8.f, 4.f };
	Paint.Bar(Bar, static_cast<float>(Health) / Maximum, Color);
	if (bImmune)
		DrawImmuneHatch(Paint, Bar);
	if (bConstructing)
		Paint.Bar({ Back.X + 4.f, Back.Y + Line + 12.f, Width - 8.f, 4.f }, Progress, Stun.bShown ? Palette::Faint : Palette::Gold);
	DrawStunChip(Paint, Back, Stun);
}

void DrawHeadquartersOverlays(const FPainter& Paint, const FContext& Context)
{
	if (!Context.State)
		return;
	const AHeadquarters* Headquarters[] = { Context.State->FriendlyHeadquarters.Get(), Context.State->EnemyHeadquarters.Get() };
	for (const AHeadquarters* HQ : Headquarters)
	{
		if (!IsValid(HQ))
			continue;
		TStringBuilder<96> Label;
		AppendHqLabel(Label, *HQ);
		const bool bOffline = HQ->IsOffline();
		DrawStructureOverlay(Paint, Context, HQ->GetActorLocation() + FVector(0.f, 0.f, 300.f), Label.ToView(),
			bOffline ? HqHoldPolicy::WholeSeconds(HQ->GetHold()) : HQ->Health,
			bOffline ? FMath::RoundToInt(HqHoldPolicy::HoldSeconds) : HQ->MaxHealth(),
			HQ->TeamIndex == 5 ? Palette::Bad : Palette::Good, false, 1.f, HQ->IsImmune());
		// A visible beam links each standing node to the HQ it guards.
		AWorldOverlay* Overlay = AWorldOverlay::Get(HQ);
		for (const TWeakObjectPtr<AFailoverNode>& Entry : HQ->GetNodes())
		{
			const AFailoverNode* Node = Entry.Get();
			if (!IsValid(Node) || !Node->IsAlive())
				continue;
			if (Overlay)
				Overlay->Line(HQ->GetActorLocation() + FVector(0.f, 0.f, 150.f), Node->GetActorLocation() + FVector(0.f, 0.f, 150.f),
					HQ->TeamIndex == 5 ? FColor(255, 70, 60) : FColor(70, 170, 255), 6.f);
			TStringBuilder<64> NodeLabel;
			NodeLabel.Appendf(TEXT("FAILOVER NODE %d/%d%s"), Node->Health, Node->MaxHealth(), Node->IsPlated() ? TEXT(" \u00B7 plated") : TEXT(""));
			DrawStructureOverlay(Paint, Context, Node->GetActorLocation() + FVector(0.f, 0.f, 380.f), NodeLabel.ToView(),
				Node->Health, Node->MaxHealth(), HQ->TeamIndex == 5 ? Palette::Bad : Palette::Good);
		}
	}
}

void DrawBuildingOverlays(const FPainter& Paint, const FContext& Context)
{
	if (!Context.State)
		return;
	for (const ACommandBuilding* Building : Context.State->Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive())
			continue;
		const UBuildingDefinition* Definition = Building->GetDefinition();
		TStringBuilder<256> Label;
		Label << (Definition ? FStringView(Definition->DisplayName.ToString()).Left(160) : FStringView(TEXT("BUILDING")));
		Label.Appendf(TEXT("  %d/%d"), Building->Health, Building->MaxHealth());
		DrawStructureOverlay(Paint, Context, Building->GetActorLocation() + FVector(0.f, 0.f, 220.f),
			Label.ToView(), Building->Health, Building->MaxHealth(),
			Building->TeamIndex == 5 ? Palette::Bad : Palette::Good,
			!Building->IsComplete(), Building->ConstructionProgress, false, BuildingStun(Context, *Building));
	}
}

}
