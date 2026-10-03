#include "CommandPlayerController.h"
#include "CommandHUD.h"
#include "CommandCamera.h"
#include "CommandBuilding.h"
#include "Commands/OrderCommandComponent.h"
#include "Commands/ProductionCommandComponent.h"
#include "Commands/PingCommandComponent.h"
#include "HAL/PlatformTime.h"

void ACommandPlayerController::SelectForceCard(AArmyGroup* Force, bool bAdd, bool bDoubleClick)
{
	const double Now = FPlatformTime::Seconds();
	const bool bFocus = bDoubleClick || (LastClickedForceCard.Get() == Force && Now - LastForceCardClickTime <= .3);
	if (!bAdd || !IsForceSelected(Force))
		SelectForce(Force, bAdd);
	LastClickedForceCard = bFocus ? nullptr : Force;
	LastForceCardClickTime = Now;
	if (bFocus)
		if (ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn()))
			Camera->FocusOn(Force->GetCenter());
}

void ACommandPlayerController::HandleForceCardClick(AArmyGroup* Force, EHUDAction Action, bool bAdd, bool bDoubleClick)
{
	if (GetUIScreen() != ECommandScreen::Game || !IsSelectableForce(Force))
		return;
	if (!IsOwnedForce(Force))
	{
		if (Action == EHUDAction::PingTeammateForce)
			PingCommands->ServerPing(Force->GetCenter(), Force);
		return;
	}
	if (Action == EHUDAction::None)
	{
		SelectForceCard(Force, bAdd, bDoubleClick);
		return;
	}
	LastClickedForceCard.Reset();
	if (!CanIssueGameplayCommand())
		return;
	if (Action == EHUDAction::ForceCardAttack || Action == EHUDAction::ForceCardRetreat)
	{
		SelectForce(Force);
		if (Action == EHUDAction::ForceCardAttack)
			BeginForceAttack();
		else
			RetreatSelectedForces(bAdd);
		return;
	}
	if (Action == EHUDAction::ForceCardProduction)
	{
		ACommandBuilding* Producer = Force->GetProductionBuilding();
		if (IsOwnedBuilding(Producer) && Producer->IsComplete() && Producer->bForceConfigured)
			ProductionCommands->ServerConfigureProduction(Producer, Producer->ProductionRole, !Producer->bProductionEnabled);
		return;
	}
	ERetreatThreshold Threshold;
	switch (Action)
	{
	case EHUDAction::ForceCardNever:
		Threshold = ERetreatThreshold::Never;
		break;
	case EHUDAction::ForceCard25:
		Threshold = ERetreatThreshold::Percent25;
		break;
	case EHUDAction::ForceCard40:
		Threshold = ERetreatThreshold::Percent40;
		break;
	case EHUDAction::ForceCard60:
		Threshold = ERetreatThreshold::Percent60;
		break;
	default:
		return;
	}
	OrderCommands->ServerSetRetreatThreshold({ Force }, Threshold);
}
