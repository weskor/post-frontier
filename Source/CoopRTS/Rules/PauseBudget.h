#pragma once

#include "CoreMinimal.h"

// Real-time budget: simulation time cannot expire a paused battle.
struct FPauseBudget
{
	static constexpr double CoopSeconds = 60.;
	bool bPaused = false;
	bool bSpent = false;
	double Deadline = 0.;

	bool CanPause(bool bCoop) const { return !bPaused && (!bCoop || !bSpent); }
	void Begin(bool bCoop, double Now)
	{
		bPaused = true;
		bSpent |= bCoop;
		Deadline = bCoop ? Now + CoopSeconds : 0.;
	}
	void Resume() { bPaused = false; }
	double Remaining(double Now) const { return bPaused && Deadline > 0. ? FMath::Max(0., Deadline - Now) : 0.; }
	bool Expired(double Now) const { return bPaused && Deadline > 0. && Now >= Deadline; }
};
