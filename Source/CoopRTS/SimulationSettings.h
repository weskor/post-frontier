#pragma once

#include "CoreMinimal.h"

class UWorld;

// Process-local, explicitly opted-in constants. Ordinary play and network worlds keep defaults.
struct FSimulationSettings
{
	bool bEnabled = false;
	bool bDuel = false;
	int32 Seed = 1;
	int32 BaselineIncome = 2;
	int32 NormalRate = 4;
	int32 RichRate = 6;
	int32 NormalAmount = 2400;
	int32 RichAmount = 3000;
	float TimeCap = 2400.f;
	float Dilation = 1.f;
	FString Output;
	FString Error;

	static const FSimulationSettings& Get();
	static const FSimulationSettings& ForWorld(const UWorld* World);
};
