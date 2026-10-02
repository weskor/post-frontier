#include "HUDPanels.h"
#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace CommandHUDPanels
{
void DrawForceLabels(const FPainter& Paint, const FContext& Context)
{
	if (!Context.State || !Context.Wallet)
		return;
	const auto DrawNumber = [&Paint, &Context](const FVector& Position, int32 Number, int32 Commander, float Lift = 6.f) {
		FVector2D Screen;
		if (!ProjectOverlay(Paint, Context, Position, Screen))
			return;
		TStringBuilder<16> Text;
		Text.Appendf(TEXT("%d"), Number);
		const float Width = FMath::Max(22.f, Paint.TextWidth(Text.ToView(), 14.f, true) + 10.f);
		const float Height = Paint.LineHeight(14.f, true) + 4.f;
		const FRect Badge{ Screen.X - Width * .5f, Screen.Y - Height - Lift, Width, Height };
		if (!OverlayFits(Paint, Badge))
			return;
		const FLinearColor Color = AArmyUnit::GetCommanderColor(Commander);
		Paint.Fill(Badge, FLinearColor(.005f, .008f, .012f, .95f));
		Paint.Outline(Badge, Color);
		Paint.TextIn(Text.ToView(), Badge, 14.f, Color, true, EAlign::Center);
	};
	for (TActorIterator<AArmyUnit> It(Context.Controller->GetWorld()); It; ++It)
	{
		const AArmyUnit* Unit = *It;
		const AArmyGroup* Group = Unit->GetGroup();
		if (!Unit->IsAlive() || !IsValid(Group) || Group->ForceNumber <= 0
			|| !IsValid(Group->GetOwningPlayerState()) || Group->GetTeamIndex() != 0 || Unit->GetTeamIndex() != 0)
			continue;
		DrawNumber(Unit->GetActorLocation() + FVector(0.f, 0.f, 135.f),
			Group->ForceNumber, Group->GetOwningPlayerState()->CommanderIndex, 17.f);
	}
	for (const ACommandBuilding* Building : Context.State->Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive() || Building->ForceNumber <= 0
			|| !IsValid(Building->OwningPlayerState) || Building->TeamIndex != 0)
			continue;
		DrawNumber(Building->GetActorLocation() + FVector(0.f, 0.f, 220.f),
			Building->ForceNumber, Building->OwningPlayerState->CommanderIndex,
			Paint.LineHeight(10.f, true) + 32.f);
	}
}

}
