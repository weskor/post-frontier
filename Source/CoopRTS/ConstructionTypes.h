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

UENUM(BlueprintType)
enum class EFrontOrder : uint8
{
	Secure,
	Defend,
	FallBack
};
