#include "TeamPanelFeed.h"

#include "CommandGameState.h"
#include "Commands/GiftCommandComponent.h"
#include "Engine/World.h"

namespace
{
// Rows older than this are past their fade in the feed and can go.
constexpr float RowLifetime = UObjectiveAnnouncer::FeedLifetime + 1.f;
}

bool UGiftFeed::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

UGiftFeed* UGiftFeed::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UGiftFeed>() : nullptr;
}

void UGiftFeed::Observe(const ACommandGameState& State)
{
	const float Now = State.GetServerWorldTimeSeconds();
	TeamPanelPolicy::FLogBuffer Log;
	UGiftCommandComponent::ReadLog(State, Log);
	const float Latest = State.GiftLog.IsEmpty() ? -1.f : State.GiftLog.Last().ServerTime;
	// A log that went back in time belongs to a new match.
	if (!bPrimed || Latest < SeenThrough)
	{
		bPrimed = true;
		SeenThrough = Latest;
		FeedRows.Reset();
		return;
	}
	for (int32 Index = 0; Index < Log.Num(); ++Index)
	{
		const FGiftLogEntry& Gift = State.GiftLog[Index];
		if (Gift.ServerTime <= SeenThrough)
			continue;
		TStringBuilder<96> Title;
		TeamPanelPolicy::AppendGiftFeedText(Title, Log[Index]);
		FObjectiveEvent Row;
		Row.Id = FName(GiftFeed::RowId);
		Row.ServerTime = Gift.ServerTime;
		Row.Sequence = NextSequence++;
		Row.TargetForceOwnerName = FString(Title.ToView());
		if (FeedRows.Num() == MaxRows)
			FeedRows.RemoveAt(0);
		FeedRows.Add(MoveTemp(Row));
	}
	SeenThrough = FMath::Max(SeenThrough, Latest);
	FeedRows.RemoveAll([Now](const FObjectiveEvent& Row) { return Row.ServerTime > Now || Now - Row.ServerTime > RowLifetime; });
}
