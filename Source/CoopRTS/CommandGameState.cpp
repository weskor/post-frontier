#include "CommandGameState.h"

#include "ArenaBounds.h"
#include "CapturePoint.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandPlayerState.h"
#include "Headquarters.h"
#include "Content/BuildingDefinition.h"
#include "Content/MatchContent.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Rules/PlacementPolicy.h"
#include "Rules/EconomyPolicy.h"

namespace
{
	using FPlacementSectors = TArray<FPlacementSector, TInlineAllocator<16>>;

	void CollectPlacementSectors(const ACommandGameState& State, int32 Team, FPlacementSectors& Out)
	{
		Out.Reserve(State.CaptureSites.Num());
		for (const ACapturePoint* Site : State.CaptureSites)
		{
			FPlacementSector Sector{};
			Sector.ControllingTeam = -1;
			if (IsValid(Site))
			{
				Sector.Position = Site->GetActorLocation();
				Sector.ControllingTeam = Site->ControllingTeam;
				Sector.bEnemyPresent = Site->bEnemyPresent;
				Sector.bFriendlyPresent = Site->bFriendlyPresent;
				Sector.bEstablishedForTeam = Site->IsEstablishedForTeam(Team);
				for (const ACommandBuilding* Existing : State.Buildings)
					if (IsValid(Existing) && Existing->IsAlive() && Existing->Kind == EBuildingKind::Outpost
						&& Existing->OutpostSite == Site)
					{
						Sector.bHasOutpost = true;
						break;
					}
			}
			Out.Add(Sector);
		}
	}
}

ACommandGameState::ACommandGameState()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
}

void ACommandGameState::AddPlayerState(APlayerState* PlayerState)
{
	// The controllerless enemy is match-local, not a human roster or travel member.
	if (const ACommandPlayerState* Commander = Cast<ACommandPlayerState>(PlayerState))
		if (Commander->TeamIndex == 5) return;
	Super::AddPlayerState(PlayerState);
}

void ACommandGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || MatchResult != EMatchResult::Ongoing) return;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	if (bVerificationIncomePaused) return;
#endif
	IncomeElapsed += DeltaSeconds;
	while (IncomeElapsed >= 2.f)
	{
		IncomeElapsed -= 2.f;
		for (APlayerState* Player : PlayerArray)
			if (ACommandPlayerState* Wallet = Cast<ACommandPlayerState>(Player))
				Wallet->AddResources(EconomyPolicy::IncomePerTick(BaselineIncomePerSecond,
					ResourceIncomePerSecond, ControlledResourceSites, 2));
		if (IsValid(EnemyCommander)) EnemyCommander->AddResources(GetEnemyIncomePerSecond() * 2);
	}
}

int32 ACommandGameState::GetEnemyIncomePerSecond() const
{
	int32 Sites = 0;
	for (const ACapturePoint* Site : CaptureSites)
		if (IsValid(Site) && Site->IsEstablishedForTeam(5)) ++Sites;
	return BaselineIncomePerSecond + ResourceIncomePerSecond * Sites;
}

void ACommandGameState::RefreshTerritory()
{
	if (!HasAuthority()) return;
	int32 Sites = 0;
	for (const ACapturePoint* Site : CaptureSites)
		if (IsValid(Site) && Site->IsEstablishedForTeam(0)) ++Sites;
	if (Sites != ControlledResourceSites)
	{
		ControlledResourceSites = Sites;
		ForceNetUpdate();
	}
}

