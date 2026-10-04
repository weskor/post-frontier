#include "DamagePolicy.h"

#include "ShieldPolicy.h"

namespace
{
// Multipliers are floats with two decimals; the epsilon keeps 10 x 0.8f from truncating to 7.
int32 Scale(int32 Damage, double Multiplier)
{
	return FMath::FloorToInt(static_cast<double>(Damage) * Multiplier + 1e-6);
}
}

int32 DamagePolicy::Outgoing(const FOutgoing& Hit)
{
	int32 Damage = CombatPolicy::Damage(Hit.Base, Hit.Type, Hit.Armor);
	if (Hit.SplashDistance >= 0.f)
		Damage = CombatPolicy::SplashDamage(Damage, Hit.SplashDistance);
	return Hit.WorkshopMultiplier == 1.f ? Damage : Scale(Damage, Hit.WorkshopMultiplier);
}

int32 DamagePolicy::Incoming(int32 Damage, TConstArrayView<float> Multipliers)
{
	double Product = 1.;
	for (const float Multiplier : Multipliers)
		Product *= Multiplier;
	return Product == 1. ? Damage : Scale(Damage, Product);
}

DamagePolicy::FResult DamagePolicy::Absorb(int32 Damage, EDamageType Type, int32 Shield)
{
	if (Damage <= 0)
		return {};
	if (Shield <= 0)
		return { 0, Damage };
	const int32 Factor = Type == EDamageType::EMP ? ShieldPolicy::EmpShieldFactor : 1;
	const int32 ShieldDamage = Damage * Factor;
	if (ShieldDamage <= Shield)
		return { ShieldDamage, 0 };
	return { Shield, (ShieldDamage - Shield) / Factor };
}

DamagePolicy::FResult DamagePolicy::Resolve(int32 Damage, EDamageType Type, int32 Shield, TConstArrayView<float> Multipliers)
{
	return Absorb(Incoming(Damage, Multipliers), Type, Shield);
}

DamagePolicy::FResult DamagePolicy::Environmental(int32 Damage, int32 Shield)
{
	return Absorb(Damage, EDamageType::Unset, Shield);
}
