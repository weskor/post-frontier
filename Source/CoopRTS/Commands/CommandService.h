#pragma once

#include "CoreMinimal.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandPlayerState.h"
#include "ConstructionTypes.h"
#include "ForceOrders.h"

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
	static FCommandResult IssueForceOrder(ACommandPlayerState* Commander, AArmyGroup* Force, EForceVerb Verb, int32 RegionIndex = INDEX_NONE, AActor* Structure = nullptr, bool bQueue = false);
	static FCommandResult IssueForceOrder(ACommandPlayerState* Commander, TConstArrayView<AArmyGroup*> Forces, EForceVerb Verb, int32 RegionIndex = INDEX_NONE, AActor* Structure = nullptr, bool bQueue = false);
	static FCommandResult SetRetreatThreshold(ACommandPlayerState* Commander, AArmyGroup* Force, ERetreatThreshold Threshold);
	static FCommandResult SetRetreatThreshold(ACommandPlayerState* Commander, TConstArrayView<AArmyGroup*> Forces, ERetreatThreshold Threshold);
	static FCommandResult SetRallyPoint(ACommandPlayerState* Commander, ACommandBuilding* Building, int32 RegionIndex);
	static FCommandResult Research(ACommandPlayerState* Commander, ACommandBuilding* Building, EArmyDoctrine Choice);
	// Free, atomic transfer of a whole amount between two roster commanders of the same team, during a live battle.
	static FCommandResult Gift(ACommandPlayerState* Sender, ACommandPlayerState* Recipient, EEconomyResource Resource, int32 Amount);
	static FCommandResult Restart(ACommandPlayerController* Controller);
	static FCommandResult Pause(ACommandPlayerController* Controller);
	static FCommandResult Resume(ACommandPlayerController* Controller);
	static FCommandResult Ping(ACommandPlayerController* Controller, const FVector& Location, AArmyGroup* Force = nullptr);
};
