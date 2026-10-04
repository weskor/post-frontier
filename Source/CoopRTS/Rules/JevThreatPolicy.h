#pragma once

#include "CoreMinimal.h"
#include "JevPlanner.h"
#include "JevReleasePolicy.h"

// Split-Brain Cut, JEV's first two-commander threat (decision X1; docs: Design/battle.md, Design/jev.md). At v2.0 JEV
// sends two extra free assault forces at once to two authored, non-adjacent human supply necks, each funded with half
// the v2.0 wave budget, on top of the normal v2.0 wave. Both plans are published 30 s ahead. With one human it sends
// one force. Everything here is pure: the schedule, the authored pairs, the pair and target choice, the budget and the
// composition. The executor (EnemyCommanderWave.cpp, EnemyCommanderRelease.cpp) applies it.
namespace JevThreat
{
// The release the threat rides on (v2.0) and the commanders it needs for two targets.
constexpr int32 TriggerRelease = 3;
constexpr int32 CoopCommanders = 2;
constexpr int32 MaxTargets = 2;
constexpr int32 MaxPairs = 8;
constexpr float LeadSeconds = JevRelease::TimelineLeadSeconds;
// A cut force is JEV's assault squad: one full squad of the Assault unit (the Lancer, three) with a Frontline escort of
// EscortUnits (one Brawler). The mix is the strength tuned so that one full Brawler squad (six, tier 1) holding a neck
// loses it without Fortify and keeps it with Fortify; the measured sweep is in Docs/Design/battle.md. The funded budget is
// a ceiling that buys whole units.
constexpr int32 EscortUnits = 1;
// What the threat is called on the timeline, in the feed and in the log.
inline constexpr const TCHAR* Name = TEXT("Split-Brain Cut");
// The announcer event raised when the plans are published (Rules/AnnouncerPolicy).
inline constexpr const TCHAR* AnnouncerId = TEXT("split_brain_cut");
// An authored pair k tags both of its region actors "SplitBrain.k" (Build/GenerateAvailabilityZoneV2.py).
inline constexpr const TCHAR* TagPrefix = TEXT("SplitBrain.");

// Match seconds at which the plans are published (the timeline lead before the release) and the forces launch.
float PublishTime();
float LaunchTime();

enum class EStage : uint8
{
	Waiting,
	// The plans are out; the forces have not launched.
	Published,
	// Launched, or skipped with a logged reason: the threat happens once per battle.
	Done
};
enum class EStep : uint8
{
	None,
	Publish,
	Launch
};
// What is due at MatchSeconds. A clock past the launch time publishes first and then launches, so a plan is never
// skipped; Done never steps.
EStep NextStep(EStage Stage, float MatchSeconds);

// Forces sent: two with two or more human commanders, else one.
int32 TargetCount(int32 HumanCommanders);
// The budget of one cut force: half the v2.0 wave budget for these commanders.
int32 ForceBudget(int32 HumanCommanders);

struct FPair
{
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;
};
struct FTaggedRegion
{
	int32 Region = INDEX_NONE;
	int32 Pair = INDEX_NONE;
};
// k for the tag "SplitBrain.k" with 0 <= k < MaxPairs, else INDEX_NONE.
int32 PairOfTag(FStringView Tag);
// The authored pairs, in tag order. A tag held by other than two distinct regions names no pair.
TArray<FPair> BuildPairs(TConstArrayView<FTaggedRegion> Tagged);

// Whether the planner's world has two existing, distinct regions A and B that border each other.
bool Adjacent(const JevPlanner::FWorld& World, int32 A, int32 B);
// A human supply neck: a non-main region strictly nearer (in hops) the humans' main than JEV's whose loss lengthens or
// cuts the humans' shortest hop path to some other region. Habitable Zone v2's pairs are audited by the same rule
// (Build/DrawAvailabilityZoneV2.py human_supply_necks).
bool IsSupplyNeck(const JevPlanner::FWorld& World, int32 Region);

enum class ESkip : uint8
{
	None,
	// The map authors no pair.
	NoPairs,
	// Every authored pair is malformed or holds a region JEV controls.
	NoEligiblePair
};
const TCHAR* SkipReason(ESkip Skip);

struct FChoice
{
	ESkip Skip = ESkip::NoPairs;
	FPair Pair;
	// Regions of the pair the humans control now, 0 to 2.
	int32 Held = 0;
	// No pair is wholly in human hands: the closest eligible one was taken.
	bool bFallback = false;
};
// A pair is eligible when its regions are distinct, non-adjacent supply necks and JEV controls neither. Of the eligible
// pairs the one the humans hold most of wins, then the one whose regions lie fewest hops from the humans' main, then
// authored order.
FChoice ChoosePair(const JevPlanner::FWorld& World, TConstArrayView<FPair> Pairs);

struct FTargets
{
	int32 Region[MaxTargets] = { INDEX_NONE, INDEX_NONE };
	int32 Count = 0;
};
// Both regions of the pair with two or more humans. Alone, the one region of the pair the humans hold, else the one
// nearer their main, else A.
FTargets ChooseTargets(const JevPlanner::FWorld& World, const FPair& Pair, int32 HumanCommanders);

// What one cut force buys with Budget: Lancers first, up to AssaultSquad (the catalogue squad size), then the escort from
// what is left. Whatever the budget does not buy is not carried. A unit that costs nothing is never bought.
struct FComposition
{
	int32 Assault = 0;
	int32 Escort = 0;
	int32 Units() const { return Assault + Escort; }
};
FComposition Compose(int32 Budget, int32 AssaultCost, int32 AssaultSquad, int32 EscortCost);
}
