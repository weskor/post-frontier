#include "CommandGameState.h"

#include "ArenaBounds.h"
#include "CapturePoint.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandPlayerState.h"
#include "CoopAudioSubsystem.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "DepositSite.h"
#include "Content/BuildingDefinition.h"
#include "Content/MatchContent.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Rules/PlacementPolicy.h"
#include "Rules/EconomyPolicy.h"
#include "SimulationSettings.h"
#include "CommandPlayerController.h"
#include "CommandGameMode.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"

namespace
{
using FPlacementRegions = TArray<FPlacementRegion, TInlineAllocator<16>>;

const AMapRegion* FindRegion(const ACommandGameState& State, int32 RegionIndex)
{
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->RegionIndex == RegionIndex)
			return Region;
	return nullptr;
}

void CollectPlacementRegions(const ACommandGameState& State, int32 Team, FPlacementRegions& Out)
{
	Out.Reserve(State.Regions.Num());
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region))
			Out.Add({ Region->RegionIndex, Region->Polygon, State.GetRegionController(Region->RegionIndex), false });
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

int32 FindFreeDeposit(const ACommandGameState& State, const FVector& RequestedLocation, int32 Team,
	TConstArrayView<FPlacementRegion> Regions)
{
	TArray<FPlacementDeposit, TInlineAllocator<32>> Deposits;
	Deposits.Reserve(State.Deposits.Num());
	for (const ADepositSite* Deposit : State.Deposits)
	{
		FPlacementDeposit Candidate{ FVector::ZeroVector, INDEX_NONE, -1, true, false };
		if (IsValid(Deposit))
		{
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
		}
		Deposits.Add(Candidate);
	}
	return PlacementPolicy::SelectFreeDeposit(Team, RequestedLocation, Deposits);
}

bool IsPayingExtractor(const ACommandBuilding* Building, const ADepositSite* Deposit)
{
	return IsValid(Building) && Building->IsAlive() && Building->IsComplete()
		&& Building->Kind == EBuildingKind::Extractor && Building->Deposit == Deposit;
}
}

ACommandGameState::ACommandGameState()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void ACommandGameState::BeginPlay()
{
	Super::BeginPlay();
	AudioLiveStartServerTime = GetServerWorldTimeSeconds();
	LastAudioMatchResult = MatchResult;
	bMatchAudioInitialized = true;
	bOutcomeAudioPlayed = MatchResult != EMatchResult::Ongoing;
}

void ACommandGameState::SetMatchResult(EMatchResult Result)
{
	if (!HasAuthority() || MatchResult == Result)
		return;
	MatchResult = Result;
	OnRep_MatchResult();
	ForceNetUpdate();
}

void ACommandGameState::OnRep_MatchResult()
{
	if (!bMatchAudioInitialized)
		return;
	const EMatchResult PreviousResult = LastAudioMatchResult;
	LastAudioMatchResult = MatchResult;
	if (bOutcomeAudioPlayed || PreviousResult != EMatchResult::Ongoing
		|| MatchResult == EMatchResult::Ongoing)
		return;
	bOutcomeAudioPlayed = true;
	if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
		Audio->PlayOutcome(MatchResult == EMatchResult::Victory);
}

void ACommandGameState::AddPlayerState(APlayerState* PlayerState)
{
	// The controllerless enemy is match-local, not a human roster or travel member.
	if (const ACommandPlayerState* Commander = Cast<ACommandPlayerState>(PlayerState))
		if (Commander->TeamIndex == 5)
			return;
	Super::AddPlayerState(PlayerState);
}

bool ACommandGameState::ApplyPause(ACommandPlayerController* Controller, bool bPause)
{
	const bool bCoop = GetNetMode() != NM_Standalone;
	if (bPause)
	{
		ACommandGameMode* Mode = GetWorld()->GetAuthGameMode<ACommandGameMode>();
		if (!PauseBudget.CanPause(bCoop) || !Mode || !Mode->ApplyMatchPause(Controller, true))
			return false;
		PauseBudget.Begin(bCoop, FPlatformTime::Seconds());
	}
	else
	{
		if (!PauseBudget.bPaused)
			return false;
		PauseBudget.Resume();
		if (!bSoloMenuPaused)
			GetWorld()->GetAuthGameMode<ACommandGameMode>()->ApplyMatchPause(Controller, false);
	}
	PublishPauseBudget();
	// Pausing freezes normal replication scheduling; publish the engine pause flag now.
	GetWorld()->GetWorldSettings()->ForceNetUpdate();
	return true;
}

