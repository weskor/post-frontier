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
class UConstructionCommandComponent;
class UProductionCommandComponent;
class UOrderCommandComponent;
class UMatchCommandComponent;

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

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UConstructionCommandComponent> ConstructionCommands;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProductionCommandComponent> ProductionCommands;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UOrderCommandComponent> OrderCommands;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UMatchCommandComponent> MatchCommands;
	void SetCommandFeedback(const FString& Message, bool bAccepted);
	void SetPlacementFeedback(const FString& Message, bool bAccepted);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void GetSeamlessTravelActorList(bool bToEntry, TArray<AActor*>& ActorList) override;
	virtual void PostSeamlessTravel() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<ACommandBuilding> SelectedBuilding;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> Actions;
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
	bool IsOwnedBuilding(const ACommandBuilding* Building) const;
	bool CanIssueGameplayCommand();
	bool IsMatchTerminal() const;
	bool CursorHit(FHitResult& Hit) const;
	bool HandleScreenAction(EHUDAction Action);
	bool CursorGround(FVector& Location) const;
	const AMapRegion* CursorGoalRegion() const;
	void AssignGoalAt(const FVector& Location);
	void ResetLocalMatchView();
};