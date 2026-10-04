#pragma once
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
class UWorld;
class FJsonObject;
// Host-only fixtures for the map presentation captures (supply cuts and the Scrambler pulse), on the opted-in authority
// host. Actions: mapPresControl (region, team), mapPresRig (region), mapPresPulse (region), mapPresDilation (factor) and
// mapPresClear. Returns false for any other action.
namespace MapPresentationVerification
{
bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error);
}
#endif