bool ACommandGameState::ValidateBuildingPlacement(int32 BuildingIndex, int32 Team, const FVector& Location, FString& OutReason) const
{
	OutReason.Reset();
	UWorld* World = GetWorld();
	const UBuildingDefinition* Definition = IsValid(Content) ? Content->Building(BuildingIndex) : nullptr;
	if (!World || MatchResult != EMatchResult::Ongoing
		|| !Definition || ACommandBuilding::GetBuildCost(*Definition) == 0)
	{
		OutReason = TEXT("Invalid building, team or match");
		return false;
	}
	const float Radius = ACommandBuilding::GetFootprintRadius(*Definition);
	const AHeadquarters* Home = Team == 0 ? FriendlyHeadquarters : EnemyHeadquarters;
	const AHeadquarters* HostileHQ = Team == 0 ? EnemyHeadquarters : FriendlyHeadquarters;
	FPlacementSectors Sectors;
	CollectPlacementSectors(*this, Team, Sectors);
	TArray<FPlacementBuilding, TInlineAllocator<32>> PlacementBuildings;
	PlacementBuildings.Reserve(Buildings.Num());
	for (const ACommandBuilding* Existing : Buildings)
		if (IsValid(Existing) && Existing->GetDefinition())
			PlacementBuildings.Add({ Existing->GetActorLocation(),
				ACommandBuilding::GetFootprintRadius(*Existing->GetDefinition()), Existing->IsAlive() });
	TArray<FVector, TInlineAllocator<64>> EnemyTroops;
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
	{
		if (It->GetTeamIndex() == Team) continue;
		for (const AArmyUnit* Unit : It->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive()) EnemyTroops.Add(Unit->GetActorLocation());
	}
	const FPlacementDecision Decision = PlacementPolicy::Evaluate({ Team, Location,
		IsValid(Home) ? Home->GetActorLocation() : FVector::ZeroVector,
		IsValid(HostileHQ) ? HostileHQ->GetActorLocation() : FVector::ZeroVector,
		Radius, ACapturePoint::TerritoryRadius, IsValid(Arena) && Arena->ContainsPlacement(Location),
		IsValid(Home) && Home->IsAlive() && IsValid(HostileHQ), Definition->bEstablishesSector,
		Sectors, PlacementBuildings, EnemyTroops });
	switch (Decision.Verdict)
	{
	case EPlacementVerdict::Valid: break;
	case EPlacementVerdict::Invalid: OutReason = TEXT("Invalid building, team or match"); return false;
	case EPlacementVerdict::OutsideBounds: OutReason = TEXT("Outside arena bounds"); return false;
	case EPlacementVerdict::HeadquartersUnavailable: OutReason = TEXT("Headquarters unavailable"); return false;
	case EPlacementVerdict::EnemyHeadquartersTooClose: OutReason = TEXT("Too close to enemy headquarters"); return false;
	case EPlacementVerdict::Contested: OutReason = TEXT("Sector is contested"); return false;
	case EPlacementVerdict::OutpostExists: OutReason = TEXT("Sector already has an outpost"); return false;
	case EPlacementVerdict::CaptureRequired: OutReason = TEXT("Capture a resource sector first"); return false;
	case EPlacementVerdict::TerritoryRequired: OutReason = TEXT("Build inside headquarters or established outpost territory"); return false;
	case EPlacementVerdict::EnemyTroopsTooClose: OutReason = TEXT("Enemy troops too close"); return false;
	case EPlacementVerdict::BuildingOverlap: OutReason = TEXT("Building footprint overlaps"); return false;
	case EPlacementVerdict::HeadquartersTooClose: OutReason = TEXT("Too close to headquarters"); return false;
	}
	const FVector Center(Location.X, Location.Y, Location.Z + 65.f);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BuildingPlacement), false);
	if (World->OverlapAnyTestByObjectType(Center, FQuat::Identity, Objects,
		FCollisionShape::MakeBox(FVector(Radius, Radius, 55.f)), Query))
	{
		OutReason = TEXT("Footprint blocked by terrain or obstacle");
		return false;
	}
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Navigation || !Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		if (HasAuthority())
		{
			OutReason = TEXT("Navigation unavailable");
			return false;
		}
		OutReason = TEXT("Navigation checked by server");
		return true;
	}
	const FVector Offsets[] = { FVector::ZeroVector, FVector(1, 0, 0), FVector(-1, 0, 0),
		FVector(0, 1, 0), FVector(0, -1, 0), FVector(.707f, .707f, 0),
		FVector(-.707f, .707f, 0), FVector(.707f, -.707f, 0), FVector(-.707f, -.707f, 0) };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Offsets); ++Index)
	{
		const FVector Sample = Location + Offsets[Index] * (Radius + 65.f);
		FNavLocation Projected;
		if (!Navigation->ProjectPointToNavigation(Sample, Projected, FVector(45.f, 45.f, 200.f))
			|| FVector::DistSquared2D(Sample, Projected.Location) > FMath::Square(45.f)
			|| FMath::Abs(Sample.Z - Projected.Location.Z) > 110.f)
		{
			OutReason = TEXT("Footprint needs clear navigable ground");
			return false;
		}
	}
	OutReason = TEXT("Valid placement");
	return true;
}

