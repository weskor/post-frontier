#pragma once

#include "Rules/RouteIntent.h"

class ACommandPlayerController;
class AArmyGroup;
class AWorldOverlay;

namespace ForceRoutePresentation
{
struct FRoute
{
	RouteIntent::FPolyline Line;
	FLinearColor Color;
	int32 OrderIndex = 0;
	bool bSelected = false;
	bool bPreview = false;
};
// Both surfaces consume the same replicated routes and client preview geometry.
void Visit(const ACommandPlayerController& Controller, TFunctionRef<void(const FRoute&)> Draw);
void DrawWorld(AWorldOverlay& Overlay, const ACommandPlayerController& Controller);
}
