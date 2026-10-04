#pragma once

#include "CoreMinimal.h"

// Constants shared by the CoopAudio*.cpp files; not part of the subsystem's public API.
namespace CoopAudio
{
constexpr int32 StructureRole = 3;
constexpr int32 UIRole = 4;
inline const FName ReverbTag(TEXT("CoopWorldReverb"));
}
