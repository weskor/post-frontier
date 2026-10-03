#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "ConstructionTypes.h"
#include "CommandPlayerState.h"
#include "ForceOrders.h"
#include "HUD/OrderInputPreview.h"
#include "CommandPlayerController.generated.h"

class ACommandBuilding;
class AMapRegion;
class UBuildingDefinition;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
enum class EHUDAction : uint8;
class UConstructionCommandComponent;
class UProductionCommandComponent;
class UOrderCommandComponent;
class UMatchCommandComponent;
class UPingCommandComponent;

enum class ECommandScreen : uint8
{
	Game,
	MainMenu,
	Pause,
	Controls,
	Audio,
	ConfirmLeave,
	ConfirmQuit,
	Result
};

UCLASS()
class COOPRTS_API ACommandPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ACommandPlayerController();
	virtual void PlayerTick(float DeltaTime) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	ACommandBuilding* GetSelectedBuilding() const { return SelectedBuilding; }
	const TArray<TObjectPtr<AArmyGroup>>& GetSelectedForces() const { return SelectedForces; }
	AArmyGroup* GetInspectedForce() const { return InspectedForce; }
	bool IsForceSelected(const AArmyGroup* Force) const;
	bool IsForceHighlighted(const AArmyGroup* Force) const;
	bool IsSelectableForce(const AArmyGroup* Force) const;
	void SelectForce(AArmyGroup* Force, bool bToggle = false);
	void SelectForceNumber(int32 Number);
	void SelectForceBox(const FVector2D& Start, const FVector2D& End, bool bAdd = false);
	void SelectActorWithModifiers(AActor* Actor, bool bToggle, bool bDoubleClick);
	bool GetSelectionDrag(FVector2D& Start, FVector2D& End) const;
	void FocusSelection();
	bool PingAtScreenPosition(const FVector2D& Position);
	const FString& GetOrderFeedback() const { return Feedback; }
	float GetFeedbackOpacity() const;
	bool IsBuildHotkeyPending() const;
	bool IsHUDExpanded() const { return bHUDExpanded; }
	bool IsPlacingBuilding() const { return bPlacingBuilding; }
	int32 GetPlacementIndex() const { return PlacementIndex; }
	const UBuildingDefinition* GetPlacementDefinition() const;
	bool GetPlacementPreview(FVector& Location, FString& Reason, bool& bCanPlace) const;
	bool CanPlaceBuildingAt(int32 BuildingIndex, const FVector& Location, FString& Reason) const;
	void PlaceBuildingAt(const FVector& Location, bool bRepeat);
	bool IsAssigningOrder() const { return bAssigningOrder; }
	EForceVerb GetPendingVerb() const { return PendingVerb; }
	void BeginForceAttack();
	void RetreatSelectedForces(bool bQueue = false);
	bool HandleOrderClick(const FVector2D& Position, bool bQueue = false);
	void ConfirmAttackAtScreenPosition(const FVector2D& Position, bool bQueue = false);
	// Call only for minimap/world points, after HUD actions and force cards have consumed their clicks.
	bool HandleAttackTargetClick(const FVector2D& Position);
	FOrderInputPreview GetOrderPreview(const FVector2D& Position, bool bQueue = false) const;
	void CompleteOrderInput(const FString& Message, bool bAccepted, uint32 AttackInputId);
	bool GetHoveredForceOrder(FForceOrder& OutOrder, bool& bQueue) const;
	// Left-click entry points shared by real input and the Development verification probe.
	// Returns true for a HUD panel or force badge; the consumed click never reaches the world trace.
	bool HandleHUDClick(const FVector2D& Position);
	void HandleForceCardClick(AArmyGroup* Force, EHUDAction Action, bool bAdd, bool bDoubleClick = false);
	void SelectActor(AActor* Actor, bool bToggle = false);
	ECommandScreen GetUIScreen() const;
	float GetMasterVolume() const;
	bool IsMenuWorld() const;
	bool FocusAlertSequence(int32 Sequence);
	void FocusAlert();
	int32 GetFocusedAlertSequence() const { return FocusedAlertSequence; }

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UConstructionCommandComponent> ConstructionCommands;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProductionCommandComponent> ProductionCommands;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UOrderCommandComponent> OrderCommands;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UMatchCommandComponent> MatchCommands;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPingCommandComponent> PingCommands;
	void SetCommandFeedback(const FString& Message, bool bAccepted);
	void SetPlacementFeedback(const FString& Message, bool bAccepted, ACommandBuilding* Building, uint64 BuildingNetGUID);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void GetSeamlessTravelActorList(bool bToEntry, TArray<AActor*>& ActorList) override;
	virtual void PostSeamlessTravel() override;

