#include "CommandPlayerController.h"
#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandHUD.h"
#include "Commands/PingCommandComponent.h"
#include "EngineUtils.h"
#include "Rules/ControllerInputPolicy.h"

void ACommandPlayerController::PingAtCursor()
{
	float X, Y;
	if (GetMousePosition(X, Y))
		PingAtScreenPosition(FVector2D(X, Y));
}

bool ACommandPlayerController::PingAtScreenPosition(const FVector2D& Position)
{
	if (!CanIssueGameplayCommand())
		return false;
	const ACommandHUD* HUD = Cast<ACommandHUD>(GetHUD());
	FVector Location;
	if (HUD && HUD->GetMinimapWorldPosition(Position, Location))
		return PingMinimapPoint(*HUD, Location);
	if (HUD && HUD->IsPanelPoint(Position))
	{
		if (HUD->GetActionAtScreenPosition(Position) == EHUDAction::PingTeammateForce)
		{
			HandleHUDAction(EHUDAction::PingTeammateForce);
			return true;
		}
		return false;
	}
	return PingWorldPoint(Position);
}

bool ACommandPlayerController::PingMinimapPoint(const ACommandHUD& HUD, const FVector& Location)
{
	AArmyGroup* Teammate = nullptr;
	FVector2D Origin;
	float Size;
	const AArenaBounds* Arena = AArenaBounds::Find(GetWorld());
	if (!Arena || !HUD.GetMinimapScreenRect(Origin, Size))
		return false;
	double Nearest = 3.; // Same diamond radius as the force marker in CommandMinimap.
	const ACommandPlayerState* Viewer = GetPlayerState<ACommandPlayerState>();
	if (!IsValid(Viewer))
		return false;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
	{
		if (It->GetTeamIndex() != Viewer->TeamIndex || !IsValid(It->GetOwningPlayerState())
			|| It->GetOwningPlayerState() == Viewer)
			continue;
		const FVector Delta = It->GetCenter() - Location;
		const double Distance = ControllerInputPolicy::MinimapDiamondDistance(FVector2D(Delta.X, Delta.Y), Arena->HalfExtent, Size);
		if (Distance < Nearest)
		{
			Nearest = Distance;
			Teammate = *It;
		}
	}
	PingCommands->ServerPing(Location, Teammate);
	return true;
}

bool ACommandPlayerController::PingWorldPoint(const FVector2D& Position)
{
	FHitResult Hit;
	GetHitResultAtScreenPosition(Position, ECC_Visibility, true, Hit);
	AArmyGroup* Force = Cast<AArmyGroup>(Hit.GetActor());
	if (const AArmyUnit* Unit = Cast<AArmyUnit>(Hit.GetActor()))
		Force = Unit->IsAlive() ? Unit->GetGroup() : nullptr;
	if (const ACommandBuilding* Producer = Cast<ACommandBuilding>(Hit.GetActor()))
		Force = Producer->IsAlive() && Producer->IsProducer() ? Producer->ForceGroup.Get() : nullptr;
	FVector Origin, Direction, Location;
	if (!DeprojectScreenPositionToWorld(Position.X, Position.Y, Origin, Direction)
		|| !ControllerInputPolicy::GroundPoint(Origin, Direction, Location))
		return false;
	Location.Z = 0.f;
	PingCommands->ServerPing(Location, Force);
	return true;
}
