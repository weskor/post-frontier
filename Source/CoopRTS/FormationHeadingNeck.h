#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"

class AArmyGroup;
class ACommandGameState;
class ACommandPlayerState;

// Fixture for proving a march in column on the real map: finds the first-leg route, out of the home region, whose
// walkable corridor is narrowest (a neck), and measures the members' extent across and along that route while the
// force really walks it. It uses no formation API, so the same measurement runs against main.
namespace FormationHeadingNeck
{
// The first member's path, resampled every SampleSpacing cm, and its narrowest corridor.
constexpr double SampleSpacing = 150.;
struct FRoute
{
	int32 Start = INDEX_NONE;
	int32 Target = INDEX_NONE;
	TArray<FVector> Points;
	int32 NeckIndex = INDEX_NONE;
	double NeckWidth = 0.;
};
// The members' extent across (Lateral) and along (Along) the route at the force's position.
struct FSpread
{
	double Lateral = 0., Along = 0.;
	int32 Index = INDEX_NONE;
};

// For every start region and every region two or more hops from it, stands the force on the start's anchor and
// orders it to the target (nothing moves between orders; the first member's path is read at once), and keeps the
// route with the narrowest neck. The force is left on that start's anchor, ordered to that target.
// False when no candidate gave a path.
bool ChooseNeckRoute(UWorld& World, ACommandPlayerState& Commander, AArmyGroup& Force, const ACommandGameState& State,
	FRoute& Route);
// Stands two long walls across Route at sample Index, leaving Gap cm of walkable ground centred on the route, as
// real navigation-affecting obstacles. The caller waits for the navigation rebuild.
void BuildNeck(UWorld& World, const FRoute& Route, int32 Index, double Gap, TArray<TWeakObjectPtr<AActor>>& Walls);
// Stands the force on Route.Start again, orders it to Route.Target and reads the first member's new path into
// Route (its narrowest corridor now).
bool Replan(UWorld& World, ACommandPlayerState& Commander, AArmyGroup& Force, const ACommandGameState& State, FRoute& Route);
// The members' spread across and along Route at the point of it nearest the force's centre.
FSpread MeasureSpread(const AArmyGroup& Force, const FRoute& Route);
}
#endif
