#pragma once

#include "CoreMinimal.h"
#include "CombatPolicy.h"

// The one damage pipeline. Order: base, outgoing (class bonus, splash falloff, Workshop), incoming
// multipliers, shield absorption, HP. Every fractional step truncates.
namespace DamagePolicy
{
// Doctrine multipliers: Siege Optics trades 25% outgoing damage for range; Entrenched Frontline
// takes 25% less while holding still. Fortify joins the incoming list with its own constant.
inline constexpr float SiegeOpticsOutgoingMultiplier = .75f;
inline constexpr float EntrenchedIncomingMultiplier = .75f;

struct FOutgoing
{
	int32 Base = 0;
	EDamageType Type = EDamageType::Unset;
	EArmorClass Armor = EArmorClass::Unset;
	// Distance from the splash centre; negative for a single-target hit.
	float SplashDistance = -1.f;
	// Outgoing Workshop specialization multiplier (Siege Optics 0.75).
	float WorkshopMultiplier = 1.f;
};

// What one hit takes from a victim's durability.
struct FResult
{
	int32 ShieldLoss = 0;
	int32 HealthLoss = 0;
	bool Any() const { return ShieldLoss > 0 || HealthLoss > 0; }
};

// Damage after class bonus, splash falloff and Workshop; 0 outside the splash radius.
int32 Outgoing(const FOutgoing& Hit);
// Incoming multipliers (Cover, Fortify, Entrenched) multiply together before one truncation.
int32 Incoming(int32 Damage, TConstArrayView<float> Multipliers);
// Shield absorbs first: EMP spends two points per damage, and the excess converts back to HP.
FResult Absorb(int32 Damage, EDamageType Type, int32 Shield);
// Steps 3 to 5 for a hit from an attacker.
FResult Resolve(int32 Damage, EDamageType Type, int32 Shield, TConstArrayView<float> Multipliers);
// Attacker-independent damage (Hazard): shield first at one point per damage, then HP; no class
// bonus and no incoming multipliers.
FResult Environmental(int32 Damage, int32 Shield);
}
