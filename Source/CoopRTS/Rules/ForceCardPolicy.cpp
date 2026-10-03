#include "ForceCardPolicy.h"

namespace ForceCardPolicy
{
int32 TravelSeconds(double PathLength, float SlowestSpeed)
{
	if (!FMath::IsFinite(PathLength) || PathLength < 0. || !FMath::IsFinite(SlowestSpeed) || SlowestSpeed <= 0.f)
		return INDEX_NONE;
	return static_cast<int32>(FMath::Min(FMath::CeilToDouble(PathLength / SlowestSpeed), static_cast<double>(MAX_int32)));
}

EState ResolveState(EState Status, bool bAttack, bool bResponding, int32 ResumeCount)
{
	if (bResponding)
		return EState::Responding;
	if (Status == EState::Refilling && bAttack && ResumeCount > 0)
		return EState::Withdrawing;
	return Status;
}

}
