#include "HUDPanels.h"
#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "Commands/PingCommandComponent.h"
#include "Rules/AnnouncerPolicy.h"
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
			|| !IsValid(Group->GetOwningPlayerState()) || Group->GetTeamIndex() != Context.Wallet->TeamIndex
			|| !Context.Controller->IsSelectableForce(Group))
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
	if (!Context.State || !Context.Wallet || !Context.Controller || Layout.Scale <= 0.f)
		return;
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
	for (const ACommandBuilding* Building : Context.State->Buildings)
	{
		if (!IsValid(Building) || Building->IsActorBeingDestroyed() || !Building->IsAlive() || !Building->IsProducer()
			|| Building->ForceNumber <= 0 || !IsValid(Building->OwningPlayerState)
			|| Building->TeamIndex != Context.Wallet->TeamIndex)
			continue;
		FVector2D Screen;
		if (!ProjectOverlay(Paint, Context, Building->GetActorLocation() + FVector(0.f, 0.f, 220.f), Screen))
			continue;
		TStringBuilder<16> Text;
		Text.Appendf(TEXT("%d"), Building->ForceNumber);
		const float Width = FMath::Max(22.f, Paint.TextWidth(Text.ToView(), 14.f, true) + 10.f);
		const float Height = Paint.LineHeight(14.f, true) + 4.f;
		const FRect Label{ Screen.X - Width * .5f, Screen.Y - Height - Paint.LineHeight(10.f, true) - 32.f, Width, Height };
		if (!OverlayClearsPanels(Context, Layout, Label))
			continue;
		const FLinearColor Color = AArmyUnit::GetCommanderColor(Building->OwningPlayerState->CommanderIndex);
		Paint.Fill(Label, FLinearColor(.005f, .008f, .012f, .95f));
		Paint.Outline(Label, Color);
		Paint.TextIn(Text.ToView(), Label, 14.f, Color, true, EAlign::Center);
	}
}

void DrawPingMarkers(const FPainter& Paint, const FContext& Context)
{
	if (!Context.State || !Context.Controller->PingCommands)
		return;
	const float Now = Context.State->GetServerWorldTimeSeconds();
	for (const FObjectiveEvent& Event : Context.Controller->PingCommands->GetEvents())
	{
		if (Now - Event.ServerTime >= UPingCommandComponent::Lifetime)
			continue;
		const AnnouncerPolicy::FDefinition* Definition = AnnouncerPolicy::Find(Event.Id);
		if (!Definition || Event.Forces.IsEmpty())
			continue;
		FVector2D Screen;
		if (!ProjectOverlay(Paint, Context, Event.Location + FVector(0.f, 0.f, 35.f), Screen))
			continue;
		const FRect Marker{ Screen.X - 10.f, Screen.Y - 10.f, 20.f, 20.f };
		if (!OverlayFits(Paint, Marker))
			continue;
		const FLinearColor Color = AArmyUnit::GetCommanderColor(Event.Forces[0].CommanderIndex);
		Paint.Fill({ Screen.X - 10.f, Screen.Y - 1.f, 6.f, 2.f }, Color);
		Paint.Fill({ Screen.X + 4.f, Screen.Y - 1.f, 6.f, 2.f }, Color);
		Paint.Fill({ Screen.X - 1.f, Screen.Y - 10.f, 2.f, 6.f }, Color);
		Paint.Fill({ Screen.X - 1.f, Screen.Y + 4.f, 2.f, 6.f }, Color);
		const float Width = Paint.TextWidth(Definition->Text, 12.f, true) + 16.f;
		const float Height = Paint.LineHeight(12.f, true) + 6.f;
		const FRect Label{ Screen.X - Width * .5f, Marker.Y - Height - 3.f, Width, Height };
		if (OverlayFits(Paint, Label))
		{
			Paint.Fill(Label, Palette::Panel);
			Paint.Outline(Label, Color);
			Paint.TextIn(Definition->Text, Label, 12.f, Color, true, EAlign::Center);
		}
	}
}

}