void ACommandGameState::PublishPauseBudget()
{
	bActivePaused = PauseBudget.bPaused;
	bCoopPauseSpent = PauseBudget.bSpent;
	PauseSecondsRemaining = static_cast<float>(PauseBudget.Remaining(FPlatformTime::Seconds()));
	ForceNetUpdate();
}

void ACommandGameState::RefreshSoloMenuPause(ACommandPlayerController* Controller, bool bMenuPaused)
{
	if (!HasAuthority() || GetNetMode() != NM_Standalone)
		return;
	bSoloMenuPaused = bMenuPaused;
	if (ACommandGameMode* Mode = GetWorld()->GetAuthGameMode<ACommandGameMode>())
		Mode->ApplyMatchPause(Controller, bSoloMenuPaused || PauseBudget.bPaused);
}

void ACommandGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && PauseBudget.bPaused)
	{
		const double Now = FPlatformTime::Seconds();
		if (PauseBudget.Expired(Now))
		{
			PauseBudget.Resume();
			GetWorld()->GetAuthGameMode<ACommandGameMode>()->ApplyMatchPause(nullptr, false);
			GetWorld()->GetWorldSettings()->ForceNetUpdate();
		}
		PublishPauseBudget();
		return; // This tick is real-time bookkeeping, never income.
	}
	if (GetWorld()->IsPaused())
		return;
	if (!HasAuthority() || MatchResult != EMatchResult::Ongoing)
		return;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	if (bVerificationIncomePaused)
		return;
#endif
	IncomeElapsed += DeltaSeconds;
	while (IncomeElapsed >= 2.f)
	{
		IncomeElapsed -= 2.f;
		for (APlayerState* Player : PlayerArray)
			if (ACommandPlayerState* Wallet = Cast<ACommandPlayerState>(Player))
				if (Wallet->TeamIndex == 0 && Wallet->CommanderIndex >= 0 && Wallet->CommanderIndex < 5)
					Wallet->AddResources(GetBaselineIncomePerSecond() * 2);
		if (IsValid(EnemyCommander))
		{
			const int32 PaymentTenths = FMath::RoundToInt(GetEnemyBaselineIncomePerSecond() * 20.)
				+ EnemyIncomeRemainderTenths;
			EnemyCommander->AddResources(PaymentTenths / 10);
			EnemyIncomeRemainderTenths = PaymentTenths % 10;
		}
		for (ADepositSite* Deposit : Deposits)
		{
			if (!IsValid(Deposit))
				continue;
			ACommandBuilding* Building = Deposit->Extractor;
			if (!IsPayingExtractor(Building, Deposit))
				continue;
			ACommandPlayerState* Wallet = Building->OwningPlayerState;
			if (!IsValid(Wallet) || Wallet->GetWorld() != GetWorld()
				|| (Building->TeamIndex == 5 && Wallet != EnemyCommander)
				|| (Building->TeamIndex == 0 && !PlayerArray.ContainsByPredicate([Wallet](const TObjectPtr<APlayerState>& Player) { return Player.Get() == Wallet; })))
				continue;
			const FExtractorPayment Payment = EconomyPolicy::ExtractorPayment({ Deposit->RatePerSecond(), Deposit->Remaining, 2, Building->TeamIndex,
				Wallet->CommanderIndex, Wallet->TeamIndex, Wallet->CommanderIndex,
				Building->IsAlive(), Building->IsComplete() });
			if (Payment.Amount == 0)
				continue;
			Wallet->AddResources(Payment.Amount);
			Deposit->Remaining = Payment.Remaining;
			Deposit->ForceNetUpdate();
		}
	}
}

