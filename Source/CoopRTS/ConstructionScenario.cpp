#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ConstructionScenario.h"

namespace ConstructionScenarioTests
{
bool FindPlacement(ACommandGameState* State, int32 BuildingIndex, const FVector& Center, FVector& Result)
{
	const UBuildingDefinition* Definition = State->Content->Building(BuildingIndex);
	if (!Definition)
		return false;
	if (Definition->bRequiresDeposit)
	{
		for (ADepositSite* Deposit : State->Deposits)
		{
			if (!IsValid(Deposit) || IsValid(Deposit->Extractor))
				continue;
			const AMapRegion* Region = State->FindRegionAt(Deposit->GetActorLocation());
			const AMapRegion* RequestedRegion = State->FindRegionAt(Center);
			if (!Region || Region != RequestedRegion)
				continue;
			const FVector Point = State->ResolveBuildingLocation(BuildingIndex, Deposit->GetActorLocation());
			FString Reason;
			if (State->ValidateBuildingPlacement(BuildingIndex, 0, Point, Reason))
			{
				Result = Point;
				return true;
			}
		}
		return false;
	}
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
			Point.Z = 5.f;
			Point = State->ResolveBuildingLocation(BuildingIndex, Point);
			if (State->FindRegionAt(Point) != State->FindRegionAt(Center))
				continue;
			FString Reason;
			if (State->ValidateBuildingPlacement(BuildingIndex, 0, Point, Reason))
			{
				Result = Point;
				return true;
			}
		}
	return false;
}
int32 FindForceRegion(ACommandGameState* State, AArmyGroup* Force, const FVector& Preferred,
	const TArray<int32, TInlineAllocator<4>>& Reserved)
{
	uint64 Graph[ForceOrders::MaxRegions];
	const int32 Count = ForceOrderGraph::ReadGraph(*State, Graph);
	const int32 Source = ForceOrderGraph::SourceRegion(*Force, *State);
	int32 Best = INDEX_NONE;
	float Distance = TNumericLimits<float>::Max();
	for (const AMapRegion* Region : State->Regions)
	{
		if (!IsValid(Region) || Region->RegionRole == ERegionRole::Main || Reserved.Contains(Region->RegionIndex)
			|| ForceOrders::NextWaypoint(Graph, Count, Source, Region->RegionIndex) == INDEX_NONE)
			continue;
		const float Candidate = FVector::DistSquared2D(Preferred, State->GetRegionAnchor(Region->RegionIndex));
		if (Candidate < Distance)
		{
			Distance = Candidate;
			Best = Region->RegionIndex;
		}
	}
	return Best;
}
bool FConstructionScenario::Update()
{
	if (CheckDeadline())
		return true;
	UWorld* World = ArmyTestSetup::World();
	if (!World || World->GetTimeSeconds() < 3.f || (Stage == 0 && !ArmyTestSetup::NavigationReady(World)))
		return false;
	ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
	ACommandGameState* State = World->GetGameState<ACommandGameState>();
	ACommandPlayerState* Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
	if (!PC || !State || !Wallet || Wallet->CommanderIndex < 0 || !MapReady(State) || !State->Content)
		return false;
	if (bProduction && Stage >= 2 && (!Attacker.IsValid() || Attacker->GetAliveCount() == 0))
		return Fail(TEXT("Hostile damage fixture lost its living attacker before production damage assertions"));
	if (Stage == 0)
		return StageZero(World, PC, State, Wallet);
	if ((Stage < 8 || Stage == 11) && !Building.IsValid())
		return Fail(TEXT("Production building disappeared"));
	return RunProduction(World, PC, State, Wallet);
}

