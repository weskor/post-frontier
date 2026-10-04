#include "GameState/GameStatePlacement.h"

#include "ArenaBounds.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Content/BuildingDefinition.h"
#include "Content/MatchContent.h"
#include "DepositSite.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameState/GameStateRegistry.h"
#include "GameState/GameStateTerritory.h"
#include "GroundHeight.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "NavigationSystem.h"
#include "Rules/GameplayConstants.h"
#include "Rules/PlacementPolicy.h"

namespace
{
using FPlacementRegions = TArray<FPlacementRegion, TInlineAllocator<16>>;

const UBuildingDefinition* DefinitionFor(const ACommandGameState& State, int32 BuildingIndex)
{
	return IsValid(State.Content) ? State.Content->Building(BuildingIndex) : nullptr;
}

void CollectPlacementRegions(const ACommandGameState& State, int32 Team, FPlacementRegions& Out)
{
	Out.Reserve(State.Regions.Num());
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region))
			Out.Add({ Region->RegionIndex, Region->Polygon,
				GameStateTerritory::RegionController(State, Region->RegionIndex), false });
	for (TActorIterator<AArmyUnit> It(State.GetWorld()); It; ++It)
	{
		if (!It->IsAlive() || It->GetTeamIndex() == Team)
			continue;
		const FVector Location = It->GetActorLocation();
		for (FPlacementRegion& Region : Out)
			if (!Region.bContested && PlacementPolicy::ContainsPoint(Region.Polygon, FVector2D(Location.X, Location.Y)))
				Region.bContested = true;
	}
}

FPlacementDeposit DescribeDeposit(const ADepositSite* Deposit, TConstArrayView<FPlacementRegion> Regions)
{
	FPlacementDeposit Candidate{ FVector::ZeroVector, INDEX_NONE, -1, true, false };
	if (!IsValid(Deposit))
		return Candidate;
	Candidate.Position = Deposit->GetActorLocation();
	Candidate.RegionIndex = Deposit->RegionIndex;
	Candidate.bOccupied = IsValid(Deposit->Extractor);
	for (const FPlacementRegion& Region : Regions)
		if (Region.RegionIndex == Deposit->RegionIndex)
		{
			Candidate.ControllingTeam = Region.ControllingTeam;
			Candidate.bContested = Region.bContested;
			break;
		}
	return Candidate;
}

int32 FindFreeDeposit(const ACommandGameState& State, const FVector& RequestedLocation, int32 Team,
	TConstArrayView<FPlacementRegion> Regions)
{
	TArray<FPlacementDeposit, TInlineAllocator<32>> Deposits;
	Deposits.Reserve(State.Deposits.Num());
	for (const ADepositSite* Deposit : State.Deposits)
		Deposits.Add(DescribeDeposit(Deposit, Regions));
	return PlacementPolicy::SelectFreeDeposit(Team, RequestedLocation, Deposits);
}

FVector DepositLocation(const ADepositSite& Deposit, const FVector& RequestedLocation)
{
	const FVector Position = Deposit.GetActorLocation();
	return FVector(Position.X, Position.Y, RequestedLocation.Z);
}

// Requests carry the cursor's z = 0 plane: the building stands on the real ground under its XY (plateau, flat floor).
FVector OnGround(const ACommandGameState& State, const FVector& Location)
{
	const UWorld* World = State.GetWorld();
	return World ? GroundHeight::Snap(*World, Location) : Location;
}

// Footprint inside a controlled, uncontested region, checked without inspecting deposits.
bool IsFreeGroundBuildTerritory(const ACommandGameState& State, const UBuildingDefinition& Definition, int32 Team,
	const FVector& Location)
{
	if (!IsValid(State.Arena) || !State.Arena->ContainsPlacement(Location))
		return false;
	// Grid tint visits every cell: only inspect troops after a polygon actually contains this footprint.
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region) && PlacementPolicy::ContainsFootprint(Region->Polygon, Location, ACommandBuilding::GetFootprintRadius(Definition))
			&& GameStateTerritory::RegionController(State, Region->RegionIndex) == Team
			&& !GameStateTerritory::IsRegionContested(State, Region->RegionIndex, Team))
			return true;
	return false;
}

bool IsDepositBuildTerritory(const ACommandGameState& State, const UBuildingDefinition& Definition, int32 Team,
	const AHeadquarters& Home, const FVector& RequestedLocation)
{
	FPlacementRegions PlacementRegions;
	CollectPlacementRegions(State, Team, PlacementRegions);
	const int32 Index = FindFreeDeposit(State, RequestedLocation, Team, PlacementRegions);
	if (Index == INDEX_NONE)
		return false;
	const ADepositSite* Deposit = State.Deposits[Index];
	const FVector Location = DepositLocation(*Deposit, RequestedLocation);
	const FPlacementDecision Decision = PlacementPolicy::EvaluateTerritory({ Team, Location,
		Home.GetActorLocation(), FVector::ZeroVector, ACommandBuilding::GetFootprintRadius(Definition),
		IsValid(State.Arena) && State.Arena->ContainsPlacement(Location), true, PlacementRegions, {}, {} });
	return Decision.Verdict == EPlacementVerdict::Valid && Deposit->RegionIndex == Decision.RegionIndex;
}
}

