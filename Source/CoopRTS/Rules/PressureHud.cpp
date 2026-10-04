#include "PressureHud.h"

namespace PressureHud
{
namespace
{
// Beyond this a later end time is a new stun, not replication jitter on the same one.
constexpr double FreshEpsilon = .05;
}

void AppendRate(FStringBuilderBase& Out, double PerSecond)
{
	const int32 Tenths = FMath::RoundToInt(PerSecond * 10.);
	if (Tenths % 10 == 0)
		Out.Appendf(TEXT("%d"), Tenths / 10);
	else
		Out.Appendf(TEXT("%.1f"), Tenths / 10.);
}

void AppendCutChip(FStringBuilderBase& Out, const FCutLoss& Loss)
{
	Out.Appendf(TEXT("LINE CUT \u00D7%d"), Loss.Regions);
	if (Loss.PowerPerSecond > 0.)
	{
		Out << TEXT("  \u2212");
		AppendRate(Out, Loss.PowerPerSecond);
		Out << TEXT(" Power/s");
	}
	if (Loss.DataPerSecond > 0.)
	{
		Out << TEXT("  \u2212");
		AppendRate(Out, Loss.DataPerSecond);
		Out << TEXT(" Data/s");
	}
}

int32 NextCutFocus(TConstArrayView<int32> Cut, int32 Last)
{
	for (const int32 Region : Cut)
		if (Region > Last)
			return Region;
	return Cut.IsEmpty() ? INDEX_NONE : Cut[0];
}

FStunChip StunChip(double Now, double End, float Peak)
{
	FStunChip Chip;
	if (End <= Now)
		return Chip;
	Chip.bShown = true;
	Chip.Remaining = static_cast<float>(End - Now);
	Chip.Drain = Peak > 0.f ? FMath::Clamp(Chip.Remaining / Peak, 0.f, 1.f) : 1.f;
	return Chip;
}

void AppendStunChip(FStringBuilderBase& Out, float Remaining)
{
	const int32 Tenths = FMath::Max(1, FMath::CeilToInt(Remaining * 10.f - .001f));
	Out.Appendf(TEXT("STUN %d.%ds"), Tenths / 10, Tenths % 10);
}

void AppendStunFeed(FStringBuilderBase& Out, FStringView Building, int32 ForceNumber)
{
	Out << Building;
	if (ForceNumber > 0)
		Out.Appendf(TEXT(" %d"), ForceNumber);
	Out << TEXT(" stunned by a Scrambler");
}

FStunObservation FStunWatch::Observe(uint32 Building, double Now, double End)
{
	FEntry* Entry = Entries.FindByPredicate([Building](const FEntry& Candidate) { return Candidate.Building == Building; });
	if (End <= Now || (Entry && End <= Entry->End + FreshEpsilon))
		return { false, false, Entry ? Entry->Peak : 0.f };
	if (!Entry)
	{
		Entry = &Entries.AddDefaulted_GetRef();
		Entry->Building = Building;
	}
	Entry->End = End;
	Entry->Peak = static_cast<float>(End - Now);
	const bool bFeed = Now - Entry->LastFeed >= StunFeedSeconds;
	if (bFeed)
		Entry->LastFeed = Now;
	return { true, bFeed, Entry->Peak };
}

float FStunWatch::Peak(uint32 Building) const
{
	const FEntry* Entry = Entries.FindByPredicate([Building](const FEntry& Candidate) { return Candidate.Building == Building; });
	return Entry ? Entry->Peak : 0.f;
}
}
