#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyTestSetup.h"
#include "Commands/PlanningCommands.h"
#include "Misc/AutomationTest.h"

// Shared scaffolding of the planning world tests (CoopRTS.Planning.*). A scenario holds the harness off, clears the
// world's buildings and forces, adds a second human commander and opens a fresh planning phase; its Step then runs
// once per update on the live world. Waits on frozen game time use World->GetRealTimeSeconds().
namespace PlanningFixture
{
class FScenario : public IAutomationLatentCommand
{
public:
	explicit FScenario(FAutomationTestBase* InTest, double InTimeoutSeconds = 120.);
	~FScenario() override;
	bool Update() final;

protected:
	// Runs once per update after setup; true ends the scenario.
	virtual bool Step() = 0;
	bool Check(bool bOk, const FString& Message);
	// Cleans up and ends the scenario.
	bool Done();
	void Enter(int32 Next);
	// Real seconds in this stage (the world may stand still).
	double StageSeconds() const { return World->GetRealTimeSeconds() - StageRealStart; }
	// Game seconds in this stage.
	double StageGameSeconds() const { return World->GetTimeSeconds() - StageGameStart; }
	ACommandPlayerState* SpawnCommander();
	void RemoveCommander(ACommandPlayerState* Commander);
	// The nth legal free Barracks location around the friendly headquarters, for team 0.
	bool BarracksSpot(int32 Nth, FVector& OutLocation) const;
	// Free deposits in team 0's territory, nearest the friendly headquarters first.
	TArray<ADepositSite*> OwnFreeDeposits() const;
	bool JevKitStands(int32 Count) const;

	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	ACommandGameState* State = nullptr;
	ACommandPlayerController* PC = nullptr;
	ACommandPlayerState* Host = nullptr;
	TWeakObjectPtr<ACommandPlayerState> GuestPtr;
	int32 Stage = 0;

private:
	void Refresh();
	bool Prepare();
	double Started;
	double Timeout;
	double StageRealStart = 0.;
	double StageGameStart = 0.;
	bool bPrepared = false;
	bool bFailed = false;
};
}
#endif
