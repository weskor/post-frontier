#include "CommandPlayerController.h"

bool ACommandPlayerController::GetHoveredForceOrder(FForceOrder& OutOrder, bool& bQueue) const
{
	bQueue = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	float X, Y;
	if (!GetMousePosition(X, Y))
		return false;
	const FOrderInputPreview Preview = GetOrderPreview(FVector2D(X, Y), bQueue);
	if (!Preview.IsAllowed()
		|| (Preview.Resolution != ForceOrderInput::EResolution::MoveHold
			&& Preview.Resolution != ForceOrderInput::EResolution::Attack))
		return false;
	OutOrder = FForceOrder(Preview.Resolution == ForceOrderInput::EResolution::Attack
			? EForceVerb::Attack : EForceVerb::MoveHold,
		Preview.RegionIndex, Preview.Structure);
	return true;
}
