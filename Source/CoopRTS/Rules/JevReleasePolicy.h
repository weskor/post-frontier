#pragma once

#include "CoreMinimal.h"
#include "CombatPolicy.h"
#include "JevPlanner.h"

// JEV's version releases and the free waves they send (docs: Design/jev.md, Releases).
// Everything here is pure: the clock, the budget arithmetic, the purchase and the
// per-release behaviour. The executor (EnemyCommanderRelease/Wave.cpp) applies it.
namespace JevRelease
{
// v1.0 .. v2.1 sit at fixed times; from OverrunStartSeconds a release comes every OverrunIntervalSeconds.
constexpr int32 ScheduledReleases = 5;
constexpr float OverrunStartSeconds = 600.f;
constexpr float OverrunIntervalSeconds = 60.f;
// A release shows on the timeline this long before it happens.
constexpr float TimelineLeadSeconds = 30.f;
constexpr int32 OverrunBudget = 300;
constexpr float RapidSpeedFactor = 1.15f;
constexpr int32 MaxForceSize = 6;
// Node depth is 1 in step 1b, so depth scaling contributes nothing yet.

enum class EVersion : uint8
{
	V10,
	V11,
	V12,
	V20,
	V21,
	Overrun
};

// Release indices count from 0 (v1.0). Times are match seconds.
float ReleaseTime(int32 Index);
EVersion VersionOf(int32 Index);
// The latest release at or before Seconds; INDEX_NONE before the match clock reaches 0.
int32 IndexAt(float Seconds);
// Whether a release at index Index already shows on JEV's timeline at Seconds.
bool TimelineVisible(int32 Index, float Seconds);
// Power-equivalent wave budget for N = 1 (zero for v1.0).
int32 BaseBudget(int32 Index);
// BaseBudget times the player-count factor for HumanCommanders, rounded to whole Power.
int32 WaveBudget(int32 Index, int32 HumanCommanders);

// Behaviours accumulate: each release keeps everything the earlier ones added.
struct FBehaviour
{
	// v1.1: the wave raids the nearest connected human Drill Rig region and buys the cheapest units.
	bool bRaid = false;
	// v1.2: the wave buys units that counter the humans' most numerous armor class.
	bool bCounter = false;
	// v2.0: every JEV force attacks the wave's target together with the wave.
	bool bCoordinated = false;
	// v2.1: wave units move this much faster.
	float SpeedFactor = 1.f;
};
FBehaviour BehaviourFor(int32 Index);

struct FUnitOption
{
	int32 Cost = 0;
	EArmorClass Armor = EArmorClass::Unset;
	EDamageType Damage = EDamageType::Unset;
};

// Humans' living units per armor class, indexed by EArmorClass.
struct FArmorCounts
{
	int32 Count[4] = {};
};
// The class with the most units; ties go to the earlier class, no units gives Unset.
EArmorClass MostNumerous(const FArmorCounts& Counts);

struct FPurchase
{
	// Units bought per option, parallel to the options passed in.
	TArray<int32> Counts;
	int32 Units = 0;
	int32 Spent = 0;
	// Pool left over, carried to the next wave.
	int32 Carry = 0;
};
// Buys whole units from Pool. The release's preferred unit comes first (the cheapest, or with
// bCounter the cheapest option strong against Target), then the cheapest option fills the rest.
// Options with a non-positive cost are never bought.
FPurchase Purchase(int32 Pool, TConstArrayView<FUnitOption> Options, const FBehaviour& Behaviour, EArmorClass Target);

// Force sizes for a wave of UnitCount units: as few forces as possible, none above MaxForceSize,
// as even as possible with the larger ones first.
TArray<int32> SplitForces(int32 UnitCount);

// The region a raiding wave attacks: the nearest region (fewest hops from World.Home, then
// straight-line distance, then index) holding a hostile Drill Rig that the hostile main still
// connects to. With none, the nearest hostile-controlled connected region, else World.EnemyHome.
int32 RaidRegion(const JevPlanner::FWorld& World);
}
