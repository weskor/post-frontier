#include "HUDPanels.h"
#include "ArmyGroup.h"
#include "CommandPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace CommandHUDPanels
{
void DrawUnitHealthBars(const FPainter& Paint, const FContext& Context)
{
	if (!Context.State)
		return;
	for (TActorIterator<AArmyUnit> It(Context.Controller->GetWorld()); It; ++It)
	{
		const AArmyUnit* Unit = *It;
		const int32 Maximum = Unit->MaxHealth();
		if (!Unit->IsAlive() || Maximum <= 0)
			continue;
		// QuietSeconds is server-only repair state, not a replicated recent-combat clock.
		const bool bSelectedForce = Context.Building && IsValid(Unit->GetGroup())
			&& Unit->GetGroup() == Context.Building->ForceGroup;
		if (Unit->GetHealth() >= Maximum && !bSelectedForce)
			continue;
		FVector2D Screen;
		if (!ProjectOverlay(Paint, Context, Unit->GetActorLocation() + FVector(0.f, 0.f, 135.f), Screen))
			continue;
		const FRect Back{ Screen.X - 18.f, Screen.Y - 11.f, 36.f, 5.f };
		if (!OverlayFits(Paint, Back))
			continue;
		Paint.Fill(Back, FLinearColor(.005f, .008f, .012f, .95f));
		Paint.Bar({ Back.X + 1.f, Back.Y + 1.f, Back.W - 2.f, Back.H - 2.f },
			static_cast<float>(Unit->GetHealth()) / Maximum,
			Unit->GetTeamIndex() == 5 ? Palette::Bad : Palette::Good);
	}
}

}