int32 ACommandGameState::GetBaselineIncomePerSecond() const
{
	return FSimulationSettings::ForWorld(GetWorld()).BaselineIncome;
}

double ACommandGameState::GetEnemyBaselineIncomePerSecond() const
{
	int32 HumanCommanders = 0;
	for (const APlayerState* Player : PlayerArray)
		if (const ACommandPlayerState* Commander = Cast<ACommandPlayerState>(Player))
			if (IsValid(Commander) && Commander->TeamIndex == 0
				&& Commander->CommanderIndex >= 0 && Commander->CommanderIndex < 5)
				++HumanCommanders;
	return GetBaselineIncomePerSecond() * EconomyPolicy::JevPlayerCountFactor(HumanCommanders);
}

int32 ACommandGameState::GetIncomePerSecond(const ACommandPlayerState* Commander) const
{
	int32 Income = GetBaselineIncomePerSecond();
	if (!IsValid(Commander))
		return Income;
	// Existing integer estimates floor only JEV's fractional baseline; extractor rates remain unscaled.
	if (Commander == EnemyCommander)
		Income = FMath::FloorToInt(GetEnemyBaselineIncomePerSecond());
	for (const ADepositSite* Deposit : Deposits)
		if (IsValid(Deposit) && Deposit->Remaining > 0 && IsPayingExtractor(Deposit->Extractor, Deposit)
			&& Deposit->Extractor->OwningPlayerState == Commander
			&& Deposit->Extractor->TeamIndex == Commander->TeamIndex)
			Income += Deposit->RatePerSecond();
	return Income;
}

int32 ACommandGameState::GetEnemyIncomePerSecond() const
{
	return GetIncomePerSecond(EnemyCommander);
}

const AMapRegion* ACommandGameState::FindRegionAt(const FVector& Location) const
{
	for (const AMapRegion* Region : Regions)
		if (IsValid(Region) && Region->Contains(Location))
			return Region;
	return nullptr;
}

int32 ACommandGameState::GetRegionController(int32 RegionIndex) const
{
	const AMapRegion* Region = FindRegion(*this, RegionIndex);
	if (!Region)
		return -1;
	const AHeadquarters* Home = Region->HomeTeam == 0 ? FriendlyHeadquarters
		: Region->HomeTeam == 5                       ? EnemyHeadquarters
													  : nullptr;
	return PlacementPolicy::RegionController(Region->RegionRole == ERegionRole::Main, Region->HomeTeam,
		IsValid(Home) && Home->IsAlive(), IsValid(Region->Anchor) ? Region->Anchor->ControllingTeam : -1);
}

bool ACommandGameState::IsRegionContested(int32 RegionIndex, int32 ForTeam) const
{
	const AMapRegion* Region = FindRegion(*this, RegionIndex);
	if (!Region || (ForTeam != 0 && ForTeam != 5) || !GetWorld())
		return false;
	for (TActorIterator<AArmyUnit> It(GetWorld()); It; ++It)
		if (It->IsAlive() && It->GetTeamIndex() != ForTeam && Region->Contains(It->GetActorLocation()))
			return true;
	return false;
}

FVector ACommandGameState::GetRegionAnchor(int32 RegionIndex) const
{
	const AMapRegion* Region = FindRegion(*this, RegionIndex);
	if (!Region)
		return FVector::ZeroVector;
	if (Region->RegionRole != ERegionRole::Main)
		return IsValid(Region->Anchor) ? Region->Anchor->GetActorLocation() : FVector::ZeroVector;
	const AHeadquarters* Home = Region->HomeTeam == 0 ? FriendlyHeadquarters
		: Region->HomeTeam == 5                       ? EnemyHeadquarters
													  : nullptr;
	return IsValid(Home) ? Home->GetActorLocation() : FVector::ZeroVector;
}

