#pragma once

#include "CoreMinimal.h"
#include "CombatPolicy.generated.h"

UENUM(BlueprintType)
enum class EArmorClass : uint8
{
	Light,
	Heavy,
	Shielded,
	Structure,
	Unset UMETA(Hidden)
};

UENUM(BlueprintType, meta = (ScriptName = "WeaponDamageType"))
enum class EDamageType : uint8
{
	Kinetic,
	Piercing,
	Demolition,
	EMP,
	Unset UMETA(Hidden)
};

namespace CombatPolicy
{
// EMP's shield-point rule is deferred until shielded units exist; no HP bonus.
bool IsStrongAgainst(EDamageType Type, EArmorClass Armor);
// Apply the class bonus before Workshop modifiers; truncate fractional HP.
int32 Damage(int32 BaseDamage, EDamageType Type, EArmorClass Armor);
inline constexpr float ArtillerySplashRadius = 200.f;
// Centre gets full damage; the inclusive edge gets half. Truncate fractional HP.
int32 SplashDamage(int32 Damage, float Distance);
}
