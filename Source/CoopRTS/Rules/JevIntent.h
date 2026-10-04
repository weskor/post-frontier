#pragma once

#include "CoreMinimal.h"
#include "JevPlanner.h"
#include "JevReleasePolicy.h"

// What the HUD shows of JEV's published plans. Everything here is a function of the
// replicated plan list and the server clock; nothing is predicted or guessed.
namespace JevIntent
{
constexpr int32 TimelineEntries = 4;
constexpr int32 MemoVisible = 3;
constexpr int32 MemoHistory = 8;
// Starting values: a memo is readable for 12 s, then fades over 2 s.
constexpr float MemoHoldSeconds = 12.f;
constexpr float MemoFadeSeconds = 2.f;

// One published plan as the display consumes it. Memo views the replicated string.
struct FPlanView
{
	int32 Ticket = 0;
	uint32 Force = 0;
	int32 ForceNumber = 0;
	JevPlanner::EVerb Verb = JevPlanner::EVerb::MoveAndHold;
	int32 Target = INDEX_NONE;
	int32 SizeBand = 2;
	float EtaSeconds = 0.f;
	// Server time at which EtaSeconds was computed; the countdown runs from there.
	float EtaIssuedAt = 0.f;
	bool bEscalated = false;
	FStringView Memo;
};

// A version release shows as the first cell for its lead time; plans follow, soonest first.
enum class EEntryKind : uint8
{
	Release,
	Plan
};

struct FTimelineEntry
{
	EEntryKind Kind = EEntryKind::Plan;
	int32 Ticket = 0;
	int32 ForceNumber = 0;
	JevPlanner::EVerb Verb = JevPlanner::EVerb::MoveAndHold;
	int32 Target = INDEX_NONE;
	int32 SizeBand = 2;
	float Seconds = 0.f;
	bool bEscalated = false;
	// Release cells: the release index (0 is v1.0) and, for v1.2, the armor class the wave will counter.
	int32 Release = INDEX_NONE;
	EArmorClass CounterArmor = EArmorClass::Unset;
};

// JEV's published release schedule and the facts a release tag reads, as the display sees them.
// Match time is GetServerWorldTimeSeconds() - ClockStartServerTime, the clock JEV's schedule runs on.
struct FReleaseView
{
	// False until JEV's actor exists on this client; the display then knows no release.
	bool bKnown = false;
	int32 Current = 0;
	int32 Next = 1;
	float NextAt = 0.f;
	float ClockStartServerTime = 0.f;
	// The armor class JEV publishes that its v1.2 wave counters (the humans' most numerous at its last evaluation); kept
	// while v1.2 is in force so the release's feed row names it. Unset otherwise.
	EArmorClass CounterArmor = EArmorClass::Unset;
};

// Seconds since the match started on JEV's clock; 0 while the schedule is unknown or the clock is ahead.
float MatchSeconds(const FReleaseView& Release, float Now);
// Seconds until the next release, 0 once it is due.
float NextReleaseIn(const FReleaseView& Release, float Now);
// The next release shows as a cell for its last JevRelease::TimelineLeadSeconds, until the schedule moves on.
bool ReleaseCellShown(const FReleaseView& Release, float Now);
// "v1.1", "v2.0", "overrun".
void AppendReleaseName(FStringBuilderBase& Out, int32 Release);
// The release's tag from what it adds: "WAVE · RAIDS DRILL RIGS", "COUNTERS HEAVY", "ALL FORCES ATTACK",
// "WAVES +15% SPEED", "OVERRUN". Empty for v1.0, which adds nothing.
void AppendReleaseTag(FStringBuilderBase& Out, int32 Release, EArmorClass CounterArmor);
// Whole minutes and seconds counting up, rounded down: "4:12".
void AppendElapsed(FStringBuilderBase& Out, float Seconds);
// "No JEV plans · next release v1.1 in 1:42", or "No JEV plans" until the schedule replicates.
void AppendEmptyTimeline(FStringBuilderBase& Out, const FReleaseView& Release, float Now);

struct FRegionBadge
{
	int32 Region = INDEX_NONE;
	// The soonest plan targeting the region.
	int32 Ticket = 0;
	JevPlanner::EVerb Verb = JevPlanner::EVerb::MoveAndHold;
	float Seconds = 0.f;
	int32 Plans = 0;
	// True when any plan targeting the region is a defense of it.
	bool bEscalated = false;
};

struct FMemo
{
	int32 Ticket = 0;
	int32 ForceNumber = 0;
	FString Text;
	float PostedAt = 0.f;
};

struct FVisibleMemo
{
	const FMemo* Memo = nullptr;
	float Alpha = 1.f;
};

using FTimeline = TArray<FTimelineEntry, TInlineAllocator<16>>;
using FBadges = TArray<FRegionBadge, TInlineAllocator<16>>;
using FVisibleMemos = TArray<FVisibleMemo, TInlineAllocator<MemoVisible>>;

// Seconds until arrival at Now; 0 once the ETA has elapsed.
float EtaRemaining(const FPlanView& Plan, float Now);
// Rounds up to whole seconds as the memo templates do: "0:30", "12:05".
void AppendCountdown(FStringBuilderBase& Out, float Seconds);
// The release cell (when shown) first, then every plan soonest first; ties by ticket, then force number.
void BuildTimeline(TConstArrayView<FPlanView> Plans, const FReleaseView& Release, float Now, FTimeline& Out);
// One badge per targeted region, ascending by region index.
void BuildBadges(TConstArrayView<FPlanView> Plans, float Now, FBadges& Out);

// Posts a plan's memo when the plan is first seen or the part of it the memo prints changes: ticket,
// verb, target region, size band or escalation. ETA drift or a changed target structure is not a new memo.
class FMemoFeed
{
public:
	// Returns the number of memos posted.
	int32 Observe(TConstArrayView<FPlanView> Plans, float Now);
	// Newest first, at most MemoVisible, with fade alpha.
	void Visible(float Now, FVisibleMemos& Out) const;
	// Oldest first.
	TConstArrayView<FMemo> History() const { return Memos; }
	void Reset();

private:
	struct FAnnounced
	{
		uint32 Force = 0;
		int32 Ticket = 0;
		JevPlanner::EVerb Verb = JevPlanner::EVerb::MoveAndHold;
		int32 Target = INDEX_NONE;
		int32 SizeBand = 0;
		bool bEscalated = false;
		bool Matches(const FPlanView& Plan) const;
	};
	static FAnnounced Describe(const FPlanView& Plan);
	TArray<FAnnounced> Known;
	TArray<FMemo> Memos;
};
}
