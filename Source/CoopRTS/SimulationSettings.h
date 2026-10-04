#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
#include "Rules/EconomyPolicy.h"

class UWorld;

// Team-0 autopilot behaviour of a whole-match simulation. Rush gives every force Attack along the
// path to JEV's HQ at spawn and after every refill; only casualty withdrawal ever retreats.
enum class ESimulationScenario : uint8
{
	Default,
	Rush
};

// Process-local, explicitly opted-in constants. Ordinary play and network worlds keep defaults.
struct FSimulationSettings
{
	bool bEnabled = false;
	bool bDuel = false;
	int32 Seed = 1;
	ESimulationScenario Scenario = ESimulationScenario::Default;
	int32 HumanBaselineIncome = EconomyPolicy::HumanBaselineIncome;
	int32 JevBaselineIncome = EconomyPolicy::JevBaselineIncome;
	int32 NormalRate = EconomyPolicy::NormalDepositRate;
	int32 RichRate = EconomyPolicy::RichDepositRate;
	int32 NormalAmount = EconomyPolicy::NormalDepositAmount;
	int32 RichAmount = EconomyPolicy::RichDepositAmount;
	float TimeCap = 2400.f;
	float Dilation = 1.f;
	FString Output;
	FString Error;

	static const FSimulationSettings& Get();
	static const FSimulationSettings& ForWorld(const UWorld* World);
};
#endif
