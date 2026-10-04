#include "HqHoldPolicy.h"

HqHoldPolicy::FStep HqHoldPolicy::Advance(EPhase Phase, const FHold& Hold, const FPresence& Presence, float DeltaSeconds)
{
	FStep Step;
	Step.Phase = Phase;
	Step.Hold = Hold;
	if (Phase != EPhase::Offline)
		return Step;
	const float Delta = FMath::Max(0.f, DeltaSeconds);
	if (Presence.Defenders > 0)
		Step.State = EHoldState::Paused;
	else if (Presence.Attackers > 0)
	{
		Step.State = EHoldState::Holding;
		Step.Hold.Progress += Delta;
		Step.Hold.bStarted = Step.Hold.bStarted || Step.Hold.Progress > 0.f;
	}
	else
	{
		Step.State = EHoldState::Decaying;
		Step.Hold.Progress -= Delta;
	}
	if (Step.Hold.Progress >= HoldSeconds)
	{
		Step.Hold.Progress = HoldSeconds;
		Step.Phase = EPhase::Lost;
		Step.State = EHoldState::None;
		Step.bCompleted = true;
	}
	else if (Step.Hold.Progress <= 0.f)
	{
		Step.Hold.Progress = 0.f;
		if (Step.Hold.bStarted)
		{
			Step.Phase = EPhase::Online;
			Step.State = EHoldState::None;
			Step.Hold = FHold();
			Step.bRevived = true;
		}
	}
	return Step;
}

float HqHoldPolicy::Fraction(const FHold& Hold)
{
	return FMath::Clamp(Hold.Progress / HoldSeconds, 0.f, 1.f);
}

int32 HqHoldPolicy::WholeSeconds(const FHold& Hold)
{
	return FMath::Clamp(FMath::FloorToInt(Hold.Progress), 0, FMath::FloorToInt(HoldSeconds));
}

bool HqHoldPolicy::HqImmune(EPhase Phase, int32 NodesStanding)
{
	return Phase == EPhase::Online && NodesStanding > 0;
}

bool HqHoldPolicy::NodePlated(float BattleSeconds)
{
	return BattleSeconds < PlatingEndSeconds;
}

int32 HqHoldPolicy::RestoredHealth(int32 MaxHealth)
{
	return FMath::Max(1, FMath::FloorToInt(MaxHealth * RestoreFraction));
}

bool HqHoldPolicy::ClaimEmergency(bool& bGranted)
{
	if (bGranted)
		return false;
	bGranted = true;
	return true;
}
