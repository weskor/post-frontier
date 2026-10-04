#include "OutcomePolicy.h"

EMatchResult OutcomePolicy::Evaluate(const FOutcomeInput& In)
{
	if (In.bFriendlyLost)
		return In.Defeat;
	if (In.bEnemyLost)
		return In.Victory;
	return In.Ongoing;
}
