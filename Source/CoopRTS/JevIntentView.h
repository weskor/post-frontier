#pragma once

#include "CoreMinimal.h"
#include "Rules/JevIntent.h"
#include "Subsystems/WorldSubsystem.h"
#include "JevIntentView.generated.h"

class ACommandGameState;

// Reads the replicated JEV plans for presentation; nothing here is computed beyond the published values.
namespace JevIntentView
{
using FPlans = TArray<JevIntent::FPlanView, TInlineAllocator<16>>;
// Memo views borrow the replicated strings and are valid until the plan list next changes.
void Snapshot(const ACommandGameState& State, FPlans& Out);
float Now(const ACommandGameState& State);
// The display name the plan memo prints for the region.
const FString& RegionName(const ACommandGameState& State, int32 Region);
}

// The memo feed is local presentation of the replicated plans: each peer posts what it sees change.
UCLASS()
class COOPRTS_API UJevIntentFeed : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	static UJevIntentFeed* Get(const UObject* Context);
	// Returns the number of memos posted.
	int32 Observe(const ACommandGameState& State);
	const JevIntent::FMemoFeed& GetFeed() const { return Feed; }

private:
	JevIntent::FMemoFeed Feed;
};
