#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "SimulationSettings.h"

#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/Parse.h"
#include <type_traits>

namespace
{
template <typename T>
void ReadNumber(const TCHAR* Key, T& Value, T Minimum, T Maximum, FString& Error)
{
	FString Text;
	if (!FParse::Value(FCommandLine::Get(), Key, Text))
		return;
	double Parsed = 0.;
	if (!FDefaultValueHelper::ParseDouble(Text, Parsed) || !FMath::IsFinite(Parsed)
		|| Parsed < Minimum || Parsed > Maximum
		|| (std::is_integral_v<T> && Parsed != FMath::FloorToDouble(Parsed)))
	{
		Error = FString::Printf(TEXT("Invalid %s%s"), Key, *Text);
		return;
	}
	Value = static_cast<T>(Parsed);
}

// "default" or "rush"; anything else is an error rather than a silent default.
void ReadScenario(ESimulationScenario& Scenario, FString& Error)
{
	FString Text;
	if (!FParse::Value(FCommandLine::Get(), TEXT("SimScenario="), Text))
		return;
	if (Text.Equals(TEXT("rush"), ESearchCase::IgnoreCase))
		Scenario = ESimulationScenario::Rush;
	else if (!Text.Equals(TEXT("default"), ESearchCase::IgnoreCase))
		Error = FString::Printf(TEXT("Invalid SimScenario=%s"), *Text);
}
}

const FSimulationSettings& FSimulationSettings::Get()
{
	static const FSimulationSettings Settings = [] {
		FSimulationSettings Result;
		Result.bDuel = FParse::Param(FCommandLine::Get(), TEXT("SimDuel"));
		Result.bEnabled = Result.bDuel || FParse::Param(FCommandLine::Get(), TEXT("autopilot"));
		if (!Result.bEnabled)
			return Result;
		if (Result.bDuel)
			Result.TimeCap = 300.f;
		ReadNumber(TEXT("SimSeed="), Result.Seed, 0, MAX_int32, Result.Error);
		ReadScenario(Result.Scenario, Result.Error);
		ReadNumber(TEXT("SimHumanBaseline="), Result.HumanBaselineIncome, 0, 10000, Result.Error);
		ReadNumber(TEXT("SimJevBaseline="), Result.JevBaselineIncome, 0, 10000, Result.Error);
		ReadNumber(TEXT("SimNormalRate="), Result.NormalRate, 0, 10000, Result.Error);
		ReadNumber(TEXT("SimRichRate="), Result.RichRate, 0, 10000, Result.Error);
		ReadNumber(TEXT("SimNormalAmount="), Result.NormalAmount, 0, 100000000, Result.Error);
		ReadNumber(TEXT("SimRichAmount="), Result.RichAmount, 0, 100000000, Result.Error);
		ReadNumber(TEXT("SimTimeCap="), Result.TimeCap, 1.f, 86400.f, Result.Error);
		ReadNumber(TEXT("SimDilation="), Result.Dilation, 1.f, 32.f, Result.Error);
		FParse::Value(FCommandLine::Get(), TEXT("SimOutput="), Result.Output);
		if (Result.Output.IsEmpty())
			Result.Error = TEXT("Simulation requires -SimOutput=<absolute JSON path>");
		return Result;
	}();
	return Settings;
}

const FSimulationSettings& FSimulationSettings::ForWorld(const UWorld* World)
{
	static const FSimulationSettings Defaults;
	return World && World->GetNetMode() == NM_Standalone ? Get() : Defaults;
}
#endif
