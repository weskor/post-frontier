#pragma once

#include "Rules/ForceOrderInput.h"

class AActor;

struct FOrderInputPreview : ForceOrderInput::FResult
{
	int32 RegionIndex = INDEX_NONE;
	AActor* Structure = nullptr;
};