private:
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	friend class FBuildBarScenario;
#endif
	UPROPERTY(Transient)
	TObjectPtr<ACommandBuilding> SelectedBuilding;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AArmyGroup>> SelectedForces;
	UPROPERTY(Transient)
	TObjectPtr<AArmyGroup> InspectedForce;
	TWeakObjectPtr<ACommandBuilding> LastClickedBuilding;
	double LastBuildingClickTime = -1.;
	int32 LastForceKey = 0;
	TWeakObjectPtr<AArmyGroup> LastClickedForceCard;
	double LastForceCardClickTime = -1.;
	double LastForceKeyTime = -1.;
	FVector2D SelectionDragStart = FVector2D::ZeroVector;
	bool bSelectionDragging = false;
	bool bSelectionDragAdd = false;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> Actions;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
	FString Feedback;
	double FeedbackStarted = 0.;
	bool bBuildHotkeyPending = false;
	double BuildHotkeyStarted = 0.;
	bool bRepeatPlacement = false;
	bool bPlacingBuilding = false;
	int32 PlacementIndex = -1;
	bool bAssigningOrder = false;
	uint32 AttackInputId = 0;
	bool bHUDExpanded = true;
	bool bPlacementPending = false;
	bool bPlacementCancelled = false;
	TWeakObjectPtr<ACommandBuilding> PendingPlacedBuilding;
	uint64 PendingPlacedBuildingNetGUID = 0;
	EForceVerb PendingVerb = EForceVerb::MoveHold;
	bool bInitialFocusPending = true;
	FVector2D PreviousDragPosition = FVector2D::ZeroVector;
	bool bDragging = false;
	FVector2D PendingPan = FVector2D::ZeroVector;
	ECommandScreen Screen = ECommandScreen::Game;
	ECommandScreen ReturnScreen = ECommandScreen::Game;
	bool bTravelPending = false;
	int32 FocusedAlertSequence = 0;
	int32 LatestAlertSequence = 0;
	void Escape();
	void ShowScreen(ECommandScreen NewScreen);
	void SetFeedback(const FString& Message);
	void PlayUISound(FName Event);
	void PanForward();
	void PanBackward();
	void PanLeft();
	void PanRight();
	FVector2D GetEdgePanAxis() const;
	void ZoomIn();
	void ZoomOut();
	void SelectUnderCursor();
	void FinishSelectionDrag();
	void SelectForce1() { SelectForceNumber(1); }
	void SelectForce2() { SelectForceNumber(2); }
	void SelectForce3() { SelectForceNumber(3); }
	void SelectForce4() { SelectForceNumber(4); }
	void SelectForce5() { SelectForceNumber(5); }
	bool IsOwnedForce(const AArmyGroup* Force) const;
	void CancelPointerMode();
	void CancelMode();
	void ToggleHUD();
	void RequestRestart();
	void ToggleActivePause();
	void PingAtCursor();
	void HandleHUDAction(EHUDAction Action);
	bool IsOwnedBuilding(const ACommandBuilding* Building) const;
	bool CanIssueGameplayCommand();
	bool IsMatchTerminal() const;
	bool CursorHit(FHitResult& Hit) const;
	bool HandleScreenAction(EHUDAction Action);
	bool CursorGround(FVector& Location) const;
	void RightClickAtCursor();
	void SendResolvedOrder(const FOrderInputPreview& Preview, bool bQueue);
	void ResetLocalMatchView();
	void SelectPlacedBuilding();
};