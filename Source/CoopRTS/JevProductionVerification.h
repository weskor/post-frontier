#pragma once
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
class ACommandBuilding;
class ACommandGameState;
class UWorld;
class FJsonObject;
// Host-only fixture for the Machine-faction squad captures, on the opted-in authority host.
// Action jevProduction (role = EUnitRole ordinal): places a completed Barracks for JEV beside its HQ, locks it to the
// role through the command layer with JEV's wallet filled, and centres the camera on it.
// Action jevFocus (x, y): centres the camera on that ground point. Returns false for any other action.
namespace JevProductionVerification
{
bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error);
// Places a Barracks for JEV's commander, paid from its wallet, on the first legal ring site in its main; null when none.
ACommandBuilding* PlaceBarracks(ACommandGameState& State);
}
#endif
