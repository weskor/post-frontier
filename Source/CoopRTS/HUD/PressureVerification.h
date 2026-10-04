#pragma once
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
class UWorld;
class FJsonObject;
// Host-only fixture for the pressure HUD captures, on the opted-in authority host. Action: pressureStun (seconds) stuns
// every finished building of the local commander through the same ApplyStun a Scrambler pulse calls. Returns false for
// any other action.
namespace PressureVerification
{
bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error);
}
#endif
