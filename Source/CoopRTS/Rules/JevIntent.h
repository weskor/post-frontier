#pragma once

#include "CoreMinimal.h"
#include "JevPlanner.h"

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
	uint32 StructureIdentity = 0;
	int32 SizeBand = 2;
	float EtaSeconds = 0.f;
	// Server time at which EtaSeconds was computed; the countdown runs from there.
	float EtaIssuedAt = 0.f;
	bool bEscalated = false;
	FStringView Memo;
};

// The timeline takes more entry kinds (version releases, calldowns) in later steps.
enum class EEntryKind : uint8
{
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
};

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
// Soonest first; ties by ticket, then force number.
void BuildTimeline(TConstArrayView<FPlanView> Plans, float Now, FTimeline& Out);
// One badge per targeted region, ascending by region index.
void BuildBadges(TConstArrayView<FPlanView> Plans, float Now, FBadges& Out);

// Posts a plan's memo when the plan is first seen or its announcement changes: ticket,
// verb, target, structure, size band or escalation. ETA drift alone is not a new memo.
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
		uint32 StructureIdentity = 0;
		int32 SizeBand = 0;
		bool bEscalated = false;
		bool Matches(const FPlanView& Plan) const;
	};
	static FAnnounced Describe(const FPlanView& Plan);
	TArray<FAnnounced> Known;
	TArray<FMemo> Memos;
};
}