FVector ACommandGameState::ResolveBuildingLocation(int32 BuildingIndex, const FVector& RequestedLocation, int32 Team) const
{
	const UBuildingDefinition* Definition = IsValid(Content) ? Content->Building(BuildingIndex) : nullptr;
	if (!Definition)
		return RequestedLocation;
	if (!Definition->bRequiresDeposit)
		return PlacementPolicy::SnapToBuildGrid(RequestedLocation, ACommandBuilding::GetFootprintRadius(*Definition));
	FPlacementRegions PlacementRegions;
	CollectPlacementRegions(*this, Team, PlacementRegions);
	const int32 Index = FindFreeDeposit(*this, RequestedLocation, Team, PlacementRegions);
	if (Index == INDEX_NONE)
		return RequestedLocation;
	const FVector Position = Deposits[Index]->GetActorLocation();
	return FVector(Position.X, Position.Y, RequestedLocation.Z);
}

bool ACommandGameState::IsInBuildTerritory(int32 BuildingIndex, int32 Team, const FVector& RequestedLocation) const
{
	const UBuildingDefinition* Definition = IsValid(Content) ? Content->Building(BuildingIndex) : nullptr;
	if (!Definition || (Team != 0 && Team != 5))
		return false;
	FVector Location = PlacementPolicy::SnapToBuildGrid(RequestedLocation, ACommandBuilding::GetFootprintRadius(*Definition));
	const AHeadquarters* Home = Team == 0 ? FriendlyHeadquarters : EnemyHeadquarters;
	if (!IsValid(Home) || !Home->IsAlive())
		return false;
	if (!Definition->bRequiresDeposit)
	{
		if (!IsValid(Arena) || !Arena->ContainsPlacement(Location))
			return false;
		// Grid tint visits every cell: only inspect troops after a polygon actually contains this footprint.
		for (const AMapRegion* Region : Regions)
			if (IsValid(Region) && PlacementPolicy::ContainsFootprint(Region->Polygon, Location, ACommandBuilding::GetFootprintRadius(*Definition))
				&& GetRegionController(Region->RegionIndex) == Team
				&& !IsRegionContested(Region->RegionIndex, Team))
				return true;
		return false;
	}
	FPlacementRegions PlacementRegions;
	CollectPlacementRegions(*this, Team, PlacementRegions);
	const int32 Index = FindFreeDeposit(*this, RequestedLocation, Team, PlacementRegions);
	if (Index == INDEX_NONE)
		return false;
	const ADepositSite* Deposit = Deposits[Index];
	const FVector Position = Deposit->GetActorLocation();
	Location = FVector(Position.X, Position.Y, RequestedLocation.Z);
	const FPlacementDecision Decision = PlacementPolicy::EvaluateTerritory({ Team, Location,
		Home->GetActorLocation(), FVector::ZeroVector, ACommandBuilding::GetFootprintRadius(*Definition),
		IsValid(Arena) && Arena->ContainsPlacement(Location), true, PlacementRegions, {}, {} });
	return Decision.Verdict == EPlacementVerdict::Valid && Deposit->RegionIndex == Decision.RegionIndex;
}

