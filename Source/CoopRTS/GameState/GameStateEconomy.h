#pragma once

#include "CoreMinimal.h"

class ACommandGameState;
class ACommandPlayerState;
class UWorld;

// Income schedule and estimates. Payments run on the authority every two seconds of game time.
class FGameStateEconomy
{
public:
	void Tick(ACommandGameState& State, float DeltaSeconds);

	static int32 BaselineIncomePerSecond(const UWorld* World);
	static double EnemyBaselineIncomePerSecond(const ACommandGameState& State);
	static int32 IncomePerSecond(const ACommandGameState& State, const ACommandPlayerState* Commander);

private:
	void PayInterval(ACommandGameState& State);
	void PayEnemyBaseline(ACommandGameState& State);
	static void PayHumanBaseline(ACommandGameState& State);
	static void PayExtractors(ACommandGameState& State);

	float Elapsed = 0.f;
	// Tenths preserve fractional JEV credits without floating-point drift.
	int32 EnemyRemainderTenths = 0;
};