bool FConstructionScenario::RunProduction(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	switch (Stage)
	{
	case 1:
		return StageOne(World, PC, State, Wallet);
	case 2:
		return StageTwo(World, PC, State, Wallet);
	case 11:
		return StageEleven(World, PC, State, Wallet);
	case 3:
		return StageThree(World, PC, State, Wallet);
	case 4:
		return StageFour(World, PC, State, Wallet);
	case 5:
		return StageFive(World, PC, State, Wallet);
	case 6:
		return StageSix(World, PC, State, Wallet);
	case 7:
		return StageSeven(World, PC, State, Wallet);
	case 8:
		return StageEight(World, PC, State, Wallet);
	case 9:
		return StageNine(World, PC, State, Wallet);
	case 10:
		return StageTen(World, PC, State, Wallet);
	default:
		return false;
	}
}

bool FConstructionScenario::CheckDeadline()
{
	const double Now = FPlatformTime::Seconds();
	if (TimedStage != Stage)
	{
		TimedStage = Stage;
		StageStarted = Now;
	}
	if (Now - Started > 300.0 || Now - StageStarted > 90.0)
	{
		for (const auto& Entry : Forces)
			if (const AArmyGroup* Force = Entry.Get(); IsValid(Force))
			{
				UE_LOG(LogTemp, Error, TEXT("Production deadline stage=%d force=%s joined=%d alive=%d target=%d waypoint=%d destination=%s"),
					Stage, *Force->GetName(), Force->GetJoinedCount(), Force->GetAliveCount(),
					Force->TargetRegionIndex, Force->WaypointRegionIndex, *Force->Destination.ToString());
				for (const AArmyUnit* Unit : Force->GetUnits())
					if (IsValid(Unit) && Unit->IsAlive())
						UE_LOG(LogTemp, Error, TEXT("Production member slot=%d position=%s"),
							Unit->GetCompositionSlot(), *Unit->GetActorLocation().ToString());
			}
		return Fail(*FString::Printf(TEXT("Construction stage %d exceeded its bounded progress deadline"), Stage));
	}
	return false;
}

