#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"

class ACommandGameState;
class UWorld;

// Controlled JEV plan publication for the intent-display proofs: two real JEV forces and
// published plans shaped exactly like the planner's (ticket, verb, regions, size band, ETA,
// commitment, memo from [JevMemos]). The planner is switched off so nothing overwrites them.
namespace JevIntentFixture
{
enum class EStage : uint8
{
	// Force 0 attacks region A (ETA 40 s); force 1 moves and holds region B (ETA 25 s).
	Create,
	// Force 1's ticket is kept and now defends region B as an escalated plan.
	Escalate,
	// Force 0's plan is replaced by a new ticket attacking region C.
	Replace
};

// Authority only. Returns an empty string on success.
FString Publish(UWorld* World, ACommandGameState& State, EStage Stage);
}
#endif
