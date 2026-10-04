#pragma once

#include "CoreMinimal.h"
#include "ObjectiveAnnouncer.h"
#include "Subsystems/WorldSubsystem.h"
#include "TeamPanelFeed.generated.h"

class ACommandGameState;

// Gift feed rows are local, like the pressure rows: the wire carries the team gift log, not events. Their sequences sit
// between every replicated ring's and the pressure rows', so a click on one can only mean "open the Team panel".
namespace GiftFeed
{
inline constexpr int32 SequenceBase = MAX_int32 / 4;
inline bool IsGiftSequence(int32 Sequence) { return Sequence >= SequenceBase && Sequence < MAX_int32 / 2; }
inline constexpr const TCHAR* RowId = TEXT("team_gift");
}

// The feed rows of accepted gifts ("Commander 2 gifted 100 Power to Commander 1", gold stripe, FeedLifetime). Derived
// from the replicated gift log: a gift already logged when this client first looked is history, not news.
UCLASS()
class COOPRTS_API UGiftFeed : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	static UGiftFeed* Get(const UObject* Context);

	// Once a tick from the local controller, before the HUD reads the rows.
	void Observe(const ACommandGameState& State);
	// Newest last; each row's title is in TargetForceOwnerName and its Location is the origin.
	const TArray<FObjectiveEvent>& Rows() const { return FeedRows; }

	static constexpr int32 MaxRows = 6;

private:
	TArray<FObjectiveEvent> FeedRows;
	float SeenThrough = -1.f;
	bool bPrimed = false;
	int32 NextSequence = GiftFeed::SequenceBase;
};
