#pragma once

#include "Rules/RouteIntent.h"

class ACommandPlayerController;
class AArmyGroup;
class AWorldOverlay;
struct FForceRoute;

namespace ForceRoutePresentation
{
struct FRoute
{
	RouteIntent::FPolyline Line;
	FLinearColor Color;
	int32 OrderIndex = 0;
	int32 TargetRegionIndex = INDEX_NONE;
	bool bActive = false;
	bool bSelected = false;
	bool bPreview = false;
};
// Canonical accepted-route geometry, including formation and structure endpoint corrections.
RouteIntent::FPolyline BuildLine(const AArmyGroup& Force, const FForceRoute& Route, const FVector& Start,
	bool bActive, TConstArrayView<FVector> Anchors);
// Both surfaces consume the same replicated routes and client preview geometry.
void Visit(const ACommandPlayerController& Controller, TFunctionRef<void(const FRoute&)> Draw);
void DrawWorld(AWorldOverlay& Overlay, const ACommandPlayerController& Controller);
}
