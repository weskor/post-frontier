#pragma once

#include "CoreMinimal.h"

// Fortify: the generic region ability every commander has in step 1b (decision F1). Rules only: the
// authority (FCommandService::CastFortify), the region actor and the HUD read these, never copies.
namespace FortifyPolicy
{
inline constexpr int32 DataCost = 40;
// Per commander, counted from the cast.
inline constexpr float CooldownSeconds = 90.f;
// The effect lasts this long from the latest cast on the region.
inline constexpr float DurationSeconds = 60.f;
// One entry of the DamagePolicy incoming list, taken by the casting team's units and buildings.
inline constexpr float IncomingMultiplier = .75f;
// JEV has no Fortify; its planner scores a Fortified hostile region's defence by this factor.
inline constexpr float JevDefenceMultiplier = 1.33f;
// The badge's drain bar pulses for this long before the effect ends.
inline constexpr float WarningSeconds = 10.f;

// A region's published Fortify: the team it protects and the server time it ends. Team -1 is none.
struct FRegionState
{
	int32 Team = -1;
	float ExpiresAt = 0.f;
};

enum class EVerdict : uint8
{
	Accepted,
	NoBattle,
	NotCommander,
	NoRegion,
	Neutral,
	HeldByEnemy,
	Cooldown,
	NeedData
};

struct FCastInput
{
	bool bBattleLive = false;
	// A human commander of team 0; JEV has no Fortify.
	bool bCommander = false;
	bool bRegionExists = false;
	int32 CasterTeam = -1;
	// The team that controls the region, -1 for neutral. A contested region keeps its controller.
	int32 RegionController = -1;
	FRegionState Region;
	int32 Data = 0;
	float Now = 0.f;
	// Server time at which the caster's cooldown ends.
	float ReadyAt = 0.f;
};

struct FDecision
{
	EVerdict Verdict = EVerdict::NoBattle;
	// Data still missing, for NeedData.
	int32 DataShort = 0;
	// Seconds until the caster's cooldown ends, for Cooldown.
	float CooldownLeft = 0.f;
	// The cast would refresh a Fortify the caster's team already holds on the region.
	bool bRefresh = false;
	// Seconds left on that Fortify, for bRefresh.
	float RefreshLeft = 0.f;
	bool IsAccepted() const { return Verdict == EVerdict::Accepted; }
};

enum class EEnd : uint8
{
	None,
	Expired,
	RegionLost
};

bool IsActive(const FRegionState& Region, float Now);
// The first failing rule wins, in the order of EVerdict. Refresh is allowed and reported, never refused.
FDecision Evaluate(const FCastInput& In);
// The region state a cast at Now produces: the caster's team, expiring DurationSeconds later.
FRegionState Cast(int32 Team, float Now);
// Server time at which the caster may cast again.
float CooldownEnd(float Now);
// Why an active Fortify ends at Now: it ran out, or its team no longer controls the region.
EEnd Review(const FRegionState& Region, int32 RegionController, float Now);
// 0.75 for a unit or building of the Fortified team standing in the region while it is active, else 1.
float IncomingFor(const FRegionState& Region, int32 VictimTeam, float Now);
// Whether the capture point of the region is frozen: any active Fortify freezes it for both sides.
bool FreezesCapture(const FRegionState& Region, float Now);
// 1.33 for a region Fortified by a team other than the planner's; 1 otherwise.
float DefenceFor(const FRegionState& Region, int32 PlannerTeam, float Now);
// Seconds left on an active Fortify, 0 when none.
float SecondsLeft(const FRegionState& Region, float Now);
// "m:ss", rounded up to the second shown. The one clock formatter for the dock, chip and badge.
void AppendClock(FStringBuilderBase& Out, float Seconds);
// "Need 12 more Data".
void AppendDataShort(FStringBuilderBase& Out, int32 DataShort);
// Appends the text the chip and the rejection show: "Brig is neutral", "Need 12 more Data", "Cooldown 0:47".
void AppendReason(FStringBuilderBase& Out, const FDecision& Decision, FStringView RegionName);
// A region the rules allow in principle: only the cooldown and the Data balance hold a cast back, not the region.
// Targeting highlights these; the chip explains the rest.
bool IsValidTarget(EVerdict Verdict);

enum class EDockState : uint8
{
	Ready,
	Cooldown,
	NeedData
};

// The dock button's state, independent of any region: the cooldown shows first, then the Data shortfall.
struct FDock
{
	EDockState State = EDockState::Ready;
	float CooldownLeft = 0.f;
	int32 DataShort = 0;
};
FDock Dock(float Now, float ReadyAt, int32 Data);

// Feed rows are team rows (ui.md): a teammate's cast and an early end. Ids tell the HUD which text to build.
inline constexpr const TCHAR* CastEventId = TEXT("fortify_cast");
inline constexpr const TCHAR* EndedEventId = TEXT("fortify_ended");
// "Commander 2 fortified Fusion Works" (CommanderSlot is zero-based).
void AppendCastFeedText(FStringBuilderBase& Out, int32 CommanderSlot, FStringView RegionName);
// "Fortify at Fusion Works ended: region lost".
void AppendEndedFeedText(FStringBuilderBase& Out, FStringView RegionName);
}
