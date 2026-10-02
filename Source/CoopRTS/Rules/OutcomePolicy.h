#pragma once

#include "CoreMinimal.h"

// The reflected enum remains in the pinned GameState header. The adapter supplies
// its named values so this policy neither includes an actor nor duplicates enum ordinals.
enum class EMatchResult : uint8;

struct FOutcomeInput
{
	int32 FriendlyHealth, EnemyHealth;
	EMatchResult Ongoing, Victory, Defeat;
};

namespace OutcomePolicy
{
EMatchResult Evaluate(const FOutcomeInput& In);
}