namespace
{
struct FPlacementSite
{
	int32 Team = INDEX_NONE;
	float Radius = 0.f;
	FVector Location = FVector::ZeroVector;
	const ADepositSite* Deposit = nullptr;
	FPlacementRegions Regions;
};

const TCHAR* VerdictReason(EPlacementVerdict Verdict)
{
	switch (Verdict)
	{
	case EPlacementVerdict::Valid:
		return nullptr;
	case EPlacementVerdict::Invalid:
		return TEXT("Invalid building, team or match");
	case EPlacementVerdict::OutsideBounds:
		return TEXT("Outside arena bounds");
	case EPlacementVerdict::HeadquartersUnavailable:
		return TEXT("Headquarters unavailable");
	case EPlacementVerdict::EnemyHeadquartersTooClose:
		return TEXT("Too close to enemy headquarters");
	case EPlacementVerdict::Contested:
		return TEXT("Region is contested");
	case EPlacementVerdict::TerritoryRequired:
		return TEXT("Footprint must fit one controlled, uncontested region");
	case EPlacementVerdict::EnemyTroopsTooClose:
		return TEXT("Enemy troops too close");
	case EPlacementVerdict::BuildingOverlap:
		return TEXT("Building footprint overlaps");
	case EPlacementVerdict::HeadquartersTooClose:
		return TEXT("Too close to headquarters");
	}
	return nullptr;
}

// Picks the free deposit that anchors an extractor and moves the site onto it.
bool ResolveDeposit(const ACommandGameState& State, const FVector& RequestedLocation, FPlacementSite& Site, FString& OutReason)
{
	const int32 Index = FindFreeDeposit(State, RequestedLocation, Site.Team, Site.Regions);
	if (Index == INDEX_NONE)
	{
		OutReason = TEXT("Needs a free deposit within 300 cm in a controlled, uncontested region");
		return false;
	}
	Site.Deposit = State.Deposits[Index];
	Site.Location = DepositLocation(*Site.Deposit, RequestedLocation);
	return true;
}

FPlacementDecision EvaluateSite(const ACommandGameState& State, const FPlacementSite& Site)
{
	const AHeadquarters* Home = GameStateRegistry::HomeHeadquarters(State, Site.Team);
	const AHeadquarters* HostileHQ = GameStateRegistry::HomeHeadquarters(State, Site.Team == 0 ? 5 : 0);
	TArray<FPlacementBuilding, TInlineAllocator<32>> PlacementBuildings;
	PlacementBuildings.Reserve(State.Buildings.Num());
	for (const ACommandBuilding* Existing : State.Buildings)
		if (IsValid(Existing) && Existing->GetDefinition())
			PlacementBuildings.Add({ Existing->GetActorLocation(),
				ACommandBuilding::GetFootprintRadius(*Existing->GetDefinition()), Existing->IsAlive() });
	TArray<FVector, TInlineAllocator<64>> EnemyTroops;
	for (TActorIterator<AArmyUnit> It(State.GetWorld()); It; ++It)
		if (It->IsAlive() && It->GetTeamIndex() != Site.Team)
			EnemyTroops.Add(It->GetActorLocation());
	return PlacementPolicy::Evaluate({ Site.Team, Site.Location,
		IsValid(Home) ? Home->GetActorLocation() : FVector::ZeroVector,
		IsValid(HostileHQ) ? HostileHQ->GetActorLocation() : FVector::ZeroVector,
		Site.Radius, IsValid(State.Arena) && State.Arena->ContainsPlacement(Site.Location),
		IsValid(Home) && Home->IsAlive() && IsValid(HostileHQ),
		Site.Regions, PlacementBuildings, EnemyTroops });
}

bool CheckPolicy(const ACommandGameState& State, const FPlacementSite& Site, FString& OutReason)
{
	const FPlacementDecision Decision = EvaluateSite(State, Site);
	if (const TCHAR* Reason = VerdictReason(Decision.Verdict))
	{
		OutReason = Reason;
		return false;
	}
	if (Site.Deposit && Site.Deposit->RegionIndex != Decision.RegionIndex)
	{
		OutReason = TEXT("Extractor footprint must fit its deposit's controlled region");
		return false;
	}
	return true;
}

bool IsFootprintBlocked(UWorld* World, const FPlacementSite& Site)
{
	const FVector Center(Site.Location.X, Site.Location.Y, Site.Location.Z + GameplayConstants::PlacementBoxCentreZ);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BuildingPlacement), false);
	return World->OverlapAnyTestByObjectType(Center, FQuat::Identity, Objects,
		FCollisionShape::MakeBox(FVector(Site.Radius, Site.Radius, GameplayConstants::PlacementBoxHalfZ)), Query);
}

