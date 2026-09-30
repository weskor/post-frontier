#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CommandHUD.generated.h"

enum class EHUDAction : uint8
{
	None, BuildBarracks, BuildOutpost, BuildWorkshop, CancelConstruction,
	RecipeFrontline, RecipeRanged, RecipeSiege, ToggleProduction,
	FrontSecure, FrontDefend, FrontFallBack,
	ResearchSiege, ResearchRepairs, ResearchEntrenched,
	Construction
};

UCLASS()
class COOPRTS_API ACommandHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
	// Drawing and hit testing share one layout computed from the viewport size and local presentation state.
	bool IsPanelPoint(const FVector2D& Position) const;
	EHUDAction GetActionAtScreenPosition(const FVector2D& Position) const;
	// Screen-space centre of a currently clickable action; false when hidden or unavailable.
	bool FindActionScreenPosition(EHUDAction Action, FVector2D& OutPosition) const;
	bool GetMinimapScreenRect(FVector2D& OutOrigin, float& OutSize) const;
	bool GetMinimapWorldPosition(const FVector2D& Position, FVector& OutWorld) const;
};