bool ACommandGameState::ValidateBuildingPlacement(int32 BuildingIndex, int32 Team, const FVector& RequestedLocation, FString& OutReason) const
{
	OutReason.Reset();
	UWorld* World = GetWorld();
	const UBuildingDefinition* Definition = IsValid(Content) ? Content->Building(BuildingIndex) : nullptr;
	if (!World || MatchResult != EMatchResult::Ongoing || (Team != 0 && Team != 5)
		|| !Definition || ACommandBuilding::GetBuildCost(*Definition) == 0)
	{
		OutReason = TEXT("Invalid building, team or match");
		return false;
	}
	const float Radius = ACommandBuilding::GetFootprintRadius(*Definition);
	FVector Location = PlacementPolicy::SnapToBuildGrid(RequestedLocation, Radius);
	const AHeadquarters* Home = Team == 0 ? FriendlyHeadquarters : EnemyHeadquarters;
	const AHeadquarters* HostileHQ = Team == 0 ? EnemyHeadquarters : FriendlyHeadquarters;
	FPlacementRegions PlacementRegions;
	CollectPlacementRegions(*this, Team, PlacementRegions);
	ADepositSite* Deposit = nullptr;
	if (Definition->bRequiresDeposit)
	{
		const int32 Index = FindFreeDeposit(*this, RequestedLocation, Team, PlacementRegions);
		if (Index == INDEX_NONE)
		{
			OutReason = TEXT("Needs a free deposit within 300 cm in a controlled, uncontested region");
			return false;
		}
		Deposit = Deposits[Index];
		const FVector Position = Deposit->GetActorLocation();
		Location = FVector(Position.X, Position.Y, RequestedLocation.Z);
	}
	TArray<FPlacementBuilding, TInlineAllocator<32>> PlacementBuildings;
	PlacementBuildings.Reserve(Buildings.Num());
	for (const ACommandBuilding* Existing : Buildings)
		if (IsValid(Existing) && Existing->GetDefinition())
			PlacementBuildings.Add({ Existing->GetActorLocation(),
				ACommandBuilding::GetFootprintRadius(*Existing->GetDefinition()), Existing->IsAlive() });
	TArray<FVector, TInlineAllocator<64>> EnemyTroops;
	for (TActorIterator<AArmyUnit> It(World); It; ++It)
		if (It->IsAlive() && It->GetTeamIndex() != Team)
			EnemyTroops.Add(It->GetActorLocation());
	const FPlacementDecision Decision = PlacementPolicy::Evaluate({ Team, Location,
		IsValid(Home) ? Home->GetActorLocation() : FVector::ZeroVector,
		IsValid(HostileHQ) ? HostileHQ->GetActorLocation() : FVector::ZeroVector,
		Radius, IsValid(Arena) && Arena->ContainsPlacement(Location),
		IsValid(Home) && Home->IsAlive() && IsValid(HostileHQ),
		PlacementRegions, PlacementBuildings, EnemyTroops });
	switch (Decision.Verdict)
	{
	case EPlacementVerdict::Valid:
		break;
	case EPlacementVerdict::Invalid:
		OutReason = TEXT("Invalid building, team or match");
		return false;
	case EPlacementVerdict::OutsideBounds:
		OutReason = TEXT("Outside arena bounds");
		return false;
	case EPlacementVerdict::HeadquartersUnavailable:
		OutReason = TEXT("Headquarters unavailable");
		return false;
	case EPlacementVerdict::EnemyHeadquartersTooClose:
		OutReason = TEXT("Too close to enemy headquarters");
		return false;
	case EPlacementVerdict::Contested:
		OutReason = TEXT("Region is contested");
		return false;
	case EPlacementVerdict::TerritoryRequired:
		OutReason = TEXT("Footprint must fit one controlled, uncontested region");
		return false;
	case EPlacementVerdict::EnemyTroopsTooClose:
		OutReason = TEXT("Enemy troops too close");
		return false;
	case EPlacementVerdict::BuildingOverlap:
		OutReason = TEXT("Building footprint overlaps");
		return false;
	case EPlacementVerdict::HeadquartersTooClose:
		OutReason = TEXT("Too close to headquarters");
		return false;
	}
	if (Deposit && Deposit->RegionIndex != Decision.RegionIndex)
	{
		OutReason = TEXT("Extractor footprint must fit its deposit's controlled region");
		return false;
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

void ACommandGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACommandGameState, Content);
	DOREPLIFETIME(ACommandGameState, Regions);
	DOREPLIFETIME(ACommandGameState, Deposits);
	DOREPLIFETIME(ACommandGameState, CaptureSites);
	DOREPLIFETIME(ACommandGameState, Buildings);
	DOREPLIFETIME(ACommandGameState, MatchResult);
	DOREPLIFETIME(ACommandGameState, FriendlyHeadquarters);
	DOREPLIFETIME(ACommandGameState, EnemyHeadquarters);
	DOREPLIFETIME(ACommandGameState, Arena);
	DOREPLIFETIME(ACommandGameState, EnemyPlan);
	DOREPLIFETIME(ACommandGameState, EnemyPlanRationale);
	DOREPLIFETIME(ACommandGameState, EnemyCommander);
	DOREPLIFETIME(ACommandGameState, bActivePaused);
	DOREPLIFETIME(ACommandGameState, bCoopPauseSpent);
	DOREPLIFETIME(ACommandGameState, PauseSecondsRemaining);
}
