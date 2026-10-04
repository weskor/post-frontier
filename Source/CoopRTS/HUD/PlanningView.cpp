#include "PlanningView.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Content/MatchContent.h"
#include "GameState/GameStatePlanning.h"
#include "GameState/GameStateRegistry.h"
#include "Headquarters.h"

namespace CommandHUDPanels
{
namespace
{
// The ghost accepts the first candidate the placement rules would allow, the server's accept places it.
void GhostBarracks(const ACommandGameState& State, int32 Team, FPlanningGhosts& Out)
{
	const int32 Index = GameStatePlanning::KitBuildingIndex(*State.Content, false);
	const UBuildingDefinition* Barracks = State.Content->Building(Index);
	FString Reason;
	FVector Spot;
	if (!Barracks
		|| !GameStatePlanning::FindDefaultBarracks(State, Team,
			[&](const FVector& Candidate) { return State.ValidateBuildingPlacement(Index, Team, Candidate, Reason); }, Spot))
		return;
	Out.bBarracks = true;
	Out.Barracks = State.ResolveBuildingLocation(Index, Spot, Team);
	Out.BarracksHalf = Barracks->FootprintRadius;
}

void GhostRig(const ACommandGameState& State, int32 Team, FPlanningGhosts& Out)
{
	const int32 Index = GameStatePlanning::KitBuildingIndex(*State.Content, true);
	const UBuildingDefinition* Rig = State.Content->Building(Index);
	FString Reason;
	FVector Site;
	if (!Rig
		|| !GameStatePlanning::FindDefaultRig(State, Team,
			[&](const FVector& Candidate) { return State.ValidateBuildingPlacement(Index, Team, Candidate, Reason); }, Site))
		return;
	Out.bRig = true;
	Out.Rig = Site;
	Out.RigHalf = Rig->FootprintRadius;
}
}

FPlanningView ReadPlanning(const FContext& Context)
{
	FPlanningView View;
	if (!Context.State || !Context.State->IsPlanning())
		return View;
	View.bActive = true;
	View.Remaining = Context.State->Planning.SecondsRemaining;
	for (const FPlanningKit& Kit : Context.State->Planning.Kits)
	{
		++View.Humans;
		View.ReadyHumans += Kit.bReady ? 1 : 0;
	}
	View.Kit = IsValid(Context.Wallet) ? Context.State->FindKit(Context.Wallet) : nullptr;
	if (!View.Kit)
		return View;
	const AHeadquarters* Home = GameStateRegistry::HomeHeadquarters(*Context.State, Context.Wallet->TeamIndex);
	TArray<PlanningPolicy::FRigSite> Sites;
	GameStatePlanning::CollectRigSites(*Context.State, Context.Wallet->TeamIndex, Sites);
	View.bRigSite = Home && PlanningPolicy::NearestRigSite(Sites, Home->GetActorLocation()) != INDEX_NONE;
	View.bBarracksPlaced = IsValid(View.Kit->Barracks);
	View.bRigPlaced = IsValid(View.Kit->Rig);
	View.Defaults = PlanningPolicy::Fill(View.bBarracksPlaced, View.bRigPlaced, View.bRigSite);
	return View;
}

bool IsKitBuilding(const UBuildingDefinition& Definition)
{
	return Definition.bProducesForces || Definition.bRequiresDeposit;
}

FString PlanningPieceName(const FContext& Context, bool bRig)
{
	const UMatchContent* Content = Context.State && IsValid(Context.State->Content) ? Context.State->Content.Get() : nullptr;
	const UBuildingDefinition* Definition = Content ? Content->Building(GameStatePlanning::KitBuildingIndex(*Content, bRig)) : nullptr;
	return Definition ? Definition->DisplayName.ToString() : FString(bRig ? TEXT("Drill Rig") : TEXT("Barracks"));
}

FPlanningGhosts ComputeGhosts(const ACommandGameState& State, const ACommandPlayerState& Commander, const FPlanningKit& Kit)
{
	FPlanningGhosts Ghosts;
	if (!IsValid(State.Content))
		return Ghosts;
	if (!IsValid(Kit.Barracks))
		GhostBarracks(State, Commander.TeamIndex, Ghosts);
	if (!IsValid(Kit.Rig))
		GhostRig(State, Commander.TeamIndex, Ghosts);
	return Ghosts;
}
}
