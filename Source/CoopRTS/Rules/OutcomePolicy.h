#pragma once

#include "CoreMinimal.h"

// The reflected enum remains in the pinned GameState header. The adapter supplies
// its named values so this policy neither includes an actor nor duplicates enum ordinals.
enum class EMatchResult : uint8;

// A side is lost when its HQ's lifecycle says so: a completed hold on its offline main
// (HqHoldPolicy::EPhase::Lost, AHeadquarters::IsAlive() false), never merely 0 HP.
struct FOutcomeInput
{
	bool bFriendlyLost, bEnemyLost;
	EMatchResult Ongoing, Victory, Defeat;
};

namespace OutcomePolicy
{
EMatchResult Evaluate(const FOutcomeInput& In);
}
