#include "HUDPanels.h"
#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace CommandHUDPanels
{
void ForEachForceBadge(const FPainter& Paint, const FContext& Context, const FLayout& Layout,
	TFunctionRef<void(AArmyGroup*, const FRect&)> Visit)
{
	if (!Context.State || !Context.Wallet || !Context.Controller || Layout.Scale <= 0.f)
		return;
	for (TActorIterator<AArmyGroup> It(Context.Controller->GetWorld()); It; ++It)
	{
		AArmyGroup* Group = *It;
		if (!IsValid(Group) || Group->IsActorBeingDestroyed() || Group->ForceNumber <= 0
			|| !IsValid(Group->GetOwningPlayerState()) || Group->GetTeamIndex() != Context.Wallet->TeamIndex)
			continue;
		FVector2D Screen;
		if (!ProjectOverlay(Paint, Context, Group->GetCenter() + FVector(0.f, 0.f, 135.f), Screen))
			continue;
		TStringBuilder<16> Text;
		Text.Appendf(TEXT("%d"), Group->ForceNumber);
		const float Width = FMath::Max(22.f, Paint.TextWidth(Text.ToView(), 14.f, true) + 10.f);
		const float Height = Paint.LineHeight(14.f, true) + 4.f;
		const FRect Badge{ Screen.X - Width * .5f, Screen.Y - Height - 17.f, Width, Height };
		if (OverlayClearsPanels(Context, Layout, Badge))
			Visit(Group, Badge);
	}
}

void DrawForceLabels(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	ForEachForceBadge(Paint, Context, Layout, [&](AArmyGroup* Group, const FRect& Badge) {
		const FLinearColor Color = AArmyUnit::GetCommanderColor(Group->GetOwningPlayerState()->CommanderIndex);
		const bool bSelected = Context.Controller->IsForceSelected(Group);
		const bool bHighlighted = Context.Controller->IsForceHighlighted(Group);
		const bool bInspected = Context.Force == Group;
		Paint.Fill(Badge, bHighlighted || bInspected ? Tint(Color, .35f, .98f) : FLinearColor(.005f, .008f, .012f, .95f));
		Paint.Outline(Badge, bSelected ? Palette::Gold : bHighlighted || bInspected ? Palette::Text
																					: Color,
			bHighlighted || bInspected ? 2.f : 1.f);
		TStringBuilder<16> Text;
		Text.Appendf(TEXT("%d"), Group->ForceNumber);
		Paint.TextIn(Text.ToView(), Badge, 14.f, bHighlighted || bInspected ? Palette::Text : Color, true, EAlign::Center);
	});
}

}
