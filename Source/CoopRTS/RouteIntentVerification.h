#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
class AArmyGroup;
class ACommandPlayerController;
class FJsonObject;
namespace RouteIntentVerification
{
void Snapshot(const AArmyGroup& Force, const TSharedPtr<FJsonObject>& Result);
void HoverSnapshot(const ACommandPlayerController& Controller, const TSharedPtr<FJsonObject>& Result);
FString TeammateFixture(ACommandPlayerController& Controller, int32 Target, bool bEnable);
}
#endif
