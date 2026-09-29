#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CommandPlayerState.h"
#include "CommandHUD.generated.h"

UCLASS()
class COOPRTS_API ACommandHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
	bool IsDoctrinePanelPoint(const FVector2D& Position) const;
	EArmyDoctrine GetDoctrineAtScreenPosition(const FVector2D& Position) const;
};