ACommandBuilding* ACommandGameState::TryPlaceBuilding(int32 BuildingIndex, const FVector& Location,
	ACommandPlayerState* Commander, int32 Team, FString& OutReason)
{
	OutReason.Reset();
	if (!HasAuthority())
	{
		OutReason = TEXT("Server authority required");
		return nullptr;
	}
	if (!ValidateBuildingPlacement(BuildingIndex, Team, Location, OutReason)) return nullptr;
	const UBuildingDefinition& Definition = *Content->Building(BuildingIndex);
	if (!IsValid(Commander) || Commander->GetWorld() != GetWorld() || Commander->TeamIndex != Team
		|| (Team == 0 && (Commander->CommanderIndex < 0 || Commander->CommanderIndex >= 5
			|| !PlayerArray.ContainsByPredicate([Commander](const TObjectPtr<APlayerState>& Player)
				{ return Player.Get() == Commander; })))
		|| (Team == 5 && Commander != EnemyCommander))
	{
		OutReason = TEXT("Invalid building owner");
		return nullptr;
	}
	const int32 Cost = ACommandBuilding::GetBuildCost(Definition);
	if (Commander->Resources < Cost)
	{
		OutReason = TEXT("Insufficient resources");
		return nullptr;
	}
	FNavLocation Ground;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Navigation || !Navigation->ProjectPointToNavigation(Location, Ground, FVector(45.f, 45.f, 200.f)))
	{
		OutReason = TEXT("Navigation unavailable");
		return nullptr;
	}
	const FTransform Transform(Ground.Location + FVector(0.f, 0.f, 65.f));
	ACommandBuilding* Building = GetWorld()->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(),
		Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Building)
	{
		OutReason = TEXT("Building spawn failed");
		return nullptr;
	}
	Building->BuildingIndex = BuildingIndex;
	Building->TeamIndex = Team;
	Building->OwningPlayerState = Commander;
	if (Definition.bEstablishesSector)
	{
		FPlacementSectors Sectors;
		CollectPlacementSectors(*this, Team, Sectors);
		const int32 Target = PlacementPolicy::SelectTargetSector(Team, Location, ACapturePoint::TerritoryRadius,
			ACommandBuilding::GetFootprintRadius(Definition), Sectors);
		if (Target != INDEX_NONE) Building->OutpostSite = CaptureSites[Target];
	}
	Building->Health = Building->MaxHealth();
	Building->FrontLocation = Ground.Location;
	Building->FinishSpawning(Transform);
	if (!IsValid(Building))
	{
		OutReason = TEXT("Building spawn failed");
		return nullptr;
	}
	if (!Commander->TrySpend(Cost))
	{
		Building->Destroy();
		OutReason = TEXT("Insufficient resources");
		return nullptr;
	}
	OutReason = TEXT("Construction started");
	return Building;
}

void ACommandGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACommandGameState, Content);
	DOREPLIFETIME(ACommandGameState, ControlledResourceSites);
	DOREPLIFETIME(ACommandGameState, CaptureSites);
	DOREPLIFETIME(ACommandGameState, Buildings);
	DOREPLIFETIME(ACommandGameState, MatchResult);
	DOREPLIFETIME(ACommandGameState, FriendlyHeadquarters);
	DOREPLIFETIME(ACommandGameState, EnemyHeadquarters);
	DOREPLIFETIME(ACommandGameState, Arena);
	DOREPLIFETIME(ACommandGameState, EnemyPlan);
	DOREPLIFETIME(ACommandGameState, EnemyPlanRationale);
	DOREPLIFETIME(ACommandGameState, EnemyCommander);
}
