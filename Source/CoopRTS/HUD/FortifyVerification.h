#pragma once
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
class UWorld;
class FJsonObject;
namespace FortifyVerification
{
bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error);
}
#endif
