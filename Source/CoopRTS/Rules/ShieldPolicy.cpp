#include "ShieldPolicy.h"

void ShieldPolicy::RestartRegen(FShieldClock& Clock)
{
	Clock = FShieldClock();
}

int32 ShieldPolicy::Regenerate(FShieldClock& Clock, int32 Shield, int32 MaxShield, float DeltaSeconds)
{
	if (MaxShield <= 0 || Shield >= MaxShield || DeltaSeconds <= 0.f)
	{
		if (Shield >= MaxShield)
			Clock.Carry = 0.f;
		return 0;
	}
	// Only the part of this step after the delay elapsed regenerates.
	const float RegenSeconds = Clock.QuietSeconds >= RegenDelaySeconds
		? DeltaSeconds
		: FMath::Max(0.f, Clock.QuietSeconds + DeltaSeconds - RegenDelaySeconds);
	Clock.QuietSeconds += DeltaSeconds;
	Clock.Carry += RegenSeconds * MaxShield * RegenPercentPerSecond / 100.f;
	const int32 Whole = FMath::Min(FMath::FloorToInt(Clock.Carry), MaxShield - Shield);
	Clock.Carry -= Whole;
	if (Shield + Whole >= MaxShield)
		Clock.Carry = 0.f;
	return Whole;
}

bool ShieldPolicy::PulseReady(double Now, double ReadyAt)
{
	return Now >= ReadyAt;
}

double ShieldPolicy::NextPulseReadyAt(double CastTime, float Interval)
{
	return CastTime + Interval;
}

bool ShieldPolicy::PulseAffects(float Distance, float Radius)
{
	return Distance <= Radius;
}

bool ShieldPolicy::PulseTriggers(EPulseSubject Subject, int32 Shield, float Distance, float Radius)
{
	return PulseAffects(Distance, Radius) && (Subject == EPulseSubject::Building || Shield > 0);
}

double ShieldPolicy::StunEndTime(double Now, double CurrentEnd, float Seconds)
{
	return FMath::Max(CurrentEnd, Now + Seconds);
}

bool ShieldPolicy::IsStunned(double Now, double StunEnd)
{
	return Now < StunEnd;
}
