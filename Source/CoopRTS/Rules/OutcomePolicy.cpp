#include "OutcomePolicy.h"

EMatchResult OutcomePolicy::Evaluate(const FOutcomeInput& In)
{
	if (In.FriendlyHealth <= 0) return In.Defeat;
	if (In.EnemyHealth <= 0) return In.Victory;
	return In.Ongoing;
}
