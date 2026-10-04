#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "VerbInputFixture.h"

namespace VerbInputTests
{
bool FScenario::StageQueueAppend()
{
	const bool bQueue = PC->IsInputKeyDown(EKeys::LeftShift);
	if (!Check(bQueue, TEXT("Real Shift key supplies the queue modifier"))
		|| !Submit(Minimap(HostileRegionPoint), ForceOrderInput::EResolution::MoveHold, EForceVerb::MoveHold, EnemyHome, nullptr, bQueue)
		|| !Submit(Minimap(Hostile->GetActorLocation()), ForceOrderInput::EResolution::Attack, EForceVerb::Attack,
			RegionAt(State, Hostile->GetActorLocation()), Hostile, bQueue))
		return true;
	for (AArmyGroup* Force : Forces)
		if (!Check(Force->Orders.Num() == 3 && Force->Orders[0].Verb == EForceVerb::MoveHold
					&& Force->Orders[0].RegionIndex == Target && Force->Orders[1].RegionIndex == EnemyHome
					&& Force->Orders[2].Structure == Hostile,
				TEXT("Shift preserves active order and appends through three total orders")))
			return true;
	SaveSerials();
	const FVector2D Point = Minimap(State->GetRegionAnchor(Target));
	const FOrderInputPreview Preview = PC->GetOrderPreview(Point, true);
	if (!Rejection(Preview))
		return true;
	PC->HandleOrderClick(Point, true);
	if (!RejectedUnchanged(Preview))
		return true;
	Key(EKeys::A, IE_Pressed);
	++Stage;
	return false;
}

bool FScenario::StageQueueFullTargeting()
{
	Key(EKeys::A, IE_Released);
	if (!Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded(), TEXT("A opens with the deck collapsed even when the existing queue is full")))
		return true;
	const FVector2D Point = Minimap(State->GetRegionAnchor(Target));
	const FOrderInputPreview Preview = PC->GetOrderPreview(Point, true);
	if (!Rejection(Preview))
		return true;
	if (!Check(PC->HandleHUDClick(Point), TEXT("Real minimap HUD click consumes rejected A target"))
		|| !RejectedUnchanged(Preview)
		|| !Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded(), TEXT("Rejected A keeps Attack mode open and the deck collapsed")))
		return true;
	Key(EKeys::LeftShift, IE_Released);
	++Stage;
	return false;
}

bool FScenario::Rejection(const FOrderInputPreview& Preview)
{
	return Check(!Preview.IsAllowed() && Preview.Resolution == ForceOrderInput::EResolution::Reject
			&& Preview.Rejection == ForceOrderInput::ERejection::QueueFull
			&& Preview.Label() && *Preview.Label(),
		TEXT("Fourth-order preview rejects with the queue-limit reason"));
}

bool FScenario::RejectedUnchanged(const FOrderInputPreview& Preview)
{
	return Check(Unchanged() && Forces[0]->Orders.Num() == 3 && Forces[1]->Orders.Num() == 3,
			   TEXT("Rejected fourth order changes neither selected force"))
		&& Check(!PC->GetOrderFeedback().IsEmpty() && PC->GetOrderFeedback().Contains(Preview.Label()) && PC->GetFeedbackOpacity() > 0.f,
			TEXT("Rejected input displays the same reason as its preview"));
}
}
#endif
