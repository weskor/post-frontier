#include "CommandPlayerController.h"
#include "CommandBuilding.h"
#include "CommandCamera.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "Commands/PlanningCommandComponent.h"
#include "Commands/PlanningCommands.h"
#include "Content/MatchContent.h"
#include "HUD/HUDPanels.h"
#include "HUD/PlanningPanel.h"
#include "Headquarters.h"
#include "Rules/PlanningHudPolicy.h"
#include "Rules/PlanningPolicy.h"

using namespace CommandHUDPanels;

namespace
{
// The default-spot ghosts are a search over the replicated world; a few refreshes a second are plenty.
constexpr double GhostRefreshSeconds = .5;
}

bool ACommandPlayerController::IsPlanningActive() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return State && State->IsPlanning();
}

void ACommandPlayerController::UpdatePlanning()
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const ACommandPlayerState* Commander = GetPlayerState<ACommandPlayerState>();
	const FPlanningKit* Kit = State && State->IsPlanning() && IsValid(Commander) ? State->FindKit(Commander) : nullptr;
	if (!Kit)
	{
		PlanningGhosts = FPlanningGhosts();
		PlanningGhostsAt = -1000.;
		return;
	}
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (Now - PlanningGhostsAt < GhostRefreshSeconds)
		return;
	PlanningGhostsAt = Now;
	PlanningGhosts = ComputeGhosts(*State, *Commander, *Kit);
}

void ACommandPlayerController::HandlePlanningReady()
{
	const FPlanningView View = ReadPlanning(MakeContext(this));
	const double Now = GetWorld()->GetRealTimeSeconds();
	PlanningHud::FEnterInput In;
	In.bPlanning = View.bActive;
	In.bHasKit = View.Kit != nullptr;
	In.bReady = View.Kit && View.Kit->bReady;
	In.bDefaultsNeeded = View.Defaults.bPlaceBarracks || View.Defaults.bPlaceRig;
	In.bConfirmLive = PlanningHud::IsConfirmLive(Now, PlanningConfirmAsked);
	switch (PlanningHud::Enter(In))
	{
	case PlanningHud::EEnter::Confirm: {
		PlanningConfirmAsked = Now;
		TStringBuilder<128> Question;
		PlanningHud::AppendConfirm(Question, View.Defaults.bPlaceBarracks, View.Defaults.bPlaceRig);
		SetFeedback(Question.ToString());
		break;
	}
	case PlanningHud::EEnter::Ready:
		PlanningConfirmAsked = -1000.;
		PlanningCommands->ServerSetReady(true);
		break;
	case PlanningHud::EEnter::Unready:
		PlanningCommands->ServerSetReady(false);
		break;
	case PlanningHud::EEnter::Ignore:
		break;
	}
}

void ACommandPlayerController::SendKitPlacement(const FVector& Location)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const UBuildingDefinition* Definition = State && IsValid(State->Content) ? State->Content->Building(PlacementIndex) : nullptr;
	if (!Definition || !IsKitBuilding(*Definition))
	{
		SetCommandFeedback(TEXT("Opens at 0:00"), false);
		return;
	}
	// The server owns the verdict: it checks the spot with the old piece out of the way, so a small move never trips over
	// the piece being moved.
	bPlacementPending = true;
	SetFeedback(TEXT("Kit placement sent; the server checks the spot."));
	PlanningCommands->ServerPlaceKit(Definition->GetKind(), Location);
}

void ACommandPlayerController::SendPlanningUnitType(int32 ChipIndex)
{
	const FContext Context = MakeContext(this);
	const FPlanningView View = ReadPlanning(Context);
	if (const TCHAR* Reason = PlanningPolicy::EditRejection(View.bActive, View.Kit && View.Kit->bReady))
	{
		SetCommandFeedback(Reason, false);
		return;
	}
	if (const UArmyUnitDefinition* Unit = PlanningUnit(Context, ChipIndex))
		PlanningCommands->ServerSetUnitType(Unit->Role);
}

void ACommandPlayerController::FocusJevBase()
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	ACommandCamera* Camera = Cast<ACommandCamera>(GetPawn());
	if (!State || !Camera || !IsValid(State->EnemyHeadquarters))
		return;
	bInitialFocusPending = false;
	Camera->FocusOn(State->EnemyHeadquarters->GetActorLocation());
}

bool ACommandPlayerController::GetPlanningOrderForce(ForceOrderInput::FForce& OutForce) const
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const ACommandPlayerState* Commander = GetPlayerState<ACommandPlayerState>();
	const FPlanningKit* Kit = State && State->IsPlanning() && IsValid(Commander) ? State->FindKit(Commander) : nullptr;
	if (!Kit || Kit->bReady)
		return false;
	OutForce = { true, FPlanningCommands::SourceRegion(*State, *Kit), Kit->Orders.Num() };
	return true;
}

bool ACommandPlayerController::HandlePlanningAction(EHUDAction Action)
{
	if (!IsPlanningActive())
		return false;
	const int32 Slot = BuildSlot(Action);
	if (Slot != INDEX_NONE)
	{
		const FContext Context = MakeContext(this);
		const UMatchContent* Content = MatchContent(Context);
		const UBuildingDefinition* Definition = Content ? Content->Building(Slot) : nullptr;
		const FPlanningView View = ReadPlanning(Context);
		const TCHAR* Locked = PlanningPolicy::EditRejection(View.bActive, View.Kit && View.Kit->bReady);
		if (!Definition || !IsKitBuilding(*Definition) || Locked)
		{
			SetCommandFeedback(Definition && IsKitBuilding(*Definition) ? Locked : TEXT("Opens at 0:00"), false);
			return true;
		}
		PlayUISound(TEXT("Click"));
		CancelMode();
		BeginBuildingPlacement(Slot);
		return true;
	}
	if (Action >= EHUDAction::PlanUnit0 && Action <= EHUDAction::PlanUnit4)
		SendPlanningUnitType(static_cast<int32>(Action) - static_cast<int32>(EHUDAction::PlanUnit0));
	else if (Action == EHUDAction::PlanReady)
		HandlePlanningReady();
	else if (Action == EHUDAction::PlanLookJev)
		FocusJevBase();
	else if (Action == EHUDAction::PlanClearOrders)
		PlanningCommands->ServerClearFirstOrders();
	else
		return Action == EHUDAction::PlanPanel;
	PlayUISound(TEXT("Click"));
	return true;
}

void ACommandPlayerController::CompletePlanningInput(const FString& Message, bool bAccepted, EPlanningEdit Edit)
{
	if (Edit == EPlanningEdit::Kit)
	{
		bPlacementPending = false;
		SetFeedback(Message);
		// A placement cancelled while it was in flight keeps its answer out of the mode.
		if (bPlacementCancelled)
		{
			bPlacementCancelled = false;
			return;
		}
		if (bAccepted)
		{
			bPlacingBuilding = false;
			bRepeatPlacement = false;
			PlanningConfirmAsked = -1000.;
		}
		else
			PlayUISound(TEXT("Reject"));
		return;
	}
	if (Edit == EPlanningEdit::FirstOrder && bAccepted)
		bAssigningOrder = false;
	SetCommandFeedback(Message, bAccepted);
}
