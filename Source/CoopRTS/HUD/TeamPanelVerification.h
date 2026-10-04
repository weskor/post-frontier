#pragma once
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
class UWorld;
class FJsonObject;
class ACommandGameState;
// Probe actions and snapshot of the Team panel for the loopback network and HUD capture scenarios. A gift goes through
// the local controller's RPC like a click on Send; the teammate and log fixtures run on the authority host only.
namespace TeamPanelVerification
{
bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error);
void Snapshot(UWorld& World, const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result);
}
#endif
