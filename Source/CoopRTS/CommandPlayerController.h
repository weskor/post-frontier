#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "ConstructionTypes.h"
#include "CommandPlayerState.h"
#include "ForceOrders.h"
#include "HUD/OrderInputPreview.h"
#include "HUD/FortifyPreview.h"
#include "HUD/PlanningGhosts.h"
#include "Rules/TeamPanelPolicy.h"
#include "CommandPlayerController.generated.h"

class ACommandBuilding;
class ACommandGameState;
class ACommandHUD;
class AWorldOverlay;
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
class UAbilityCommandComponent;
class UBranchCommandComponent;
class UGiftCommandComponent;
class UPlanningCommandComponent;
enum class EPlanningEdit : uint8;

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
	bool IsDeckPinned() const { return bDeckPinned; }
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
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UAbilityCommandComponent> AbilityCommands;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBranchCommandComponent> BranchCommands;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UGiftCommandComponent> GiftCommands;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPlanningCommandComponent> PlanningCommands;
	void SetCommandFeedback(const FString& Message, bool bAccepted);
	void SetPlacementFeedback(const FString& Message, bool bAccepted, ACommandBuilding* Building, uint64 BuildingNetGUID);
	// Fortify targeting (CommandPlayerControllerFortify.cpp): H or the dock arms it, LMB on ground or minimap casts,
	// H, RMB or Esc cancels. The mode ends on acceptance and stays open on rejection.
	bool IsFortifyTargeting() const { return bFortifyTargeting; }
	void ToggleFortifyTargeting();
	// The region under Position and what a cast there would do, as the cursor chip prints it.
	FFortifyPreview GetFortifyPreview(const FVector2D& Position) const;
	// Casts at Position while targeting; true when the click was consumed.
	bool HandleFortifyClick(const FVector2D& Position);
	void CompleteFortifyInput(const FString& Message, bool bAccepted);
	// Team panel (ui.md surface 3), in CommandPlayerControllerTeam.cpp: Tab or the TEAM button toggles it. The flow, its
	// refusals and the gold dot live here; the gift itself goes through GiftCommands and FCommandService::Gift.
	const TeamPanelPolicy::FFlow& GetTeamFlow() const { return TeamFlow; }
	bool IsTeamPanelOpen() const { return TeamFlow.bOpen; }
	void ToggleTeamPanel();
	// Opens the panel with Slot chosen: the teammate force card's Gift... entry.
	void OpenTeamPanelFor(int32 Slot);
	// Applies a Team panel button; false when Action is not one.
	bool HandleTeamPanelAction(EHUDAction Action);
	// The refusal text under Send and how opaque it still is; false when none is showing.
	bool GetTeamRefusal(FString& OutText, float& OutOpacity) const;
	bool HasUnseenGift() const;
	void CompleteGiftInput(const FString& Message, bool bAccepted);
	// Planning (ui.md surface 10), in CommandPlayerControllerPlanning.cpp: Enter or the READY button toggles Ready, the KIT
	// bar and panel place the kit and pick its unit type, RMB / A queue a first order. Every edit goes through
	// PlanningCommands and FPlanningCommands; the commands refuse what the phase or Ready forbids.
	bool IsPlanningActive() const;
	// Enter or the READY button: Ready, un-Ready, or the one-line question about a kit piece left to its default spot.
	void HandlePlanningReady();
	// Applies a planning button or a KIT card; false when Action is neither (or planning is over).
	bool HandlePlanningAction(EHUDAction Action);
	void CompletePlanningInput(const FString& Message, bool bAccepted, EPlanningEdit Edit);
	// The default-spot ghosts of the kit pieces still unplaced, refreshed a few times a second.
	const CommandHUDPanels::FPlanningGhosts& GetPlanningGhosts() const { return PlanningGhosts; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void GetSeamlessTravelActorList(bool bToEntry, TArray<AActor*>& ActorList) override;
	virtual void PostSeamlessTravel() override;

private:
	void SelectForceCard(AArmyGroup* Force, bool bAdd, bool bDoubleClick);

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
	bool bFortifyTargeting = false;
	// A cast is sent and its verdict has not arrived: further clicks wait.
	bool bFortifyCastPending = false;
	// The Team panel's flow, the refusal under Send, and the newest gift time the commander has seen.
	TeamPanelPolicy::FFlow TeamFlow;
	FString TeamRefusal;
	double TeamRefusalStarted = 0.;
	float GiftSeenThrough = -1.f;
	// A gift is sent and its verdict has not arrived: Send waits.
	bool bGiftPending = false;
	FString PendingGiftText;
	// Planning: when Enter last asked about a kit piece left to its default spot, and the ghosts of those pieces.
	double PlanningConfirmAsked = -1000.;
	CommandHUDPanels::FPlanningGhosts PlanningGhosts;
	double PlanningGhostsAt = -1000.;
	bool bPlanningWasActive = false;
	uint32 AttackInputId = 0;
	bool bHUDExpanded = true;
	// Keeps the deck open over the world when it does not fit beside the force cards.
	bool bDeckPinned = false;
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
	// Camera and per-tick state, in CommandPlayerControllerCamera.cpp and CommandPlayerControllerSelection.cpp.
	void UpdateCamera(float DeltaTime);
	void PruneSelection();
	// Input chords, in CommandPlayerControllerInput.cpp.
	bool HandleBuildChordKey(const FKey& Key);
	void BeginBuildChord();
	// Placement, in CommandPlayerControllerPlacement.cpp.
	void BeginBuildingPlacement(int32 BuildIndex);
	void ClickPlacement();
	// HUD action dispatch, in CommandPlayerControllerHUD.cpp.
	bool HandleGlobalHUDAction(EHUDAction Action);
	// Team panel, in CommandPlayerControllerTeam.cpp.
	void UpdateTeamPanel();
	void SendGift();
	void SetTeamRefusal(const FString& Text);
	int32 TeammateSlotOfRow(int32 Row) const;
	// Planning, in CommandPlayerControllerPlanning.cpp.
	void UpdatePlanning();
	void EndPlanningInput();
	void SuppressPausedMotionBlur(bool bSuppress);
	void SendKitPlacement(const FVector& Location);
	void SendPlanningUnitType(int32 ChipIndex);
	void FocusJevBase();
	// The first order's preview context: one force at the kit's source region carrying the queue, while the kit is editable.
	bool GetPlanningOrderForce(ForceOrderInput::FForce& OutForce) const;
	bool IsHUDActionBlocked(EHUDAction Action);
	void HandleBuildingAction(EHUDAction Action);
	// Menu screens, in CommandPlayerControllerScreens.cpp; each returns true when the action applied.
	bool ApplyMenuWorldAction(EHUDAction Action, ECommandScreen Current);
	bool ApplyNavigationAction(EHUDAction Action, ECommandScreen Current);
	bool ApplyLeaveAction(EHUDAction Action, ECommandScreen Current);
	// Pings, in CommandPlayerControllerPing.cpp.
	bool PingMinimapPoint(const ACommandHUD& HUD, const FVector& Location);
	bool PingWorldPoint(const FVector2D& Position);
	// Order preview targets, in CommandPlayerControllerOrders.cpp.
	AActor* PickMinimapStructure(const ACommandHUD& HUD, const FVector2D& Position, const ACommandGameState& State, int32 Team) const;
	bool PickOrderTarget(const FVector2D& Position, const ACommandGameState& State, AActor*& Structure, FVector& Location) const;
	// World overlay drawn each tick, in CommandPlayerControllerOverlay.cpp.
	void DrawWorldOverlay(AWorldOverlay& Overlay) const;
	void DrawPlacementOverlay(AWorldOverlay& Overlay) const;
	void DrawRegionOverlay(AWorldOverlay& Overlay) const;
	void DrawSelectionOverlay(AWorldOverlay& Overlay) const;
	void DrawFortifyOverlay(AWorldOverlay& Overlay) const;
	void DrawPlanningOverlay(AWorldOverlay& Overlay) const;
};