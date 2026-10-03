#include "CommandPlayerController.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "CommandBuilding.h"
#include "CombatTarget.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "Engine/World.h"

bool ACommandPlayerController::GetHoveredForceOrder(FForceOrder& OutOrder, bool& bQueue) const
{
	if (GetUIScreen() != ECommandScreen::Game || IsPlacingBuilding() || SelectedForces.IsEmpty())
		return false;
	const AMapRegion* Region = CursorOrderRegion();
	if (!Region)
		return false;
	const ACommandPlayerState* Player = GetPlayerState<ACommandPlayerState>();
	if (!Player)
		return false;
	OutOrder = FForceOrder(IsAssigningOrder() ? GetPendingVerb() : EForceVerb::MoveHold, Region->RegionIndex);
	bQueue = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	float X, Y;
	FVector Location;
	const ACommandHUD* HUD = Cast<ACommandHUD>(GetHUD());
	const bool bMinimap = HUD && GetMousePosition(X, Y) && HUD->GetMinimapWorldPosition(FVector2D(X, Y), Location);
	FHitResult Hit;
	if (!IsAssigningOrder() && !bMinimap && CursorHit(Hit)
		&& (Cast<ACommandBuilding>(Hit.GetActor()) || Cast<AHeadquarters>(Hit.GetActor()))
		&& CombatTarget::IsAliveHostile(Hit.GetActor(), Player->TeamIndex))
	{
		const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		const AMapRegion* StructureRegion = State ? State->FindRegionAt(Hit.GetActor()->GetActorLocation()) : nullptr;
		if (!StructureRegion)
			return false;
		OutOrder = FForceOrder(EForceVerb::Attack, StructureRegion->RegionIndex, Hit.GetActor());
	}
	return OutOrder.Verb != EForceVerb::Retreat;
}
