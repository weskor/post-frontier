#pragma once
#include "CoreMinimal.h"
class AArmyGroup;
class ACommandPlayerController;
class FJsonObject;
class UWorld;

namespace ForceBarVerification
{
void Snapshot(const ACommandPlayerController& Controller, const AArmyGroup& Force, const TSharedPtr<FJsonObject>& Entry);
bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error);
}
