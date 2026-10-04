#pragma once

#include "CoreMinimal.h"

// Pure shield, regen and pulse rules; units and buildings only store the state.
namespace ShieldPolicy
{
// EMP spends two shield points per point of damage; the excess converts back at the same ratio.
inline constexpr int32 EmpShieldFactor = 2;
inline constexpr float RegenDelaySeconds = 4.f;
// Regen is a whole percent of max per second, so quarter-second steps stay exact in float.
inline constexpr int32 RegenPercentPerSecond = 10;

struct FShieldClock
{
	// Seconds since the last damage or pulse; regen runs once this reaches the delay.
	float QuietSeconds = 0.f;
	// Fraction of a shield point not yet granted.
	float Carry = 0.f;
};

// Any damage taken, to shield or HP, and any pulse restarts the delay and drops the carry.
void RestartRegen(FShieldClock& Clock);
// Advances the clock and returns the whole points to add; Shield + result never exceeds MaxShield.
int32 Regenerate(FShieldClock& Clock, int32 Shield, int32 MaxShield, float DeltaSeconds);

// Headquarters and Failover Nodes are never pulse subjects: the scan skips them, so they are never stunned.
enum class EPulseSubject : uint8
{
	Unit,
	Building
};

// A pulse is ready when the cooldown that started at the last cast has elapsed; the first is ready at spawn.
bool PulseReady(double Now, double ReadyAt);
double NextPulseReadyAt(double CastTime, float Interval);
// The set that makes a Scrambler cast: a hostile unit with shield above 0, or a hostile building,
// within the radius. Distance is edge distance in the ground plane.
bool PulseTriggers(EPulseSubject Subject, int32 Shield, float Distance, float Radius);
// Everything inside the radius is affected, including shieldless units (nothing to strip).
bool PulseAffects(float Distance, float Radius);

// A new stun refreshes the end to Now + Seconds; stuns never stack or shorten a longer one.
double StunEndTime(double Now, double CurrentEnd, float Seconds);
bool IsStunned(double Now, double StunEnd);
}
