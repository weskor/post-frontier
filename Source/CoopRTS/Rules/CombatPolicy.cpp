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
