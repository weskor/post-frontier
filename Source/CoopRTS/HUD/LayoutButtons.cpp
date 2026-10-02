#include "HUDPanels.h"
#include "CommandPlayerController.h"
#include "CommandGameState.h"
#include "Content/MatchContent.h"
#include "CoopSessionSubsystem.h"
#include "Engine/GameInstance.h"

namespace CommandHUDPanels
{
constexpr EHUDAction BuildActions[] = {
	EHUDAction::BuildSlot0, EHUDAction::BuildSlot1, EHUDAction::BuildSlot2,
	EHUDAction::BuildSlot3, EHUDAction::BuildSlot4, EHUDAction::BuildSlot5
};
constexpr EHUDAction RecipeActions[] = {
	EHUDAction::RecipeSlot0, EHUDAction::RecipeSlot1, EHUDAction::RecipeSlot2,
	EHUDAction::RecipeSlot3, EHUDAction::RecipeSlot4, EHUDAction::RecipeSlot5
};
static EHUDAction ResearchAction(EArmyDoctrine Choice)
{
	return Choice == EArmyDoctrine::SiegeOptics ? EHUDAction::ResearchSiege
		: Choice == EArmyDoctrine::FieldRepairs ? EHUDAction::ResearchRepairs
												: EHUDAction::ResearchEntrenched;
}
static void MainMenuButtons(const FRect& Panel, const UCoopSessionSubsystem* Session, TFunctionRef<void(const FButton&)> Visit)
{
	const float Width = (Panel.W - 88.f) * .5f;
	const EBlock Block = Session && Session->IsBusy() ? EBlock::Chosen : EBlock::None;
	Visit(FButton{ EHUDAction::MapV2, { Panel.X + 40.f, Panel.Y + 246.f, Width, 38.f },
		Block, !Session || Session->IsV2Selected(), 0 });
	Visit(FButton{ EHUDAction::MapClassic, { Panel.X + 48.f + Width, Panel.Y + 246.f, Width, 38.f },
		Block, Session && !Session->IsV2Selected(), 0 });
}

static void PauseButtons(const FRect& Panel, const UCoopSessionSubsystem* Session, TFunctionRef<void(const FButton&)> Visit, TFunctionRef<void(EHUDAction, int32)> Button)
{
	if (Session && Session->IsHosting())
	{
		const float Width = (Panel.W - 88.f) * .5f;
		Visit(FButton{ EHUDAction::Resume, { Panel.X + 40.f, Panel.Y + 300.f, Width, 38.f }, EBlock::None, false, 0 });
		Visit(FButton{ EHUDAction::InviteFriends, { Panel.X + 48.f + Width, Panel.Y + 300.f, Width, 38.f },
			Session->CanInvite() ? EBlock::None : EBlock::Chosen, false, 0 });
	}
	else
		Button(EHUDAction::Resume, 0);
	Button(EHUDAction::Controls, 1);
	Button(EHUDAction::Audio, 2);
	Button(EHUDAction::MainMenu, 3);
	Button(EHUDAction::Quit, 4);
}

static void ScreenButtons(const FContext& Context, const FLayout& Layout, ECommandScreen Screen, TFunctionRef<void(const FButton&)> Visit)
{
	const FRect& Panel = Layout.Screen;
	const UCoopSessionSubsystem* Session = Context.Controller->GetGameInstance()->GetSubsystem<UCoopSessionSubsystem>();
	auto Button = [Visit, &Panel, Session](EHUDAction Action, int32 Row) {
		const bool bAvailable = Action == EHUDAction::HostCoop ? Session && Session->CanHost()
			: Action == EHUDAction::PlaySolo                   ? !Session || !Session->IsBusy()
															   : true;
		Visit(FButton{ Action, { Panel.X + 40.f, Panel.Y + 300.f + Row * 46.f, Panel.W - 80.f, 38.f },
			bAvailable ? EBlock::None : EBlock::Chosen, false, 0 });
	};
	switch (Screen)
	{
	case ECommandScreen::MainMenu:
		MainMenuButtons(Panel, Session, Visit);
		Button(EHUDAction::PlaySolo, 0);
		Button(EHUDAction::HostCoop, 1);
		Button(EHUDAction::Controls, 2);
		Button(EHUDAction::Audio, 3);
		Button(EHUDAction::Quit, 4);
		break;
	case ECommandScreen::Pause:
		PauseButtons(Panel, Session, Visit, Button);
		break;
	case ECommandScreen::Controls:
		Button(EHUDAction::Back, 4);
		break;
	case ECommandScreen::Audio:
		Visit(FButton{ EHUDAction::VolumeDown, { Panel.X + 40.f, Panel.Y + 280.f, 260.f, 44.f }, EBlock::None, false, 0 });
		Visit(FButton{ EHUDAction::VolumeUp, { Panel.Right() - 300.f, Panel.Y + 280.f, 260.f, 44.f }, EBlock::None, false, 0 });
		Button(EHUDAction::Back, 4);
		break;
	case ECommandScreen::ConfirmLeave:
		Button(EHUDAction::ConfirmLeave, 0);
		Button(EHUDAction::Back, 1);
		break;
	case ECommandScreen::ConfirmQuit:
		Button(EHUDAction::ConfirmQuit, 0);
		Button(EHUDAction::Back, 1);
		break;
	case ECommandScreen::Result:
		Button(EHUDAction::Restart, 0);
		Button(EHUDAction::MainMenu, 1);
		Button(EHUDAction::Quit, 2);
		break;
	default:
		break;
	}
}

static void EmitButton(const FContext& Context, TFunctionRef<void(const FButton&)> Visit, EHUDAction Action, const FRect& Rect, int32 Cost, EBlock Lock, bool bActive)
{
	const int32 Shortfall = FMath::Max(0, Cost - Context.Balance);
	const EBlock Block = Context.bTerminal ? EBlock::Terminal : Lock != EBlock::None ? Lock
		: Shortfall > 0                                                              ? EBlock::Funds
																					 : EBlock::None;
	Visit(FButton{ Action, Rect, Block, bActive, Shortfall });
}

static void BuildButtons(const FContext& Context, const FLayout& Layout, TFunctionRef<void(const FButton&)> Visit)
{
	const UMatchContent* Content = MatchContent(Context);
	const int32 BuildCount = Content ? FMath::Min(Content->Buildings.Num(), static_cast<int32>(UE_ARRAY_COUNT(BuildActions))) : 0;
	for (int32 Index = 0; Index < BuildCount; ++Index)
	{
		const UBuildingDefinition* Definition = Content->Building(Index);
		if (Definition)
			EmitButton(Context, Visit, BuildActions[Index], BuildCard(Layout.Build, Index, BuildCount), Definition->BuildCost, EBlock::None,
				Context.Controller->IsPlacingBuilding() && Context.Controller->GetPlacementIndex() == Index);
	}
}

static void ProducerButtons(const FContext& Context, const FLayout& Layout, TFunctionRef<void(const FButton&)> Visit)
{
	const ACommandBuilding* Building = Context.Building;
	const UMatchContent* Content = MatchContent(Context);
	const FRect Recipes = Column(Layout.Inspector, 0, 3);
	const FRect Production = Column(Layout.Inspector, 1, 3);
	FRect Goals = Column(Layout.Inspector, 2, 3);
	// Leave the footer button and an eight-pixel gap below the goal rows.
	if (IsValid(Building->ForceGroup))
		Goals.H -= 12.f;
	const EBlock RoleLock = Building->bForceConfigured ? EBlock::ForceLocked : EBlock::None;
	const UArmyUnitDefinition* Recipe = ProductionDefinition(Context);
	const int32 RecipeCount = Content ? FMath::Min(Content->Units.Num(), static_cast<int32>(UE_ARRAY_COUNT(RecipeActions))) : 0;
	for (int32 Index = 0; Index < RecipeCount; ++Index)
	{
		if (Content->Unit(Index))
			EmitButton(Context, Visit, RecipeActions[Index], Row(Recipes, Index, RecipeCount), 0, RoleLock,
				Building->ProductionUnitIndex == Index || (Building->ProductionUnitIndex == INDEX_NONE && Content->Unit(Index) == Recipe));
	}
	EmitButton(Context, Visit, EHUDAction::ToggleProduction, Row(Production, 1),
		Building->bForceConfigured || !Recipe ? 0 : ACommandBuilding::GetConfigurationCost(*Recipe),
		EBlock::None, Building->bProductionEnabled);
	const EBlock GoalLock = Building->bForceConfigured && IsValid(Building->ForceGroup)
		? EBlock::None
		: EBlock::ForceUnconfigured;
	EmitButton(Context, Visit, EHUDAction::GoalHold, Row(Goals, 0, 4), 0, GoalLock, Building->ForceGoal == EForceGoal::Hold);
	EmitButton(Context, Visit, EHUDAction::GoalExpand, Row(Goals, 1, 4), 0, GoalLock, Building->ForceGoal == EForceGoal::Expand);
	EmitButton(Context, Visit, EHUDAction::GoalAssault, Row(Goals, 2, 4), 0, GoalLock, Building->ForceGoal == EForceGoal::Assault);
	EmitButton(Context, Visit, EHUDAction::GoalFallBack, Row(Goals, 3, 4), 0, GoalLock, Building->ForceGoal == EForceGoal::FallBack);
	if (IsValid(Building->ForceGroup))
		Visit(FButton{ EHUDAction::SelectForce,
			{ Layout.Inspector.Right() - Pad - 150.f, Layout.Inspector.Bottom() - Pad - 22.f, 150.f, 22.f },
			EBlock::None, false, 0 });
}

static void ResearchButtons(const FContext& Context, const FLayout& Layout, TFunctionRef<void(const FButton&)> Visit)
{
	const EArmyDoctrine Owned = Context.Wallet ? Context.Wallet->Doctrine : EArmyDoctrine::None;
	int32 Index = 0;
	for (const EArmyDoctrine Choice : ResearchChoices)
	{
		const bool bOpen = Owned == EArmyDoctrine::None;
		EmitButton(Context, Visit, ResearchAction(Choice), ResearchCard(Layout.Inspector, Index), bOpen ? ACommandBuilding::ResearchCost : 0,
			bOpen ? EBlock::None : EBlock::Chosen, Owned == Choice);
		++Index;
	}
}

void ForEachButton(const FContext& Context, const FLayout& Layout, TFunctionRef<void(const FButton&)> Visit)
{
	if (!Context.Controller || Layout.Scale <= 0.f)
		return;
	const ECommandScreen Screen = Context.Controller->GetUIScreen();
	if (Screen != ECommandScreen::Game)
	{
		ScreenButtons(Context, Layout, Screen, Visit);
		return;
	}
	Visit(FButton{ EHUDAction::Menu, Layout.Menu, EBlock::None, false, 0 });
	const bool bSpent = Context.State && Context.State->IsCoopPauseSpent() && !Context.State->IsActivePaused();
	Visit(FButton{ EHUDAction::ActivePause, Layout.Pause, bSpent ? EBlock::Chosen : EBlock::None,
		Context.State && Context.State->IsActivePaused(), 0 });
	BuildButtons(Context, Layout, Visit);
	if (CanPingInspectedForce(Context))
	{
		EmitButton(Context, Visit, EHUDAction::PingTeammateForce, Row(Column(Layout.Inspector, 0, 3), 1), 0, EBlock::None, false);
		return;
	}
	if (!Context.bExpanded)
		return;
	const ACommandBuilding* Building = Context.Building;
	if (!Building)
		return;
	if (!Building->IsComplete())
	{
		EmitButton(Context, Visit, EHUDAction::CancelConstruction, CancelButton(Layout.Inspector), 0, EBlock::None, false);
		return;
	}
	if (Building->IsProducer())
		ProducerButtons(Context, Layout, Visit);
	else if (Building->GetDefinition() && Building->GetDefinition()->bOffersResearch)
		ResearchButtons(Context, Layout, Visit);
}

}
