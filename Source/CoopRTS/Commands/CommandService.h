#pragma once

#include "CoreMinimal.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandPlayerState.h"
#include "ConstructionTypes.h"
#include "ForceGoals.h"

class ACommandBuilding;
class ACommandPlayerController;

enum class ECommandRejection : uint8
{
	None,
	Unavailable,
	InvalidOwner,
	InvalidRequest
};

struct FCommandResult
{
	ECommandRejection Rejection = ECommandRejection::None;
	FString Message;
	ACommandBuilding* Building = nullptr;
	bool IsAccepted() const { return Rejection == ECommandRejection::None; }
	explicit operator bool() const { return IsAccepted(); }
};

// Authoritative entry points shared by RPC components, JEV and verification.
// Commander identifies the issuing wallet, never the target object's owner.
class COOPRTS_API FCommandService
{
public:
	static FCommandResult PlaceBuilding(ACommandPlayerState* Commander, int32 BuildingIndex, const FVector& Location);
	static FCommandResult CancelBuilding(ACommandPlayerState* Commander, ACommandBuilding* Building);
	static FCommandResult ConfigureProduction(ACommandPlayerState* Commander, ACommandBuilding* Building, EUnitRole Recipe, bool bEnabled);
	static FCommandResult AssignGoal(ACommandPlayerState* Commander, ACommandBuilding* Building, EForceGoal Goal, int32 RegionIndex);
	static FCommandResult AssignFront(ACommandPlayerState* Commander, ACommandBuilding* Building, EFrontOrder Order, const FVector& Location);
	static FCommandResult AssignFront(ACommandPlayerState* Commander, AArmyGroup* Army, EFrontOrder Order, const FVector& Location);
	static FCommandResult Research(ACommandPlayerState* Commander, ACommandBuilding* Building, EArmyDoctrine Choice);
	static FCommandResult Restart(ACommandPlayerController* Controller);
	static FCommandResult IssueOrder(ACommandPlayerState* Commander, AArmyGroup* Army, EArmyOrder Order, const FVector& Destination);
	static FCommandResult IssueAttack(ACommandPlayerState* Commander, AArmyGroup* Army, FVector Destination, AActor* Target);
};
