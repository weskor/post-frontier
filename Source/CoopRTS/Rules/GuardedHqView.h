#pragma once

#include "CoreMinimal.h"
#include "HqHoldPolicy.h"

// Guarded-HQ readability rules (Design/ui.md, surface 9): the wording of the objective feed's guarded-HQ rows and where
// the minimap draws each HQ's Failover Node marks. Rules only: the HUD reads these and draws them.
namespace GuardedHqView
{
// The guarded-HQ rows of the objective feed, told apart by the announcer event id.
enum class EFeedKind : uint8
{
	Plain,
	// "<side> Failover Node lost": the event's number is the nodes left.
	NodeLost,
	// "<side> HQ back online": the event's number is the restored HP percent.
	BackOnline,
	// "<side> HQ offline. Emergency forces deployed.": carries the EMERGENCY badge.
	Emergency
};
EFeedKind Classify(FStringView EventId);

// The badge text drawn on an emergency row.
inline constexpr const TCHAR* EmergencyBadge = TEXT("EMERGENCY");

// Appends a row's title: Text is the announcer's sentence, Number the event's one number (read by Kind).
// A node loss reads `Hardline Failover Node lost · 1 left`, a revival `Hardline HQ back online at 25% HP`.
void AppendFeedTitle(FStringBuilderBase& Out, FStringView Text, EFeedKind Kind, int32 Number);

// Minimap pixels, from the centre of an HQ's mark. The HQ mark is a 12 px square (half 6) whose offline X stays inside it.
// A Fortify ring (radius 9) and a JEV badge box (half 5.5) are centred on the region's anchor, which sits on the main.
inline constexpr float MinimapHqHalf = 6.f;
inline constexpr float MinimapRingRadius = 9.f;
// A node mark is a diamond of this radius.
inline constexpr float MinimapNodeRadius = 2.5f;
// Marks sit in a row this far below the HQ's centre, which clears the ring by more than a pixel, and this far apart.
inline constexpr float MinimapNodeDrop = 13.f;
inline constexpr float MinimapNodePitch = 10.f;

struct FNodeMarks
{
	FVector2D Centre[HqHoldPolicy::NodesPerHq];
	int32 Count = 0;
};

// Where Count marks go for an HQ at Hq inside the square starting at Origin: a centred row below the HQ, or above it when the
// row would leave the square at the bottom, shifted sideways when it would leave at a side.
FNodeMarks PlaceNodeMarks(FVector2D Hq, FVector2D Origin, double Size, int32 Count);
}
