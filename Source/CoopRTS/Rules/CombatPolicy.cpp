#include "CombatPolicy.h"

bool CombatPolicy::IsStrongAgainst(EDamageType Type, EArmorClass Armor)
{
	return (Type == EDamageType::Kinetic && Armor == EArmorClass::Light)
		|| (Type == EDamageType::Piercing && Armor == EArmorClass::Heavy)
		|| (Type == EDamageType::Demolition && Armor == EArmorClass::Structure);
}

int32 CombatPolicy::Damage(int32 BaseDamage, EDamageType Type, EArmorClass Armor)
{
	return IsStrongAgainst(Type, Armor) ? BaseDamage + BaseDamage / 2 : BaseDamage;
}

int32 CombatPolicy::SplashDamage(int32 Damage, float Distance)
{
	if (Distance > ArtillerySplashRadius)
		return 0;
	return static_cast<int32>(Damage * (1.f - .5f * FMath::Max(0.f, Distance) / ArtillerySplashRadius));
}
