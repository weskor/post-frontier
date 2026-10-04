#include "CommandPlayerController.h"
#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandCamera.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "Commands/ConstructionCommandComponent.h"
#include "Commands/BranchCommandComponent.h"
#include "Commands/PingCommandComponent.h"
#include "Commands/ProductionCommandComponent.h"
#include "Content/MatchContent.h"
#include "HUD/HUDPanels.h"
#include "InputCoreTypes.h"

void ACommandPlayerController::ToggleHUD()
{
	if (GetUIScreen() != ECommandScreen::Game || bPlacingBuilding || bAssigningOrder)
		return;
	const ACommandHUD* HUD = Cast<ACommandHUD>(GetHUD());
	const bool bOpen = HUD ? HUD->IsDeckOpen() : bHUDExpanded;
	// F4 flips what is drawn: it opens a deck the layout left collapsed, and closes an open one.
	bHUDExpanded = !bOpen;
	bDeckPinned = !bOpen;
}

bool ACommandPlayerController::HandleHUDClick(const FVector2D& Position)
{
	const ACommandHUD* HUD = Cast<ACommandHUD>(GetHUD());
	if (!HUD)
		return false;
	if (GetUIScreen() != ECommandScreen::Game)
	{
		HandleHUDAction(HUD->GetActionAtScreenPosition(Position));
		return true;
	}
	EHUDAction CardAction;
	if (AArmyGroup* Force = HUD->GetForceCardAtScreenPosition(Position, CardAction))
	{
		HandleForceCardClick(Force, CardAction, IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
		return true;
	}
	FVector WorldPosition;
	int32 AlertSequence;
	if (HUD->GetAlertWorldPosition(Position, WorldPosition, AlertSequence))
	{
		FocusAlertSequence(AlertSequence);
		return true;
	}
	if (HUD->GetMinimapWorldPosition(Position, WorldPosition))
	{
		if (HandleFortifyClick(Position) || HandleAttackTargetClick(Position))
			return true;
		bInitialFocusPending = false;
		if (ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn()))
			Camera->FocusOn(WorldPosition);
		return true;
	}
	if (!bPlacingBuilding && !bAssigningOrder && !bFortifyTargeting)
		if (AArmyGroup* Force = HUD->GetForceAtScreenPosition(Position))
		{
			SelectForce(Force, IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
			return true;
		}
	if (!HUD->IsPanelPoint(Position))
		return false;
	HandleHUDAction(HUD->GetActionAtScreenPosition(Position));
	return true;
}

bool ACommandPlayerController::HandleGlobalHUDAction(EHUDAction Action)
{
	if (Action == EHUDAction::ActivePause)
	{
		ToggleActivePause();
		return true;
	}
	if (Action == EHUDAction::Fortify)
	{
		ToggleFortifyTargeting();
		return true;
	}
	if (Action != EHUDAction::PingTeammateForce)
		return false;
	if (AArmyGroup* Force = GetInspectedForce(); IsSelectableForce(Force) && !IsOwnedForce(Force))
		PingCommands->ServerPing(Force->GetCenter(), Force);
	return true;
}

bool ACommandPlayerController::IsHUDActionBlocked(EHUDAction Action)
{
	const CommandHUDPanels::FContext Context = CommandHUDPanels::MakeContext(this);
	const CommandHUDPanels::FLayout Layout = CommandHUDPanels::MakeLayout(Context, 1280.f, 720.f);
	bool bBlocked = false;
	CommandHUDPanels::ForEachButton(Context, Layout, [&](const CommandHUDPanels::FButton& Button) {
		if (Button.Action == Action && !Button.Available())
		{
			bBlocked = true;
			TStringBuilder<128> Reason;
			CommandHUDPanels::BlockReason(Button, Reason);
			SetFeedback(Reason.ToString());
		}
	});
	return bBlocked;
}

void ACommandPlayerController::HandleHUDAction(EHUDAction Action)
{
	if (Action == EHUDAction::None)
		return;
	if (HandleScreenAction(Action))
		return;
	if (GetUIScreen() != ECommandScreen::Game)
		return;
	if (HandleGlobalHUDAction(Action))
		return;
	if (IsHUDActionBlocked(Action))
	{
		PlayUISound(TEXT("Reject"));
		return;
	}
	PlayUISound(TEXT("Click"));
	if (Action == EHUDAction::Construction)
	{
		CancelMode();
		return;
	}
	if (!CanIssueGameplayCommand())
		return;
	const int32 BuildIndex = BuildSlot(Action);
	if (BuildIndex != INDEX_NONE)
	{
		BeginBuildingPlacement(BuildIndex);
		return;
	}
	if (!IsOwnedBuilding(SelectedBuilding))
	{
		SetFeedback(TEXT("Select your building first."));
		return;
	}
	HandleBuildingAction(Action);
}

void ACommandPlayerController::HandleBuildingAction(EHUDAction Action)
{
	if (Action == EHUDAction::SelectForce)
	{
		SelectForce(SelectedBuilding->ForceGroup);
		return;
	}
	if (Action == EHUDAction::CancelConstruction)
	{
		ConstructionCommands->ServerCancelBuilding(SelectedBuilding);
		return;
	}
	if (Action == EHUDAction::BranchPurchase)
	{
		BranchCommands->ServerPurchaseBranch(SelectedBuilding);
		return;
	}
	if (Action == EHUDAction::ResearchSiege || Action == EHUDAction::ResearchRepairs || Action == EHUDAction::ResearchEntrenched)
	{
		ProductionCommands->ServerResearch(SelectedBuilding, Action == EHUDAction::ResearchSiege ? EArmyDoctrine::SiegeOptics : Action == EHUDAction::ResearchRepairs ? EArmyDoctrine::FieldRepairs
																																									  : EArmyDoctrine::EntrenchedFrontline);
		return;
	}
	if (!SelectedBuilding->IsProducer())
		return;
	if (Action == EHUDAction::ToggleProduction)
	{
		ProductionCommands->ServerConfigureProduction(SelectedBuilding, SelectedBuilding->ProductionRole, !SelectedBuilding->bProductionEnabled);
		return;
	}
	const int32 RecipeIndex = RecipeSlot(Action);
	if (RecipeIndex == INDEX_NONE)
		return;
	if (SelectedBuilding->bForceConfigured)
		return; // Locked even while paused; no role-change RPC.
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const UArmyUnitDefinition* Definition = State && IsValid(State->Content) ? State->Content->Unit(RecipeIndex) : nullptr;
	if (!Definition)
		return;
	ProductionCommands->ServerConfigureProduction(SelectedBuilding, Definition->Role, false);
}
