#include "ForceBar.h"
#include "HUDPanels.h"
#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "HAL/PlatformTime.h"

using namespace CommandHUDPanels;

AArmyGroup* ACommandHUD::GetForceCardAtScreenPosition(const FVector2D& Position, EHUDAction& OutAction) const
{
	OutAction = EHUDAction::None;
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller || Controller->GetUIScreen() != ECommandScreen::Game)
		return nullptr;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	if (Layout.Scale <= 0.f)
		return nullptr;
	AArmyGroup* Result = nullptr;
	ForEachForceCard(Context, Layout, [&](AArmyGroup* Force, const FRect& Rect) {
		if (Rect.Contains(Position / Layout.Scale))
		{
			Result = Force;
			FForceCard Card;
			ReadForceCard(Context, *Force, INDEX_NONE, Card);
			OutAction = HitTestForceCard(Card, Rect, Position / Layout.Scale);
		}
	});
	return Result;
}

bool ACommandHUD::FindForceCardScreenPosition(const AArmyGroup* Force, EHUDAction Action, FVector2D& OutPosition) const
{
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller || Controller->GetUIScreen() != ECommandScreen::Game)
		return false;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	if (Layout.Scale <= 0.f)
		return false;
	bool bFound = false;
	ForEachForceCard(Context, Layout, [&](AArmyGroup* Candidate, const FRect& Rect) {
		if (Candidate != Force)
			return;
		if (Action == EHUDAction::None)
		{
			OutPosition = FVector2D(Rect.X + 10.f, Rect.Y + 10.f) * Layout.Scale;
			bFound = true;
			return;
		}
		FForceCard Card;
		ReadForceCard(Context, *Force, INDEX_NONE, Card);
		ForEachForceCardButton(Card, Rect, [&](const FButton& Button, FStringView) {
			if (Button.Action == Action)
			{
				OutPosition = Button.Rect.Center() * Layout.Scale;
				bFound = true;
			}
		});
	});
	return bFound;
}

int32 ACommandHUD::GetForceETA(const AArmyGroup& Force) const
{
	if (Force.Status != EForceStatus::Marching && Force.Status != EForceStatus::Withdrawing && Force.Status != EForceStatus::Retreating)
		return INDEX_NONE;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
		return INDEX_NONE;
	ForceETACache.RemoveAllSwap([](const ForceTravelETA::FEntry& Entry) { return !Entry.Force.IsValid(); });
	ForceTravelETA::FEntry* Entry = ForceETACache.FindByPredicate([&](const ForceTravelETA::FEntry& Item) { return Item.Force.Get() == &Force; });
	if (!Entry)
	{
		Entry = &ForceETACache.AddDefaulted_GetRef();
		Entry->Force = &Force;
	}
	const double Now = FPlatformTime::Seconds();
	if (Entry->OrderSerial != Force.OrderSerial || Entry->Waypoint != Force.WaypointRegionIndex || Now - Entry->Updated >= .5)
	{
		Entry->OrderSerial = Force.OrderSerial;
		Entry->Waypoint = Force.WaypointRegionIndex;
		Entry->Updated = Now;
		Entry->Seconds = ForceTravelETA::Compute(Force, *State);
	}
	return Entry->Seconds;
}
