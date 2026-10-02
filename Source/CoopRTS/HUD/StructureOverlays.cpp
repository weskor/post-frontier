#include "HUDPanels.h"
#include "CommandGameState.h"
#include "Headquarters.h"

namespace CommandHUDPanels
{
static void DrawStructureOverlay(const FPainter& Paint, const FContext& Context, const FVector& Position,
	FStringView Label, int32 Health, int32 Maximum, const FLinearColor& Color,
	bool bConstructing = false, float Progress = 1.f)
{
	if (Maximum <= 0)
		return;
	FVector2D Screen;
	if (!ProjectOverlay(Paint, Context, Position, Screen))
		return;
	const float Line = Paint.LineHeight(10.f, true);
	const float Width = FMath::Clamp(Paint.TextWidth(Label, 10.f, true) + 12.f, 96.f, 220.f);
	const FRect Back{ Screen.X - Width * .5f, Screen.Y - Line - (bConstructing ? 20.f : 12.f) - 6.f,
		Width, Line + (bConstructing ? 20.f : 12.f) };
	if (!OverlayFits(Paint, Back))
		return;
	Paint.Fill(Back, FLinearColor(.005f, .008f, .012f, .95f));
	Paint.TextIn(Label, { Back.X + 4.f, Back.Y + 2.f, Width - 8.f, Line },
		10.f, Color, true, EAlign::Center);
	Paint.Bar({ Back.X + 4.f, Back.Y + Line + 4.f, Width - 8.f, 4.f },
		static_cast<float>(Health) / Maximum, Color);
	if (bConstructing)
		Paint.Bar({ Back.X + 4.f, Back.Y + Line + 12.f, Width - 8.f, 4.f }, Progress, Palette::Gold);
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
		TStringBuilder<64> Label;
		Label.Appendf(TEXT("%s HQ %d/%d"), HQ->TeamIndex == 5 ? TEXT("ENEMY") : TEXT("FRIENDLY"),
			HQ->Health, HQ->MaxHealth());
		DrawStructureOverlay(Paint, Context, HQ->GetActorLocation() + FVector(0.f, 0.f, 300.f),
			Label.ToView(), HQ->Health, HQ->MaxHealth(), HQ->TeamIndex == 5 ? Palette::Bad : Palette::Good);
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
			!Building->IsComplete(), Building->ConstructionProgress);
	}
}

}
