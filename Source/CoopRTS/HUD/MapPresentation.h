#pragma once

#include "CoreMinimal.h"
#include "Rules/MapPresentationPolicy.h"

class ACommandGameState;
class AMapRegion;
class ADepositSite;
class AWorldOverlay;
class UWorld;
class UCanvas;
namespace CommandMinimap
{
struct FMap;
}

// The map's view of step 1b state: supply cuts read the replicated connected masks and their change times (one
// connectivity rule, never a copy), so a client that joins after a cut sees the persistent state. Drawing and decisions
// live in Rules/MapPresentationPolicy.
namespace MapView
{
// The two teams that have a supply chain: the humans and JEV.
inline constexpr int32 Teams[] = { 0, 5 };

MapPresentation::FRegionLink LinkOf(const ACommandGameState& State, int32 Team, int32 Region);
bool IsCutOff(const ACommandGameState& State, int32 Team, int32 Region);
// Whether a finished, living Drill Rig stands on this deposit in a region its team no longer reaches.
bool IsRigOffline(const ACommandGameState& State, const ADepositSite& Deposit);
// Drill Rigs of Team on deposits in Region that are offline.
int32 OfflineRigs(const ACommandGameState& State, int32 Team, int32 Region);
// One call per pair of neighbouring regions (ascending index) that has a cable for Team.
void ForEachCable(const ACommandGameState& State, int32 Team,
	TFunctionRef<void(const AMapRegion& A, const AMapRegion& B, MapPresentation::ECable Cable)> Visit);
// Seconds since the team's connected set last changed, or negative when the flash window is closed.
float FlashAge(const ACommandGameState& State, int32 Team);

// The minimap's marks: a red dashed outline with hatch for each cut-off region, and a 10 px trait glyph at each node's
// top-left, clipped to the minimap square.
void DrawMinimapMarks(const ACommandGameState& State, const CommandMinimap::FMap& Map);
}

// Cables, cut borders, hatch, snap sparks and the Scrambler pulse ring, drawn as world lines. The overlay actor owns one.
class FMapPresentationWorld
{
public:
	void Draw(AWorldOverlay& Overlay, const ACommandGameState& State);

private:
	struct FRegionGeometry
	{
		TArray<FVector> Border;
		// Hatch sub-segments as consecutive point pairs.
		TArray<FVector> Hatch;
	};
	struct FCableGeometry
	{
		TArray<FVector> Path;
		// Index in Path of the sample nearest the region border, where a snapped cable breaks.
		int32 Break = INDEX_NONE;
	};
	double GroundAt(const UWorld& World, double X, double Y, double Fallback);
	const FRegionGeometry& Geometry(const UWorld& World, const AMapRegion& Region);
	const FCableGeometry& Cable(const UWorld& World, const ACommandGameState& State, const AMapRegion& From, const AMapRegion& To);
	void DrawCuts(AWorldOverlay& Overlay, const ACommandGameState& State, int32 Team);
	void DrawCables(AWorldOverlay& Overlay, const ACommandGameState& State, int32 Team);
	void DrawPulses(AWorldOverlay& Overlay, const ACommandGameState& State);

	TMap<FIntPoint, float> Ground;
	TMap<int32, FRegionGeometry> Regions;
	TMap<uint32, FCableGeometry> Cables;
};
