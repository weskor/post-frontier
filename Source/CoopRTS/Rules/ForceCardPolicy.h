#pragma once
#include "CoreMinimal.h"

namespace ForceCardPolicy
{
enum class EState : uint8
{
	Marching,
	Holding,
	Withdrawing,
	Retreating,
	Refilling,
	Responding
};
struct FState
{
	EState State = EState::Holding;
	int32 Joined = 0;
	int32 Capacity = 0;
	int32 ResumeCount = 0;
	int32 ETA = INDEX_NONE;
	FStringView Target;
	FStringView Threat;
};
// Negative means an unavailable estimate, never an instantaneous arrival.
int32 TravelSeconds(double PathLength, float SlowestSpeed);
EState ResolveState(EState Status, bool bAttack, bool bResponding, int32 ResumeCount);
}
