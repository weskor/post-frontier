#include "PlanningView.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Content/MatchContent.h"
#include "DepositSite.h"
#include "GameState/GameStateRegistry.h"
#include "Headquarters.h"
#include "Rules/PlanningHudPolicy.h"

namespace CommandHUDPanels
{
namespace
{
// A free deposit with ore left that the team holds and nobody contests: where a Drill Rig may stand.
void CollectRigSites(const ACommandGameState& State, int32 Team, TArray<PlanningPolicy::FRigSite>& Out)
{
	for (const ADepositSite* Deposit : State.Deposits)
	{
		PlanningPolicy::FRigSite& Site = Out.AddDefaulted_GetRef();
		if (!IsValid(Deposit))
			continue;
		Site.Position = Deposit->GetActorLocation();
		Site.bFree = !IsValid(Deposit->Extractor) && Deposit->Remaining > 0;
		Site.bOwnTerritory = State.GetRegionController(Deposit->RegionIndex) == Team
			&& !State.IsRegionContested(Deposit->RegionIndex, Team);
	}
}

// A free deposit within Clearance of Location in the plane: a Barracks there would crowd the Rig's spot.
bool CrowdsFreeDeposit(const ACommandGameState& State, const FVector& Location, float Clearance)
{
	for (const ADepositSite* Deposit : State.Deposits)
		if (IsValid(Deposit) && Deposit->Remaining > 0 && !IsValid(Deposit->Extractor)
			&& FVector::DistSquared2D(Location, Deposit->GetActorLocation()) < FMath::Square(Clearance))
			return true;
	return false;
}

int32 FirstBuildingWith(const UMatchContent& Content, bool UBuildingDefinition::* Capability)
{
	for (int32 Index = 0; Index < Content.Buildings.Num(); ++Index)
		if (const UBuildingDefinition* Building = Content.Building(Index); Building && Building->*Capability)
			return Index;
	return INDEX_NONE;
}

void GhostBarracks(const ACommandGameState& State, const AHeadquarters& Home, int32 Team, FPlanningGhosts& Out)
{
	const int32 Index = FirstBuildingWith(*State.Content, &UBuildingDefinition::bProducesForces);
	const UBuildingDefinition* Barracks = State.Content->Building(Index);
	if (!Barracks)
		return;
	const float Clearance = Barracks->FootprintRadius * UE_SQRT_2 + 200.f;
	FString Reason;
	for (int32 Spot = 0; Spot < PlanningHud::SpotCount; ++Spot)
	{
		const FVector Candidate = PlanningHud::DefaultBarracksSpot(Home.GetActorLocation(), Team, Spot);
		if (CrowdsFreeDeposit(State, Candidate, Clearance) || !State.ValidateBuildingPlacement(Index, Team, Candidate, Reason))
			continue;
		Out.bBarracks = true;
		Out.Barracks = State.ResolveBuildingLocation(Index, Candidate, Team);
		Out.BarracksHalf = Barracks->FootprintRadius;
		return;
	}
}

void GhostRig(const ACommandGameState& State, const AHeadquarters& Home, int32 Team, FPlanningGhosts& Out)
{
	TArray<PlanningPolicy::FRigSite> Sites;
	CollectRigSites(State, Team, Sites);
	const int32 Best = PlanningPolicy::NearestRigSite(Sites, Home.GetActorLocation());
	const UBuildingDefinition* Rig = State.Content->Building(FirstBuildingWith(*State.Content, &UBuildingDefinition::bRequiresDeposit));
	if (Best == INDEX_NONE || !Rig)
		return;
	Out.bRig = true;
	Out.Rig = Sites[Best].Position;
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
	CollectRigSites(*Context.State, Context.Wallet->TeamIndex, Sites);
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

FPlanningGhosts ComputeGhosts(const ACommandGameState& State, const ACommandPlayerState& Commander, const FPlanningKit& Kit)
{
	FPlanningGhosts Ghosts;
	const AHeadquarters* Home = GameStateRegistry::HomeHeadquarters(State, Commander.TeamIndex);
	if (!Home || !IsValid(State.Content))
		return Ghosts;
	if (!IsValid(Kit.Barracks))
		GhostBarracks(State, *Home, Commander.TeamIndex, Ghosts);
	if (!IsValid(Kit.Rig))
		GhostRig(State, *Home, Commander.TeamIndex, Ghosts);
	return Ghosts;
}
}
