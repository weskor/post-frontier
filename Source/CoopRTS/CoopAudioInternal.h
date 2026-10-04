#pragma once

#include "CoreMinimal.h"

// Constants shared by the CoopAudio*.cpp files; not part of the subsystem's public API.
namespace CoopAudio
{
constexpr int32 StructureRole = 10;
constexpr int32 UIRole = 11;
constexpr int32 UnitEffectRole = 12;
inline const FName ReverbTag(TEXT("CoopWorldReverb"));
}
