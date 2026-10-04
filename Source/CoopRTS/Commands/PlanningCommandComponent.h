#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CommandService.h"
#include "PlanningCommandComponent.generated.h"

class AActor;

// Which kind of planning edit a verdict answers, so the controller can end the placement mode or clear a pending Ready.
UENUM()
enum class EPlanningEdit : uint8
{
	Ready,
	Kit,
	UnitType,
	FirstOrder
};

// Owner-only planning commands (ui.md surface 10). The authority is FPlanningCommands, which names the issuing commander
// and refuses an edit outside planning or after Ready; this component keeps no state of its own.
UCLASS()
class COOPRTS_API UPlanningCommandComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPlanningCommandComponent();
	UFUNCTION(Server, Reliable)
	void ServerSetReady(bool bReady);
	// Piece is EBuildingKind::Barracks or EBuildingKind::Extractor (the Drill Rig); Location is the requested centre.
	UFUNCTION(Server, Reliable)
	void ServerPlaceKit(EBuildingKind Piece, FVector Location);
	UFUNCTION(Server, Reliable)
	void ServerSetUnitType(EUnitRole Role);
	UFUNCTION(Server, Reliable)
	void ServerSetFirstOrder(EForceVerb Verb, int32 RegionIndex, AActor* Structure, bool bQueue);
	UFUNCTION(Server, Reliable)
	void ServerClearFirstOrders();
	// The verdict of the last edit: the service's reason on a rejection, its acceptance line otherwise.
	UFUNCTION(Client, Reliable)
	void ClientPlanningFeedback(const FString& Message, bool bAccepted, EPlanningEdit Edit);

private:
	class ACommandPlayerState* Commander() const;
};
