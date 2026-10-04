#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "VerbInputFixture.h"

namespace VerbInputTests
{
bool FScenario::StageAttackKey()
{
	Key(EKeys::A, IE_Released);
	if (!Check(PC->IsAssigningOrder() && PC->GetPendingVerb() == EForceVerb::Attack && !PC->IsHUDExpanded(),
			TEXT("Real A key enters selected-force Attack mode and collapses the deck"))
		|| !ExerciseAttackPanels() || !ConfirmMinimapAttack())
		return true;
	Key(EKeys::R, IE_Pressed);
	++Stage;
	return false;
}

bool FScenario::StageRetreatKey()
{
	Key(EKeys::R, IE_Released);
	for (AArmyGroup* Force : Forces)
		if (!Check(Force->Verb == EForceVerb::Retreat && Force->Orders.Num() == 1,
				TEXT("Real R key immediately replaces orders with Retreat for every selected force")))
			return true;
	if (!Check(!PC->IsAssigningOrder(), TEXT("Retreat leaves no pointer targeting mode")))
		return true;
	Key(EKeys::A, IE_Pressed);
	++Stage;
	return false;
}

bool FScenario::StageReopenAfterRetreat()
{
	Key(EKeys::A, IE_Released);
	if (!Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded(), TEXT("A can reopen targeting after Retreat with the deck collapsed")))
		return true;
	SaveSerials();
	Key(EKeys::Escape, IE_Pressed);
	++Stage;
	return false;
}

bool FScenario::StageEscape()
{
	Key(EKeys::Escape, IE_Released);
	if (!Check(!PC->IsAssigningOrder() && PC->IsHUDExpanded() && PC->GetUIScreen() == ECommandScreen::Game && Unchanged(),
			TEXT("Real Esc cancels A without ordering or opening pause and restores the deck")))
		return true;
	Key(EKeys::A, IE_Pressed);
	++Stage;
	return false;
}

bool FScenario::StageReopenForRightClick()
{
	Key(EKeys::A, IE_Released);
	if (!Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded(), TEXT("A reopens targeting before right-click cancellation with the deck collapsed")))
		return true;
	Key(EKeys::RightMouseButton, IE_Pressed);
	++Stage;
	return false;
}

bool FScenario::StageRightClick()
{
	Key(EKeys::RightMouseButton, IE_Released);
	if (!Check(!PC->IsAssigningOrder() && PC->IsHUDExpanded() && Unchanged(),
			TEXT("Real right-click cancels A without giving a smart order and restores the deck"))
		|| !Submit(Minimap(State->GetRegionAnchor(Target)), ForceOrderInput::EResolution::MoveHold, EForceVerb::MoveHold, Target))
		return true;
	Key(EKeys::LeftShift, IE_Pressed);
	++Stage;
	return false;
}

bool FScenario::ClickPanel(EHUDAction Action)
{
	FVector2D Point;
	return Check(HUD->FindActionScreenPosition(Action, Point), TEXT("Regression panel action has a visible HUD hit target"))
		&& Check(PC->HandleHUDClick(Point), TEXT("Real HUD click dispatches the panel action during A"));
}

bool FScenario::ExerciseAttackPanels()
{
	SaveSerials();
	PC->CompleteOrderInput(TEXT("Earlier non-targeting order accepted."), true, 0);
	if (!Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded() && Unchanged(),
			TEXT("A late non-targeting acknowledgement cannot close A or reopen its deck")))
		return false;
	if (!Check(!State->IsActivePaused(), TEXT("Panel regression begins with the simulation running"))
		|| !ClickPanel(EHUDAction::ActivePause)
		|| !Check(State->IsActivePaused() && PC->GetWorld()->IsPaused()
				&& PC->IsAssigningOrder() && !PC->IsHUDExpanded() && Unchanged(),
			TEXT("Pause panel pauses simulation while preserving A targeting and collapsed deck without ordering"))
		|| !ClickPanel(EHUDAction::Menu)
		|| !Check(PC->GetUIScreen() == ECommandScreen::Pause && !PC->IsAssigningOrder()
				&& PC->IsHUDExpanded() && State->IsActivePaused() && Unchanged(),
			TEXT("Menu panel cancels A and restores the deck without changing active pause or force orders"))
		|| !ClickPanel(EHUDAction::Resume)
		|| !Check(PC->GetUIScreen() == ECommandScreen::Game && State->IsActivePaused() && PC->GetWorld()->IsPaused(),
			TEXT("Closing the menu does not resume a separately active-paused simulation")))
		return false;
	Key(EKeys::A, IE_Pressed);
	Key(EKeys::A, IE_Released);
	return Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded(), TEXT("A reopens while actively paused"))
		&& ClickPanel(EHUDAction::ActivePause)
		&& Check(!State->IsActivePaused() && !PC->GetWorld()->IsPaused()
				&& PC->IsAssigningOrder() && !PC->IsHUDExpanded() && Unchanged(),
			TEXT("Pause panel resumes simulation while preserving A targeting and force orders"));
}
}
#endif
