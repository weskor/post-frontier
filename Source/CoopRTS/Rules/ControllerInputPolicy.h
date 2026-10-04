#pragma once

#include "CoreMinimal.h"

// Geometry, timing and ordering only: the controller adapters read actors, the viewport and the clock.
namespace ControllerInputPolicy
{
constexpr double DoubleClickSeconds = .3;
constexpr double BuildHotkeyWindowSeconds = 2.;
constexpr double FeedbackSeconds = 4.;
constexpr float EdgePanMargin = 8.f;
// Cells of territory shown around a footprint; the window never exceeds MaxWindowCells per axis.
constexpr int32 WindowPaddingCells = 16;
constexpr int32 MaxWindowCells = 22;

struct FPlacementWindow
{
	int32 WindowCells = 0;
	int32 Margin = 0;
};

// Both times are real seconds; the second press must not be later than DoubleClickSeconds.
bool IsDoubleClick(double Now, double Previous);
// A pending B chord expires BuildHotkeyWindowSeconds after it began.
bool IsBuildHotkeyLive(double Now, double Started);
// Fades from 1 to 0 over the last second of FeedbackSeconds.
float FeedbackOpacity(double ElapsedSeconds);
// Edge-pan direction (X forward, Y right) for a mouse inside a viewport; zero outside it.
FVector2D EdgePanAxis(const FVector2D& Mouse, const FIntPoint& ViewportSize);
// Intersection of a view ray with the Z = 0 plane; false when parallel, behind the origin or non-finite.
bool GroundPoint(const FVector& Origin, const FVector& Direction, FVector& OutPoint);
// Territory window around a footprint of FootprintCells cells, centred with an equal margin per side.
FPlacementWindow PlacementWindow(int32 FootprintCells);
// Alert to focus on a press of Space: the newest one, then each earlier one while the newest was already seen.
// Returns INDEX_NONE for an empty list; a focused sequence at the oldest event keeps the oldest.
int32 AlertCycleSequence(TConstArrayView<int32> Sequences, int32 LatestSeenSequence, int32 FocusedSequence);
// World XY on the minimap, in screen pixels; X is up-screen, Y is right-screen.
FVector2D MinimapPoint(const FVector2D& WorldXY, const FVector2D& HalfExtent, const FVector2D& MapOrigin, double MapSize);
// Manhattan distance, in minimap pixels, between a world offset and the drawn force diamond.
double MinimapDiamondDistance(const FVector2D& WorldDelta, const FVector2D& HalfExtent, double MapSize);
// Square pick box around a minimap marker, in screen pixels; edges are inclusive.
bool IsWithinMarker(const FVector2D& Position, const FVector2D& Marker, double Radius);

// Fortify targeting (ui.md surface 4): what H and a left click do, by whether the mode is armed and a cast is
// still awaiting the server. RMB and Esc cancel through the shared pointer-mode cancel.
enum class EFortifyInput : uint8
{
	HKey,
	LeftClick
};
enum class EFortifyStep : uint8
{
	Ignore,
	Arm,
	Cancel,
	Cast,
	// The click is consumed but sends nothing: the previous cast has not been answered yet.
	Wait
};
// H arms, and cancels again while armed; LMB casts, or waits while a cast is pending; otherwise nothing.
EFortifyStep FortifyStep(bool bArmed, bool bCastPending, EFortifyInput Input);
// The mode ends on acceptance and stays open on rejection, so a refused cast can be retried elsewhere.
bool FortifyStaysArmed(bool bArmed, bool bAccepted);
}