bool HasNavigableGround(UNavigationSystemV1& Navigation, const FPlacementSite& Site)
{
	const FVector Offsets[] = { FVector::ZeroVector, FVector(1, 0, 0), FVector(-1, 0, 0),
		FVector(0, 1, 0), FVector(0, -1, 0), FVector(.707f, .707f, 0),
		FVector(-.707f, .707f, 0), FVector(.707f, -.707f, 0), FVector(-.707f, -.707f, 0) };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Offsets); ++Index)
	{
		const FVector Sample = Site.Location + Offsets[Index] * (Site.Radius + 65.f);
		FNavLocation Projected;
		if (!Navigation.ProjectPointToNavigation(Sample, Projected, FVector(45.f, 45.f, 200.f))
			|| FVector::DistSquared2D(Sample, Projected.Location) > FMath::Square(45.f)
			|| FMath::Abs(Sample.Z - Projected.Location.Z) > GameplayConstants::PlacementZTolerance)
			return false;
	}
	return true;
}

// Clients cannot see the navmesh reliably; only the authority rejects, and the server rechecks.
bool CheckNavigation(const ACommandGameState& State, const FPlacementSite& Site, FString& OutReason)
{
	UWorld* World = State.GetWorld();
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Navigation || !Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		if (State.HasAuthority())
		{
			OutReason = TEXT("Navigation unavailable");
			return false;
		}
		OutReason = TEXT("Navigation checked by server");
		return true;
	}
	if (!HasNavigableGround(*Navigation, Site))
	{
		OutReason = TEXT("Footprint needs clear navigable ground");
		return false;
	}
	OutReason = TEXT("Valid placement");
	return true;
}
}

namespace GameStatePlacement
{
FVector ResolveLocation(const ACommandGameState& State, int32 BuildingIndex, const FVector& RequestedLocation, int32 Team)
{
	const UBuildingDefinition* Definition = DefinitionFor(State, BuildingIndex);
	if (!Definition)
		return RequestedLocation;
	if (!Definition->bRequiresDeposit)
		return OnGround(State, PlacementPolicy::SnapToBuildGrid(RequestedLocation, ACommandBuilding::GetFootprintRadius(*Definition)));
	FPlacementRegions PlacementRegions;
	CollectPlacementRegions(State, Team, PlacementRegions);
	const int32 Index = FindFreeDeposit(State, RequestedLocation, Team, PlacementRegions);
	return OnGround(State, Index == INDEX_NONE ? RequestedLocation : DepositLocation(*State.Deposits[Index], RequestedLocation));
}

bool IsInBuildTerritory(const ACommandGameState& State, int32 BuildingIndex, int32 Team, const FVector& RequestedLocation)
{
	const UBuildingDefinition* Definition = DefinitionFor(State, BuildingIndex);
	if (!Definition || (Team != 0 && Team != 5))
		return false;
	const AHeadquarters* Home = GameStateRegistry::HomeHeadquarters(State, Team);
	if (!IsValid(Home) || !Home->IsAlive())
		return false;
	if (Definition->bRequiresDeposit)
		return IsDepositBuildTerritory(State, *Definition, Team, *Home, RequestedLocation);
	const FVector Location = PlacementPolicy::SnapToBuildGrid(RequestedLocation, ACommandBuilding::GetFootprintRadius(*Definition));
	return IsFreeGroundBuildTerritory(State, *Definition, Team, Location);
}

bool Validate(const ACommandGameState& State, int32 BuildingIndex, int32 Team, const FVector& RequestedLocation, FString& OutReason)
{
	OutReason.Reset();
	UWorld* World = State.GetWorld();
	const UBuildingDefinition* Definition = DefinitionFor(State, BuildingIndex);
	if (!World || State.MatchResult != EMatchResult::Ongoing || (Team != 0 && Team != 5)
		|| !Definition || ACommandBuilding::GetBuildCost(*Definition) == 0)
	{
		OutReason = TEXT("Invalid building, team or match");
		return false;
	}
	FPlacementSite Site;
	Site.Team = Team;
	Site.Radius = ACommandBuilding::GetFootprintRadius(*Definition);
	Site.Location = PlacementPolicy::SnapToBuildGrid(RequestedLocation, Site.Radius);
	CollectPlacementRegions(State, Team, Site.Regions);
	if (Definition->bRequiresDeposit && !ResolveDeposit(State, RequestedLocation, Site, OutReason))
		return false;
	if (!CheckPolicy(State, Site, OutReason))
		return false;
	Site.Location = OnGround(State, Site.Location);
	if (IsFootprintBlocked(World, Site))
	{
		OutReason = TEXT("Footprint blocked by terrain or obstacle");
		return false;
	}
	return CheckNavigation(State, Site, OutReason);
}
}
