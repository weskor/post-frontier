#pragma once

#include "CoreMinimal.h"

// What the HUD says about the economy under pressure: the supply-cut chip and a building's stun chip
// (docs: Design/ui.md, step 1b surfaces 1 and 6). Pure text and timing; the HUD supplies the replicated values.
namespace PressureHud
{
// A rate in the top bar: whole numbers print bare, anything else to one decimal ("2", "2.5").
void AppendRate(FStringBuilderBase& Out, double PerSecond);

// What a broken supply chain costs this commander: regions the team holds but no longer reaches, and its own
// share of the Power and Data they were paying.
struct FCutLoss
{
	int32 Regions = 0;
	double PowerPerSecond = 0.;
	double DataPerSecond = 0.;
	bool Any() const { return Regions > 0; }
};

// "LINE CUT ×2  −3 Power/s  −1 Data/s". Only the rates that are lost are listed.
void AppendCutChip(FStringBuilderBase& Out, const FCutLoss& Loss);

// Repeated clicks on the chip walk the cut regions in ascending order. Cut is ascending region indices; the result
// is the first above Last, wrapping to the first, or INDEX_NONE with nothing cut.
int32 NextCutFocus(TConstArrayView<int32> Cut, int32 Last);

// A building posts at most one feed row per this many seconds.
constexpr float StunFeedSeconds = 5.f;

struct FStunChip
{
	bool bShown = false;
	float Remaining = 0.f;
	// 1 at the stun's start, 0 when it ends.
	float Drain = 0.f;
};
// The chip for a building whose replicated stun ends at End; Peak is the remaining time when the stun began.
FStunChip StunChip(double Now, double End, float Peak);
// "STUN 2.4s". Tenths round up, so a live stun never reads 0.0s.
void AppendStunChip(FStringBuilderBase& Out, float Remaining);
// "Barracks 1 stunned by a Scrambler"; the number is the producer's force number, 0 for none.
void AppendStunFeed(FStringBuilderBase& Out, FStringView Building, int32 ForceNumber);

struct FStunObservation
{
	// This observation saw a stun that ends later than any seen before: a new or refreshed stun.
	bool bFresh = false;
	// A feed row is due for it.
	bool bFeed = false;
	// The stun's length at its start, for the drain bar.
	float Peak = 0.f;
};

// Per-building stun history on one client. The wire carries only the end time, so the start of a stun is the
// moment this client first saw that end.
class FStunWatch
{
public:
	FStunObservation Observe(uint32 Building, double Now, double End);
	// The remaining time at the start of the building's latest stun; 0 for one never seen.
	float Peak(uint32 Building) const;
	void Reset() { Entries.Reset(); }

private:
	struct FEntry
	{
		uint32 Building = 0;
		double End = -1.;
		float Peak = 0.f;
		double LastFeed = -1.e9;
	};
	TArray<FEntry> Entries;
};
}
