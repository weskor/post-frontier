#pragma once

#include "CoreMinimal.h"
#include "RegionTraitPolicy.h"

// What the map shows of the step 1b state: supply cuts, the Scrambler pulse and region traits (ui.md surfaces 2, 6, 7).
// Decisions only: which cable is drawn how, when a flash plays, where each plate part sits. The HUD and the world overlay
// read these; nothing here touches an actor.
namespace MapPresentation
{
// A client's estimate of server time may trail a replicated stamp by this much; an earlier stamp has not happened yet.
inline constexpr float ClockSlackSeconds = .25f;

// A cut flashes the region's border FlashCount times within FlashSeconds, on a clock the client starts when it first
// sees the cut; a change seen up to FlashWindowSeconds after it happened still plays, an older one shows only the steady
// state.
inline constexpr float FlashWindowSeconds = 3.f;
inline constexpr float FlashSeconds = 1.f;
inline constexpr int32 FlashCount = 3;
// The snapped cable's spark fades over this long from the change.
inline constexpr float SparkSeconds = .5f;

// Seconds since a replicated change time (server world seconds); negative when it never changed (time 0) or has not
// happened yet.
float ChangeAge(float Now, float ChangedAt);
bool FlashPlays(float Now, float ChangedAt);
// Whether the border is lit at this age (seconds since the client started the flash): lit for the first half of each of the FlashCount cycles, dark after FlashSeconds.
bool FlashLit(float Age);
float SparkAlpha(float Age);

// A region as a team's supply chain sees it. Connected regions are controlled by definition.
struct FRegionLink
{
	bool bControlled = false;
	bool bConnected = false;
};

// Controlled but not connected: cut off from the team's main.
bool IsCutOff(const FRegionLink& Region);

enum class ECable : uint8
{
	None,
	// Between two connected regions: team colour.
	Live,
	// Between two cut-off regions: grey dashes.
	Beyond,
	// The stub left where a cable that was live broke: a cut-off region next to a region the team lost. The observer
	// decides which edges these are (FCutObserver); the region states alone cannot tell a lost cable from none.
	Snapped
};
ECable ClassifyCable(const FRegionLink& A, const FRegionLink& B);

// The team feed row's event id.
inline constexpr const TCHAR* SupplyCutEventId = TEXT("supply_cut");

// Regions that left a team's connected mask while it still controls them: the ones a feed row announces. Regions lost to
// an enemy are not cut off, they are lost. A mask with no region at all means the main fell, which ends the battle
// rather than cutting anything.
uint64 NewlyCutOff(uint64 PreviousConnected, uint64 Connected, uint64 Controlled);

// What one client has seen of a team's supply chain. The replicated change time is team-wide and moves on every change, so
// the client compares each new mask with the last one it observed to find the regions that were just cut: only those
// flash and spark, and only the cables that were live between them and the lost neighbour snap.
inline constexpr int32 ObservedRegions = 64;
struct FCutObserver
{
	bool bSeen = false;
	uint64 PreviousConnected = 0;
	float SeenChangedAt = 0.f;
	// Local flash clock start per region, or negative when it is not flashing.
	float Started[ObservedRegions];
	// Per cut-off region, the neighbours it was linked to when the cut was seen: where cables were live (or, for a client
	// that joined after the cut, where the opponent now holds). A cable snapped when that neighbour is no longer held,
	// judged on every frame because the mask and the region owners replicate separately and may arrive in either order.
	uint64 CutFrom[ObservedRegions] = {};
	uint64 LastHeld = 0;
	uint64 LastCutOff = 0;
	FCutObserver()
	{
		for (float& Start : Started)
			Start = -1.f;
	}
	// Seconds since the region's flash started, or negative when it is steady.
	float FlashAge(int32 Region, float Now) const;
	bool IsSnapped(int32 CutRegion, int32 OtherRegion) const;
};
struct FObservation
{
	uint64 Connected = 0;
	// Held and not connected.
	uint64 CutOff = 0;
	uint64 Held = 0;
	// Regions the opposing team holds: the best guess for a lost cable when the client joined after the cut.
	uint64 Opponent = 0;
	// Per region, the bit mask of its neighbours.
	const uint64* Neighbours = nullptr;
	float ChangedAt = 0.f;
	float Now = 0.f;
};
void Observe(FCutObserver& Observer, const FObservation& In);

// The Scrambler pulse ring grows from the unit to the pulse radius over RingSeconds, then fades over FadeSeconds. A hit
// shield bar or building sparks for SparkSeconds once the ring reaches it.
inline constexpr float PulseRingSeconds = .4f;
inline constexpr float PulseFadeSeconds = .15f;
inline constexpr float PulseSparkSeconds = .2f;

struct FPulseRing
{
	bool bActive = false;
	float Radius = 0.f;
	float Alpha = 0.f;
};
// Seconds since a cast (negative when it never pulsed, has not happened yet, or has fully played out).
float PulseAge(float Now, float CastTime);
FPulseRing PulseRing(float Age, float FullRadius);
// Spark opacity for a target Distance from the Scrambler: zero until the ring reaches it. 0 when not active.
float PulseSparkAlpha(float Age, float Distance, float FullRadius);

// Trait plate sizes, virtual pixels. 9.5 px keeps the word above the 7.1 px caption floor at the 0.78 minimum HUD scale.
inline constexpr float TraitGlyphSize = 20.f;
inline constexpr float TraitWordSize = 9.5f;
inline constexpr float MinimapGlyphSize = 10.f;
// The minimap node's Fortify ring, which the glyph must clear.
inline constexpr float MinimapNodeClearRadius = 9.f;

// Capitals under the glyph; empty for a region without a trait.
const TCHAR* TraitWord(ERegionTrait Trait);

struct FGlyphSegment
{
	FVector2D A;
	FVector2D B;
};
// Silhouettes in the unit square (+Y down): chevron, brick, double arrow, warning triangle. Empty for None.
TConstArrayView<FGlyphSegment> TraitGlyph(ERegionTrait Trait);
// Two chain links with a break between them.
TConstArrayView<FGlyphSegment> ChainBreakGlyph();

// Top-left of the trait glyph on the minimap for a node centred at (0, 0), in minimap pixels: clear of the Fortify ring,
// the JEV marker and countdown (right), and the deposit ticks (bottom-left).
FVector2D MinimapGlyphOrigin();

// The region stack above a region's anchor, relative to the projected anchor point, top to bottom: the JEV badge (not
// drawn here), the label plate carrying the glyph, name, trait word and capture bar, then the chip rows.
struct FPlateInput
{
	float NameWidth = 0.f;
	float NameLine = 0.f;
	float WordWidth = 0.f;
	float WordLine = 0.f;
	bool bTrait = false;
};
struct FRectF
{
	float X = 0.f;
	float Y = 0.f;
	float W = 0.f;
	float H = 0.f;
	float Right() const { return X + W; }
	float Bottom() const { return Y + H; }
	bool Intersects(const FRectF& Other) const
	{
		return X < Other.Right() && Right() > Other.X && Y < Other.Bottom() && Bottom() > Other.Y;
	}
};
struct FPlate
{
	FRectF Plate;
	FRectF Glyph;
	FRectF Name;
	FRectF Word;
	FRectF Bar;
	// Where the first chip row starts: below the plate, which grows downward so the badge above never moves.
	float ChipTop = 0.f;
};
// The plate's top edge sits this far above the anchor point whatever it carries, so the JEV badge stays put.
float PlateTopAboveAnchor(float NameLine);
FPlate LayoutPlate(const FPlateInput& In);

// Chip rows under the plate, in this order, with the gap between them.
enum class EChip : uint8
{
	CutOff,
	Fortified
};
inline constexpr float CutOffChipHeight = 18.f;
inline constexpr float FortifiedChipHeight = 28.f;
inline constexpr float ChipGap = 3.f;
float ChipHeight(EChip Chip);
// Top of Chip in the stack of present chips (in EChip order) that starts at FirstTop; -1 for an absent chip.
float ChipTop(float FirstTop, bool bCutOff, bool bFortified, EChip Chip);

// Diagonal hatch (45 degrees) clipped to a polygon: every line whose x + y equals a multiple of Spacing is cut at the
// polygon's edges and visited as segments inside it.
void ForEachHatch(TConstArrayView<FVector2D> Polygon, float Spacing, TFunctionRef<void(const FVector2D&, const FVector2D&)> Visit);

// Feed row and deposit label text (ui.md surfaces 2 and 3).
void AppendCutFeedText(FStringBuilderBase& Out, FStringView Region, int32 RigsOffline);
void AppendOfflineDepositLabel(FStringBuilderBase& Out, bool bRich, int32 Remaining);
}
