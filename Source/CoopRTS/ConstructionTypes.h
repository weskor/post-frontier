#pragma once

#include "CoreMinimal.h"
#include "ConstructionTypes.generated.h"

UENUM(BlueprintType)
enum class EBuildingKind : uint8
{
	Barracks,
	Extractor,
	Workshop
};

