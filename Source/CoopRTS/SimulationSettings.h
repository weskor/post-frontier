#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
#include "Rules/EconomyPolicy.h"

class UWorld;

// Process-local, explicitly opted-in constants. Ordinary play and network worlds keep defaults.
struct FSimulationSettings
{
	bool bEnabled = false;
	bool bDuel = false;
	int32 Seed = 1;
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
