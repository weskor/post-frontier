#pragma once

#include "CoreMinimal.h"
#include "Rules/ForceOrderPolicy.h"
#include "ForceOrders.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EForceVerb : uint8
{
	MoveHold,
	Attack,
	Retreat
};

UENUM(BlueprintType)
enum class EForceStatus : uint8
{
	Marching,
	Holding,
	Withdrawing,
	Retreating,
	Refilling
};

UENUM(BlueprintType)
enum class ERetreatThreshold : uint8
{
	Never = 0,
	Percent25 = 25,
	Percent40 = 40,
	Percent60 = 60
};

USTRUCT(BlueprintType)
struct FForceOrder
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EForceVerb Verb = EForceVerb::MoveHold;
	UPROPERTY(BlueprintReadOnly)
	int32 RegionIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AActor> Structure;
	UPROPERTY(BlueprintReadOnly)
	float SelectionSpeed = 0.f;
	// Retained if the actor is destroyed before this queued order becomes active.
	UPROPERTY(BlueprintReadOnly)
	bool bStructureTarget = false;

	FForceOrder() = default;
	FForceOrder(EForceVerb InVerb, int32 InRegion, AActor* InStructure = nullptr)
		: Verb(InVerb), RegionIndex(InRegion), Structure(InStructure), bStructureTarget(InStructure != nullptr) {}
};
