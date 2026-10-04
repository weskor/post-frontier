#pragma once

#include "CoreMinimal.h"

namespace RouteCapturePolicy
{
// What a marching force does this tick.
enum class EStep : uint8
{
	// Walk to the region it stands in and wait for it to be controlled.
	Secure,
	// Walk to the next region of the shortest route toward the target.
	Advance,
	// Keep walking to the waypoint already applied; the region underfoot is off the route.
	Continue
};

struct FMarch
{
	// The region the force physically stands in, the order's target, and the last region
	// the force stood in while on the route (INDEX_NONE when no route is held yet).
	int32 Source = INDEX_NONE;
	int32 Target = INDEX_NONE;
	int32 Origin = INDEX_NONE;
	// The waypoint region currently being walked to, or INDEX_NONE.
	int32 Applied = INDEX_NONE;
	// Facts about the standing region: whether it has a capture anchor, is controlled by the
	// force's team, or is contested (a hostile stands at its anchor).
	bool bHasAnchor = false;
	bool bControlled = false;
	bool bContested = false;
	// The force has physically reached Source. Only read while Source is contested and applied.
	bool bArrived = false;
};

struct FDecision
{
	EStep Step = EStep::Secure;
	// The route origin to keep for the next tick.
	int32 Origin = INDEX_NONE;
};

// Intermediate capture applies only to regions on the shortest graph route (ascending-index tie
// break, as ForceOrders::NextWaypoint) from the last on-route region to the target. A region the
// navmesh path merely clips is never captured; the force keeps walking its applied waypoint.
// Standing on the route: capture the region unless the target, uncapturable or already controlled
// (advance), or contested and the waypoint was physically reached (advance without chasing).
// With no route held, no applied waypoint or no route at all, the standing region re-roots the route.
FDecision Decide(const uint64* Graph, int32 Count, const FMarch& March);
}
