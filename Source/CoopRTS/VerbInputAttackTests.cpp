#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "VerbInputFixture.h"

namespace VerbInputTests
{
bool FScenario::StageUnshiftedConfirm()
{
	if (!Check(!PC->IsInputKeyDown(EKeys::LeftShift), TEXT("Shift release reaches the input state before unshifted confirmation"))
		|| !ConfirmMinimapAttack())
		return true;
	Camera->FocusOn(State->GetRegionAnchor(Target));
	Key(EKeys::A, IE_Pressed);
	++Stage;
	return false;
}

bool FScenario::StageGroundAttackOpen()
{
	Key(EKeys::A, IE_Released);
	FVector2D Ground;
	if (!Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded(), TEXT("A opens ground targeting with the deck collapsed"))
		|| !Check(PC->ProjectWorldLocationToScreen(State->GetRegionAnchor(Target), Ground) && !HUD->IsPanelPoint(Ground),
			TEXT("Attack region projects onto the uncovered ground viewport")))
		return true;
	PC->SetMouseLocation(FMath::RoundToInt(Ground.X), FMath::RoundToInt(Ground.Y));
	float X, Y;
	if (!Check(PC->GetMousePosition(X, Y), TEXT("Native ground click has a viewport cursor"))
		|| !AttackPreview(FVector2D(X, Y), false))
		return true;
	SaveSerials();
	Key(EKeys::LeftMouseButton, IE_Pressed);
	++Stage;
	return false;
}

bool FScenario::StageGroundAttackClick()
{
	Key(EKeys::LeftMouseButton, IE_Released);
	if (!Check(!Unchanged(), TEXT("Native LMB through the controller selection path submits the ground Attack"))
		|| !OrdersMatch(EForceVerb::Attack, Target, nullptr, false)
		|| !Check(!PC->IsAssigningOrder() && PC->IsHUDExpanded(), TEXT("Accepted native ground A ends targeting and restores the deck"))
		|| !ExerciseMinimapRally())
		return true;
	++Stage;
	return false;
}

bool FScenario::AttackPreview(const FVector2D& Point, bool bQueue)
{
	const FOrderInputPreview Preview = PC->GetOrderPreview(Point, bQueue);
	return Check(Preview.IsAllowed() && Preview.Resolution == ForceOrderInput::EResolution::Attack
			&& Preview.RegionIndex == Target && !Preview.Structure,
		TEXT("Pending A previews Attack on a region rather than smart MoveHold"));
}

bool FScenario::ConfirmMinimapAttack()
{
	const FVector2D Point = Minimap(State->GetRegionAnchor(Target));
	return AttackPreview(Point, false)
		&& Check(PC->HandleHUDClick(Point), TEXT("Real minimap HUD click confirms pending A"))
		&& OrdersMatch(EForceVerb::Attack, Target, nullptr, false)
		&& Check(!PC->IsAssigningOrder() && PC->IsHUDExpanded(), TEXT("Accepted minimap A ends targeting and restores the deck"));
}
}
#endif
