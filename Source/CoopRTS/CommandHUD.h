#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HUD/ForceETA.h"
#include "CommandHUD.generated.h"

// Ordinals 0..15 are fixed for offscreen probes and hud_capture.py; append new actions only.
// Build/recipe slot N addresses registry index N, with at most six displayed entries each.
enum class EHUDAction : uint8
{
	None = 0,
	BuildSlot0 = 1,
	BuildSlot1 = 2,
	BuildSlot2 = 3,
	CancelConstruction = 4,
	RecipeSlot0 = 5,
	RecipeSlot1 = 6,
	RecipeSlot2 = 7,
	ToggleProduction = 8,
	ResearchSiege = 12,
	ResearchRepairs = 13,
	ResearchEntrenched = 14,
	Construction = 15,
	BuildSlot3 = 16,
	BuildSlot4 = 17,
	BuildSlot5 = 18,
	RecipeSlot3 = 19,
	RecipeSlot4 = 20,
	RecipeSlot5 = 21,
	PlaySolo = 22,
	Resume = 23,
	Controls = 24,
	Audio = 25,
	Back = 26,
	MainMenu = 27,
	Quit = 28,
	ConfirmLeave = 29,
	ConfirmQuit = 30,
	VolumeDown = 31,
	VolumeUp = 32,
	Menu = 33,
	Restart = 34,
	HostCoop = 35,
	InviteFriends = 36,
	MapV2 = 41,
	MapClassic = 42,
	ActivePause = 43,
	SelectForce = 44,
	PingTeammateForce = 45,
	ForceCardAttack = 46,
	ForceCardRetreat = 47,
	ForceCardProduction = 48,
	ForceCardNever = 49,
	ForceCard25 = 50,
	ForceCard40 = 51,
	ForceCard60 = 52,
	Fortify = 53,
	// The production panel's TIER 2 BRANCH button. Every click goes to the server, whose reason explains a refusal.
	BranchPurchase = 54,
	JevCutChip = 55,
	JevTimelineCell0 = 56,
	JevTimelineCell1 = 57,
	JevTimelineCell2 = 58,
	JevTimelineCell3 = 59,
	// The Team panel (ui.md surface 3): the TEAM opener and the panel's Close, then its teammate rows, resource toggle,
	// amount presets, stepper, Send and log scroll buttons. TeamToggle..TeamLogDown is one contiguous range.
	TeamToggle = 60,
	TeamClose = 61,
	TeamRow0 = 62,
	TeamRow1 = 63,
	TeamRow2 = 64,
	TeamPower = 65,
	TeamData = 66,
	TeamPreset0 = 67,
	TeamPreset1 = 68,
	TeamPreset2 = 69,
	TeamPresetAll = 70,
	TeamStepDown = 71,
	TeamStepUp = 72,
	TeamSend = 73,
	TeamLogUp = 74,
	TeamLogDown = 75,
	// A teammate's read-only force card: opens the Team panel with its owner chosen.
	GiftTeammateForce = 76
};

// Registry indices, independent of the stable action ordinals used by HUD probes.
int32 BuildSlot(EHUDAction Action);
int32 RecipeSlot(EHUDAction Action);

class AArmyGroup;

UCLASS()
class COOPRTS_API ACommandHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void PostRender() override;
	virtual void DrawHUD() override;
	// Drawing and hit testing share one layout computed from the viewport size and local presentation state.
	bool IsPanelPoint(const FVector2D& Position) const;
	// Whether the deck is drawn: requested and either beside the force cards or pinned open.
	bool IsDeckOpen() const;
	EHUDAction GetActionAtScreenPosition(const FVector2D& Position) const;
	AArmyGroup* GetForceCardAtScreenPosition(const FVector2D& Position, EHUDAction& OutAction) const;
	bool FindForceCardScreenPosition(const AArmyGroup* Force, EHUDAction Action, FVector2D& OutPosition) const;
	int32 GetForceETA(const AArmyGroup& Force) const;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	// Screen-space centre of a visible action, including blocked buttons that explain clicks.
	bool FindActionScreenPosition(EHUDAction Action, FVector2D& OutPosition) const;
#endif
	// Visible map badges use the same geometry for rendering, clicks, boxes and probes.
	AArmyGroup* GetForceAtScreenPosition(const FVector2D& Position) const;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	bool FindForceScreenPosition(const AArmyGroup* Force, FVector2D& OutPosition) const;
#endif
	void GetForcesInScreenBox(const FVector2D& Start, const FVector2D& End, TArray<AArmyGroup*>& OutForces) const;
	bool GetMinimapScreenRect(FVector2D& OutOrigin, float& OutSize) const;
	bool GetMinimapWorldPosition(const FVector2D& Position, FVector& OutWorld) const;
	bool GetAlertWorldPosition(const FVector2D& Position, FVector& OutWorld, int32& OutSequence) const;
	bool FindAlertScreenPosition(int32 Sequence, FVector2D& OutPosition) const;
private:
	mutable TArray<ForceTravelETA::FEntry, TInlineAllocator<6>> ForceETACache;
};
