#include "Rules/MovementProgressPolicy.h"

namespace MovementProgressPolicy
{
namespace
{
// Ticks land a hair before or after the nominal window end.
constexpr float WindowSlack = .05f;
}

float CloseEnoughRadius(float IdleSeconds)
{
	return FMath::Clamp(IdleSeconds * RadiusGrowth, 0.f, MaxRadius);
}

bool IsExempt(const FUnitProgress& Progress, const FVector& Position)
{
	if (!Progress.bTracking)
		return false;
	return Progress.bSettled
		|| (Progress.IdleSeconds > 0.f
			&& FVector::DistSquared2D(Position, Progress.Goal) <= FMath::Square(CloseEnoughRadius(Progress.IdleSeconds)));
}

EAction Update(FUnitProgress& Progress, const FSample& Sample)
{
	if (!Sample.bHasGoal || FVector::DistSquared2D(Sample.Position, Sample.Goal) <= FMath::Square(AtGoalRadius))
	{
		Progress = FUnitProgress();
		return EAction::None;
	}
	if (!Progress.bTracking || FVector::DistSquared2D(Progress.Goal, Sample.Goal) > FMath::Square(GoalChangeTolerance))
	{
		Progress = FUnitProgress();
		Progress.bTracking = true;
		Progress.Goal = Sample.Goal;
		Progress.WindowStart = Sample.Position;
		Progress.WindowStartTime = Sample.Now;
		return EAction::None;
	}
	if (Progress.bSettled || Sample.Now - Progress.WindowStartTime < SampleSeconds - WindowSlack)
		return EAction::None;
	if (FVector::DistSquared2D(Progress.WindowStart, Sample.Position) >= FMath::Square(MinProgress))
		Progress.IdleSeconds = 0.f;
	else
		Progress.IdleSeconds += Sample.Now - Progress.WindowStartTime;
	Progress.WindowStart = Sample.Position;
	Progress.WindowStartTime = Sample.Now;
	if (Progress.IdleSeconds >= SettleIdleSeconds)
	{
		Progress.bSettled = true;
		return EAction::Settle;
	}
	if (Progress.IdleSeconds >= RepathIdleSeconds && !Progress.bRepathed)
	{
		Progress.bRepathed = true;
		return EAction::Repath;
	}
	return EAction::None;
}

FArrival JudgeArrival(TConstArrayView<FArrivalMember> Members, float FittedRadius)
{
	FArrival Arrival;
	if (Members.IsEmpty())
		return Arrival;
	const auto Settled = [](const FArrivalMember& Member) {
		return Member.bIdle
			&& FVector::Dist2D(Member.Position, Member.Slot) <= ArmyGroupPolicy::SlotFallbackRadius + ArrivalTolerance;
	};
	for (const FArrivalMember& Member : Members)
	{
		Arrival.Center += Member.Position;
		Arrival.ExpectedMean += Settled(Member) && FVector::Dist2D(Member.Position, Member.Slot) > ArrivalTolerance
			? Member.Position
			: Member.Slot;
	}
	Arrival.Center /= Members.Num();
	Arrival.ExpectedMean /= Members.Num();
	Arrival.bGathered = FVector::Dist2D(Arrival.Center, Arrival.ExpectedMean) <= ArrivalTolerance;
	for (const FArrivalMember& Member : Members)
		Arrival.bGathered &= Settled(Member) || FVector::Dist2D(Member.Position, Member.Slot) <= ArrivalTolerance
			|| FVector::Dist2D(Member.Position, Arrival.Center) <= FittedRadius + ArrivalTolerance;
	return Arrival;
}
}