bool FConstructionScenario::StageZero(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	for (TActorIterator<AEnemyCommander> It(World); It; ++It)
		It->Destroy();
	for (TActorIterator<ACommandBuilding> It(World); It; ++It)
		if (It->TeamIndex == 5)
			It->Destroy();
	State->bVerificationIncomePaused = true;
	Wallet->Resources = 4000; // Budget fixture; configuration and every recruit use real paid authority paths.
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		if (It->GetTeamIndex() == 0)
			return Fail(TEXT("Normal new match must not spawn fixed friendly armies"));
	const UBuildingDefinition* Barracks = State->Content->Building(BarracksIndex);
	if (!Barracks)
		return Fail(TEXT("Match content lacks the barracks definition"));
	FVector Location;
	if (!FindPlacement(State, BarracksIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
		return Fail(TEXT("No valid barracks footprint in HQ construction territory"));
	const FVector Snapped = Location;
	Location += FVector(7.f, -11.f, 0.f);
	return ValidatePlacement(World, PC, State, Wallet, Barracks, Location, Snapped);
}

bool FConstructionScenario::ValidatePlacement(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, const UBuildingDefinition* Barracks, FVector Location, const FVector& Snapped)
{
	if (!Check(Location.X != Snapped.X && Location.Y != Snapped.Y,
			TEXT("Server placement fixture requests explicitly off-grid XY")))
		return true;
	FString RequestedReason, SnappedReason;
	const bool bRequestedValid = State->ValidateBuildingPlacement(BarracksIndex, 0, Location, RequestedReason);
	const bool bSnappedValid = State->ValidateBuildingPlacement(BarracksIndex, 0, Snapped, SnappedReason);
	if (!Check(bRequestedValid && bSnappedValid && RequestedReason == SnappedReason,
			TEXT("Off-grid and snapped valid placements have the same verdict and reason")))
		return true;
	if (!Check(State->IsInBuildTerritory(BarracksIndex, 0, Location)
				&& State->IsInBuildTerritory(BarracksIndex, 0, Snapped),
			TEXT("Off-grid and snapped valid footprints share build territory")))
		return true;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FNavLocation Ground;
	if (!Check(Navigation && Navigation->ProjectPointToNavigation(Snapped, Ground, FVector(45.f, 45.f, 200.f)),
			TEXT("Server placement fixture has navigable snapped ground")))
		return true;
	const int32 Before = Wallet->Resources;
	FString PreviewReason;
	if (!Check(PC->CanPlaceBuildingAt(BarracksIndex, Location, PreviewReason),
			TEXT("Construction preview permits a valid affordable footprint regardless of explanatory text")))
		return true;
	Wallet->Resources = Barracks->BuildCost - 1;
	if (!Check(!PC->CanPlaceBuildingAt(BarracksIndex, Location, PreviewReason),
			TEXT("Construction preview rejects a valid footprint when one resource short")))
		return true;
	Wallet->Resources = Before;
	return BuildPlacement(World, PC, State, Wallet, Barracks, Location, Snapped, Ground, Before, RequestedReason, SnappedReason);
}

bool FConstructionScenario::BuildPlacement(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, const UBuildingDefinition* Barracks, const FVector& Location, const FVector& Snapped, const FNavLocation& Ground, int32 Before, FString& RequestedReason, FString& SnappedReason)
{
	Building = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Location).Building;
	if (!Check(Building.IsValid() && Wallet->Resources == Before - Barracks->BuildCost,
			TEXT("Owned placement creates one paid barracks")))
		return true;
	const FVector BuiltLocation = Building->GetActorLocation();
	if (!Check(BuiltLocation.X == Snapped.X && BuiltLocation.Y == Snapped.Y,
			TEXT("Off-grid server request builds at exact snapped XY, not navigation-projected XY")))
		return true;
	if (!Check(FMath::IsNearlyEqual(BuiltLocation.Z, Ground.Location.Z + 65.f, .01),
			TEXT("Server placement uses navigation ground height plus 65")))
		return true;
	const bool bRequestedOverlap = State->ValidateBuildingPlacement(BarracksIndex, 0, Location, RequestedReason);
	const bool bSnappedOverlap = State->ValidateBuildingPlacement(BarracksIndex, 0, Snapped, SnappedReason);
	if (!Check(!bRequestedOverlap && !bSnappedOverlap && !RequestedReason.IsEmpty() && RequestedReason == SnappedReason,
			TEXT("Off-grid and snapped overlapping placements have the same rejection and reason")))
		return true;
	const FVector OutsideSnapped = PlacementPolicy::SnapToBuildGrid(OutsideArena(State),
		ACommandBuilding::GetFootprintRadius(*Barracks));
	const FVector OutsideRequest = OutsideSnapped + FVector(7.f, -11.f, 0.f);
	if (!Check(!State->IsInBuildTerritory(BarracksIndex, 0, OutsideRequest)
				&& !State->IsInBuildTerritory(BarracksIndex, 0, OutsideSnapped),
			TEXT("Off-grid and snapped outside footprints both lack build territory")))
		return true;
	if (!Check(!Building->IsComplete(), TEXT("Placement begins construction instead of instantly completing")))
		return true;
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Frontline, true);
	if (!Check(!Building->bProductionEnabled, TEXT("Unfinished production rejects activation")))
		return true;
	const int32 After = Wallet->Resources;
	FCommandService::PlaceBuilding(Wallet, BarracksIndex, Location);
	FCommandService::PlaceBuilding(Wallet, BarracksIndex, OutsideArena(State));
	FCommandService::PlaceBuilding(Wallet, 255, Location + FVector(400.f, 0.f, 0.f)); // No such definition index.
	if (!Check(Wallet->Resources == After, TEXT("Overlap, outside territory and invalid definition cannot debit wallet")))
		return true;
	Building->Tick(60.f);
	if (!Check(Building->IsComplete() && Building->Health > 0, TEXT("Game-time construction completes a living building")))
		return true;
	if (!bProduction)
		return Lifecycle(World, PC, State, Wallet);
	Stage = 1;
	return false; // Let the real dynamic navmesh incorporate the new blocker before deployment.
}
}
#endif
