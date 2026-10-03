#include "JevIntent.h"

namespace JevIntent
{
float EtaRemaining(const FPlanView& Plan, float Now)
{
	return FMath::Max(0.f, Plan.EtaSeconds - FMath::Max(0.f, Now - Plan.EtaIssuedAt));
}

void AppendCountdown(FStringBuilderBase& Out, float Seconds)
{
	const int32 Whole = FMath::Max(0, FMath::CeilToInt(Seconds));
	Out.Appendf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}

void BuildTimeline(TConstArrayView<FPlanView> Plans, float Now, FTimeline& Out)
{
	Out.Reset();
	for (const FPlanView& Plan : Plans)
	{
		FTimelineEntry& Entry = Out.AddDefaulted_GetRef();
		Entry.Ticket = Plan.Ticket;
		Entry.ForceNumber = Plan.ForceNumber;
		Entry.Verb = Plan.Verb;
		Entry.Target = Plan.Target;
		Entry.SizeBand = Plan.SizeBand;
		Entry.Seconds = EtaRemaining(Plan, Now);
		Entry.bEscalated = Plan.bEscalated;
	}
	Out.Sort([](const FTimelineEntry& A, const FTimelineEntry& B) {
		if (A.Seconds != B.Seconds)
			return A.Seconds < B.Seconds;
		return A.Ticket != B.Ticket ? A.Ticket < B.Ticket : A.ForceNumber < B.ForceNumber;
	});
}

void BuildBadges(TConstArrayView<FPlanView> Plans, float Now, FBadges& Out)
{
	Out.Reset();
	for (const FPlanView& Plan : Plans)
	{
		if (Plan.Target == INDEX_NONE)
			continue;
		const float Seconds = EtaRemaining(Plan, Now);
		FRegionBadge* Badge = Out.FindByPredicate([&](const FRegionBadge& Entry) { return Entry.Region == Plan.Target; });
		if (!Badge)
		{
			Badge = &Out.AddDefaulted_GetRef();
			Badge->Region = Plan.Target;
			Badge->Seconds = TNumericLimits<float>::Max();
		}
		++Badge->Plans;
		Badge->bEscalated |= Plan.bEscalated;
		if (Seconds < Badge->Seconds || (Seconds == Badge->Seconds && Plan.Ticket < Badge->Ticket))
		{
			Badge->Seconds = Seconds;
			Badge->Ticket = Plan.Ticket;
			Badge->Verb = Plan.Verb;
		}
	}
	Out.Sort([](const FRegionBadge& A, const FRegionBadge& B) { return A.Region < B.Region; });
}

bool FMemoFeed::FAnnounced::Matches(const FPlanView& Plan) const
{
	return Ticket == Plan.Ticket && Verb == Plan.Verb && Target == Plan.Target
		&& StructureIdentity == Plan.StructureIdentity && SizeBand == Plan.SizeBand && bEscalated == Plan.bEscalated;
}

FMemoFeed::FAnnounced FMemoFeed::Describe(const FPlanView& Plan)
{
	return { Plan.Force, Plan.Ticket, Plan.Verb, Plan.Target, Plan.StructureIdentity, Plan.SizeBand, Plan.bEscalated };
}

int32 FMemoFeed::Observe(TConstArrayView<FPlanView> Plans, float Now)
{
	int32 Posted = 0;
	for (const FPlanView& Plan : Plans)
	{
		FAnnounced* Seen = Known.FindByPredicate([&](const FAnnounced& Entry) { return Entry.Force == Plan.Force; });
		if (Seen && Seen->Matches(Plan))
			continue;
		if (Seen)
			*Seen = Describe(Plan);
		else
			Known.Add(Describe(Plan));
		// A plan whose template failed to format has no memo to post; it is still recorded as seen.
		if (Plan.Memo.IsEmpty())
			continue;
		if (Memos.Num() == MemoHistory)
			Memos.RemoveAt(0);
		Memos.Add({ Plan.Ticket, Plan.ForceNumber, FString(Plan.Memo.Len(), Plan.Memo.GetData()), Now });
		++Posted;
	}
	Known.RemoveAllSwap([&](const FAnnounced& Entry) {
		return !Plans.ContainsByPredicate([&](const FPlanView& Plan) { return Plan.Force == Entry.Force; });
	});
	return Posted;
}

void FMemoFeed::Visible(float Now, FVisibleMemos& Out) const
{
	Out.Reset();
	for (int32 Index = Memos.Num() - 1; Index >= 0 && Out.Num() < MemoVisible; --Index)
	{
		const float Age = FMath::Max(0.f, Now - Memos[Index].PostedAt);
		if (Age >= MemoHoldSeconds + MemoFadeSeconds)
			continue;
		Out.Add({ &Memos[Index], FMath::Clamp((MemoHoldSeconds + MemoFadeSeconds - Age) / MemoFadeSeconds, 0.f, 1.f) });
	}
}

void FMemoFeed::Reset()
{
	Known.Reset();
	Memos.Reset();
}
}
