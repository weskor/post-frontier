#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "VerbInputFixture.h"

namespace VerbInputTests
{
bool FScenario::StageSmartMinimap()
{
	FVector2D Ground;
	if (!Check(PC->ProjectWorldLocationToScreen(State->GetRegionAnchor(Target), Ground), TEXT("Map region projects onto ground viewport"))
		|| !Submit(Ground, ForceOrderInput::EResolution::MoveHold, EForceVerb::MoveHold, Target)
		|| !Submit(Minimap(HostileRegionPoint), ForceOrderInput::EResolution::MoveHold, EForceVerb::MoveHold, EnemyHome)
		|| !Submit(Minimap(Hostile->GetActorLocation()), ForceOrderInput::EResolution::Attack, EForceVerb::Attack,
			RegionAt(State, Hostile->GetActorLocation()), Hostile)
		|| !Submit(Minimap(Node->GetActorLocation()), ForceOrderInput::EResolution::Attack, EForceVerb::Attack,
			RegionAt(State, Node->GetActorLocation()), Node))
		return true;
	Camera->FocusOn(Hostile->GetActorLocation());
	++Stage;
	return false;
}

bool FScenario::StageSmartNode()
{
	if (!bNodeFocused)
	{
		Camera->FocusOn(Node->GetActorLocation());
		bNodeFocused = true;
		return false;
	}
	FVector2D Ground;
	if (!Check(PC->ProjectWorldLocationToScreen(Node->GetActorLocation(), Ground), TEXT("Hostile node projects onto ground viewport"))
		|| !Submit(Ground, ForceOrderInput::EResolution::Attack, EForceVerb::Attack, RegionAt(State, Node->GetActorLocation()), Node))
		return true;
	bNodeClicked = true;
	Camera->FocusOn(Hostile->GetActorLocation());
	return false;
}

bool FScenario::StageSmartGround()
{
	FVector2D Ground;
	if (!Check(PC->ProjectWorldLocationToScreen(Hostile->GetActorLocation() + FVector(0.f, 0.f, 40.f), Ground),
			TEXT("Hostile structure projects onto ground viewport"))
		|| !Submit(Ground, ForceOrderInput::EResolution::Attack, EForceVerb::Attack,
			RegionAt(State, Hostile->GetActorLocation()), Hostile))
		return true;
	Key(EKeys::A, IE_Pressed);
	++Stage;
	return false;
}

bool FScenario::Submit(const FVector2D& Point, ForceOrderInput::EResolution Resolution, EForceVerb Verb,
	int32 Region, AActor* Structure, bool bQueue)
{
	const FOrderInputPreview Preview = PC->GetOrderPreview(Point, bQueue);
	if (!Preview.IsAllowed() || Preview.Resolution != Resolution || Preview.RegionIndex != Region
		|| Preview.Structure != Structure)
	{
		Test->AddInfo(FString::Printf(TEXT("Preview at (%.1f, %.1f): resolution=%d rejection=%d region=%d structure=%s panel=%d camera=%s"),
			Point.X, Point.Y, static_cast<int32>(Preview.Resolution), static_cast<int32>(Preview.Rejection),
			Preview.RegionIndex, *GetNameSafe(Preview.Structure), HUD->IsPanelPoint(Point), *Camera->GetActorLocation().ToString()));
		return Check(false, TEXT("Smart preview resolves expected verb and target"));
	}
	return Check(PC->HandleOrderClick(Point, bQueue), TEXT("Shared smart right-click entry consumes the order"))
		&& OrdersMatch(Verb, Region, Structure, bQueue);
}

bool FScenario::OrdersMatch(EForceVerb Verb, int32 Region, AActor* Structure, bool bQueue)
{
	for (AArmyGroup* Force : Forces)
	{
		if (!Check(!Force->Orders.IsEmpty(), TEXT("Selected force receives an order")))
			return false;
		const FForceOrder& Order = Force->Orders.Last();
		if (!Check(Order.Verb == Verb && Order.RegionIndex == Region && Order.Structure == Structure,
				TEXT("Every selected force's resulting order agrees with the hovered preview"))
			|| !Check(bQueue || (Force->Orders.Num() == 1 && Force->Verb == Verb && Force->TargetRegionIndex == Region && Force->TargetStructure == Structure), TEXT("Unshifted input replaces active order")))
			return false;
	}
	return true;
}
}
#endif
