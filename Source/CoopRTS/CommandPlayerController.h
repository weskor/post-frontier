#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "ConstructionTypes.h"
#include "CommandPlayerState.h"
#include "ForceGoals.h"
#include "CommandPlayerController.generated.h"

class ACommandBuilding;
class AMapRegion;
class UBuildingDefinition;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
enum class EHUDAction : uint8;

enum class ECommandScreen : uint8 { Game, MainMenu, Pause, Controls, Audio, ConfirmLeave, ConfirmQuit, Result };

UCLASS()
class COOPRTS_API ACommandPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ACommandPlayerController();
	virtual void PlayerTick(float DeltaTime) override;
	ACommandBuilding* GetSelectedBuilding() const { return SelectedBuilding; }
	const FString& GetOrderFeedback() const { return Feedback; }
	bool IsHUDExpanded() const { return bHUDExpanded; }
	bool IsPlacingBuilding() const { return bPlacingBuilding; }
	int32 GetPlacementIndex() const { return PlacementIndex; }
	const UBuildingDefinition* GetPlacementDefinition() const;
	bool GetPlacementPreview(FVector& Location, FString& Reason, bool& bCanPlace) const;
	bool CanPlaceBuildingAt(int32 BuildingIndex, const FVector& Location, FString& Reason) const;
	bool IsAssigningGoal() const { return bAssigningGoal; }
	EForceGoal GetPendingGoal() const { return PendingGoal; }
	// Left-click entry points shared by real input and the Development verification probe.
	// Returns true when Position lies on a HUD panel; the click then never reaches the world.
	bool HandleHUDClick(const FVector2D& Position);
	void SelectActor(AActor* Actor);
	ECommandScreen GetUIScreen() const;
	float GetMasterVolume() const;
	bool IsMenuWorld() const;

	UFUNCTION(Server, Reliable)
	void ServerPlaceBuilding(int32 BuildingIndex, FVector Location);
	UFUNCTION(Server, Reliable)
	void ServerCancelBuilding(ACommandBuilding* Building);
	UFUNCTION(Server, Reliable)
	void ServerConfigureProduction(ACommandBuilding* Building, EUnitRole Recipe, bool bEnabled);
	UFUNCTION(Server, Reliable)
	void ServerAssignGoal(ACommandBuilding* Building, EForceGoal Goal, int32 RegionIndex);
	UFUNCTION(Server, Reliable)
	void ServerAssignFront(ACommandBuilding* Building, EFrontOrder Order, FVector Location);
	UFUNCTION(Server, Reliable)
	void ServerResearch(ACommandBuilding* Building, EArmyDoctrine Choice);
	UFUNCTION(Client, Reliable)
	void ClientConstructionFeedback(const FString& Message, bool bAccepted);
	UFUNCTION(Client, Reliable)
	void ClientPlacementFeedback(const FString& Message, bool bAccepted);
	UFUNCTION(Server, Reliable)
	void ServerIssueOrder(AArmyGroup* Army, EArmyOrder Order, FVector Destination);
	UFUNCTION(Server, Reliable)
	void ServerIssueAttack(AArmyGroup* Army, FVector Destination, AActor* Target);
	UFUNCTION(Client, Reliable)
	void ClientAttackFeedback(bool bAccepted);
	UFUNCTION(Client, Reliable)
	void ClientOrderFeedback(bool bAccepted);
	UFUNCTION(Server, Reliable)
	void ServerRequestRestart();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void GetSeamlessTravelActorList(bool bToEntry, TArray<AActor*>& ActorList) override;
	virtual void PostSeamlessTravel() override;

private:
	UPROPERTY(Transient) TObjectPtr<ACommandBuilding> SelectedBuilding;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> Actions;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
	FString Feedback;
	bool bPlacingBuilding = false;
	int32 PlacementIndex = -1;
	bool bAssigningGoal = false;
	bool bHUDExpanded = true;
	bool bPlacementPending = false;
	EForceGoal PendingGoal = EForceGoal::Hold;
	bool bInitialFocusPending = true;
	FVector2D PreviousDragPosition = FVector2D::ZeroVector;
	bool bDragging = false;
	FVector2D PendingPan = FVector2D::ZeroVector;
	ECommandScreen Screen = ECommandScreen::Game;
	ECommandScreen ReturnScreen = ECommandScreen::Game;
	bool bTravelPending = false;
	void Escape();
	void ShowScreen(ECommandScreen NewScreen);
	void PlayUISound(FName Event);
	void PanForward();
	void PanBackward();
	void PanLeft();
	void PanRight();
	void ZoomIn();
	void ZoomOut();
	void SelectUnderCursor();
	void CancelPointerMode();
	void CancelMode();
	void ToggleHUD();
	void RequestRestart();
	void FocusSelection();
	void HandleHUDAction(EHUDAction Action);
	bool IsOwnedArmy(const AArmyGroup* Army) const;
	bool IsOwnedBuilding(const ACommandBuilding* Building) const;
	bool IsValidBuildingCommand(const ACommandBuilding* Building) const;
	bool CanIssueGameplayCommand();
	bool IsMatchTerminal() const;
	bool CursorHit(FHitResult& Hit) const;
	bool HandleScreenAction(EHUDAction Action);
	bool CursorGround(FVector& Location) const;
	const AMapRegion* CursorGoalRegion() const;
	void AssignGoalAt(const FVector& Location);
	void ResetLocalMatchView();
};