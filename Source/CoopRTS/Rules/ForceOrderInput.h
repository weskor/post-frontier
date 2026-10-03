#pragma once

#include "CoreMinimal.h"

namespace ForceOrderInput
{
enum class EResolution : uint8
{
	Reject,
	MoveHold,
	Attack,
	Rally
};
enum class ERejection : uint8
{
	None,
	Unavailable,
	NoSelection,
	InvalidTarget,
	NotOwned,
	QueueFull,
	Unreachable,
	Pending
};
struct FForce
{
	bool bOwned = false;
	int32 SourceRegion = INDEX_NONE;
	int32 OrderCount = 0;
};
struct FContext
{
	TConstArrayView<FForce> Forces;
	TConstArrayView<uint64> Graph;
	int32 TargetRegion = INDEX_NONE;
	int32 ProducerRegion = INDEX_NONE;
	bool bAvailable = false;
	bool bProducerSelected = false;
	bool bHostileStructure = false;
	bool bAttack = false;
	bool bQueue = false;
};
struct FResult
{
	EResolution Resolution = EResolution::Reject;
	ERejection Rejection = ERejection::None;
	bool IsAllowed() const { return Resolution != EResolution::Reject; }
	const TCHAR* Label() const;
};
// Target semantics and atomic selection validation shared by hover and confirmation.
// Region ownership does not affect the smart verb. Three queued orders include active.
FResult Resolve(const FContext& Context);
}
