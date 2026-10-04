#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class ACommandBuilding;
class ACommandGameState;
class ACommandPlayerState;
class UWorld;

// Income schedule, team pool and estimates. Payments run on the authority every two seconds of game time.
class FGameStateEconomy
{
public:
	void Tick(ACommandGameState& State, float DeltaSeconds);

	static int32 HumanBaselineIncomePerSecond(const UWorld* World);
	static int32 JevBaselineIncomePerSecond(const UWorld* World);
	static double EnemyBaselineIncomePerSecond(const ACommandGameState& State);
	// Whole Power per second, rounded down from PowerRate.
	static int32 IncomePerSecond(const ACommandGameState& State, const ACommandPlayerState* Commander);
	// Exact per-second share of the human pool for a roster commander; JEV's own income for its commander.
	// Estimates read the published connected mask, the one clients see; payment reads the live rule.
	static double PowerRate(const ACommandGameState& State, const ACommandPlayerState* Commander);
	static double DataRate(const ACommandGameState& State, const ACommandPlayerState* Commander);
	// Human commanders of the current roster in slot order: team 0, slot 0 to 4, one wallet per slot.
	static TArray<ACommandPlayerState*> Roster(const ACommandGameState& State);

private:
	void PayInterval(ACommandGameState& State);
	void PayEnemyBaseline(ACommandGameState& State);
	void TrackStructureKills(ACommandGameState& State);
	static void PayHumanPool(ACommandGameState& State);
	static void PayEnemyExtractors(ACommandGameState& State);

	float Elapsed = 0.f;
	// Tenths preserve fractional JEV credits without floating-point drift.
	int32 EnemyRemainderTenths = 0;
	// Finished JEV buildings seen alive; one that disappears was destroyed, since only unfinished ones cancel.
	TArray<TWeakObjectPtr<const ACommandBuilding>> LiveJevBuildings;
};
