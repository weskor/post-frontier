#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ConstructionScenario.h"

namespace ConstructionScenarioTests
{
bool FConstructionScenario::Lifecycle(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
{
	ACommandPlayerController* Other = World->SpawnActor<ACommandPlayerController>();
	ACommandPlayerState* OtherWallet = World->SpawnActor<ACommandPlayerState>();
	if (!Other || !OtherWallet)
		return Fail(TEXT("Other owner fixture could not spawn"));
	Other->SetPlayerState(OtherWallet);
	OtherWallet->CommanderIndex = 1;
	OtherWallet->Resources = 777;
	State->AddPlayerState(OtherWallet);
	const int32 Before = Wallet->Resources;
	FCommandService::ConfigureProduction(OtherWallet, Building.Get(), EUnitRole::Ranged, true);
	FCommandService::CancelBuilding(OtherWallet, Building.Get());
	const int32 Rally = Building->RallyRegionIndex;
	const AMapRegion* Home = State->FindRegionAt(Building->GetActorLocation());
	int32 ForeignRally = INDEX_NONE;
	if (Home)
		for (const int32 Neighbour : Home->Neighbours)
			if (Neighbour != Rally)
			{
				ForeignRally = Neighbour;
				break;
			}
	if (!Check(ForeignRally != INDEX_NONE && ForeignRally != Rally,
			TEXT("Foreign rally fixture targets a different reachable neighbouring region")))
		return true;
	const FCommandResult ForeignResult = FCommandService::SetRallyPoint(OtherWallet, Building.Get(), ForeignRally);
	if (!Check(!ForeignResult.IsAccepted() && Building.IsValid() && !Building->bForceConfigured && !Building->bProductionEnabled
				&& Building->RallyRegionIndex == Rally && Wallet->Resources == Before && OtherWallet->Resources == 777,
			TEXT("Same-team foreign role/rally/cancel commands change neither building nor either wallet")))
		return true;
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, true);
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
	AArmyGroup* ConfiguredForce = Building->ForceGroup;
	if (!Check(FCommandService::SetRallyPoint(Wallet, Building.Get(), ForeignRally).IsAccepted()
				&& Building->RallyRegionIndex == ForeignRally
				&& FCommandService::SetRallyPoint(Wallet, Building.Get(), Rally).IsAccepted(),
			TEXT("The same alternate rally is reachable and accepted for its actual owner")))
		return true;
	FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Siege, true);
	if (!Check(Building->bForceConfigured && Building->ProductionRole == EUnitRole::Ranged
				&& !Building->bProductionEnabled && Building->ForceGroup == ConfiguredForce && Wallet->Resources == Before,
			TEXT("Lifecycle retains first-Start configuration and rejects paused type changes without an upgrade path")))
		return true;
	return LifecycleWorkshop(World, PC, State, Wallet, OtherWallet);
}

bool FConstructionScenario::LifecycleWorkshop(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, ACommandPlayerState* OtherWallet)
{
	FVector Location;
	if (!FindPlacement(State, WorkshopIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
		return Fail(TEXT("No workshop footprint in HQ territory"));
	const FVector Snapped = Location;
	Location += FVector(-9.f, 13.f, 0.f);
	if (!Check(Location.X != Snapped.X && Location.Y != Snapped.Y,
			TEXT("Direct placement fixture requests explicitly off-grid XY")))
		return true;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FNavLocation Ground;
	if (!Check(Navigation && Navigation->ProjectPointToNavigation(Snapped, Ground, FVector(45.f, 45.f, 200.f)),
			TEXT("Direct placement fixture has navigable snapped ground")))
		return true;
	ACommandBuilding* Workshop = FCommandService::PlaceBuilding(Wallet, WorkshopIndex, Location).Building;
	if (!Check(Workshop != nullptr, TEXT("Workshop construction accepted")))
		return true;
	const FVector BuiltLocation = Workshop->GetActorLocation();
	if (!Check(BuiltLocation.X == Snapped.X && BuiltLocation.Y == Snapped.Y,
			TEXT("Off-grid placement request builds the even-cell workshop at exact snapped XY")))
		return true;
	if (!Check(FMath::IsNearlyEqual(BuiltLocation.Z, Ground.Location.Z + 65.f, .01),
			TEXT("Direct placement uses navigation ground height plus 65")))
		return true;
	Workshop->Tick(60.f);
	const int32 ResearchBalance = Wallet->Resources;
	FCommandService::Research(Wallet, Workshop, EArmyDoctrine::SiegeOptics);
	FCommandService::Research(Wallet, Workshop, EArmyDoctrine::FieldRepairs);
	if (!Check(Wallet->Doctrine == EArmyDoctrine::SiegeOptics && Wallet->Resources == ResearchBalance - ACommandBuilding::ResearchCost
				&& OtherWallet->Doctrine == EArmyDoctrine::None,
			TEXT("Research is paid exactly once and scoped to owning commander")))
		return true;
	if (!FindPlacement(State, BarracksIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
		return Fail(TEXT("No cancellation-test footprint"));
	const int32 CancelBalance = Wallet->Resources;
	ACommandBuilding* Cancelled = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Location).Building;
	if (!Check(Cancelled && FCommandService::CancelBuilding(Wallet, Cancelled).IsAccepted() && Wallet->Resources == CancelBalance,
			TEXT("Immediate cancellation refunds unbuilt construction exactly once")))
		return true;
	const int32 AfterCancel = Wallet->Resources;
	FCommandService::CancelBuilding(Wallet, Cancelled);
	if (!Check(Wallet->Resources == AfterCancel, TEXT("Repeated cancellation cannot mint resources")))
		return true;
	return LifecycleCapture(World, PC, State, Wallet, OtherWallet, Location);
}

bool FConstructionScenario::LifecycleCapture(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, ACommandPlayerState* OtherWallet, FVector Location)
{
	ADepositSite* Deposit = nullptr;
	ACapturePoint* Site = nullptr;
	for (ADepositSite* Candidate : State->Deposits)
	{
		if (!IsValid(Candidate) || IsValid(Candidate->Extractor))
			continue;
		for (AMapRegion* Region : State->Regions)
			if (IsValid(Region) && Region->RegionIndex == Candidate->RegionIndex && IsValid(Region->Anchor))
			{
				Deposit = Candidate;
				Site = Region->Anchor;
				break;
			}
		if (Site)
			break;
	}
	if (!Site || !Deposit)
		return Fail(TEXT("No anchored region with a free deposit in map"));
	AArmyGroup* Occupiers = ArmyTestSetup::SpawnGroup(World, PC, 10, Site->GetActorLocation() + FVector(0.f, 0.f, 100.f));
	if (!Check(Occupiers != nullptr, TEXT("Real capture occupants spawn")))
		return true;
	Occupiers->SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Occupiers->GetUnits())
		Unit->SetActorTickEnabled(false);
	Site->AdvanceCapture(20.f);
	if (!Check(Site->ControllingTeam == 0 && State->GetRegionController(Deposit->RegionIndex) == 0
				&& State->GetIncomePerSecond(Wallet) == 2 && State->GetIncomePerSecond(OtherWallet) == 2,
			TEXT("Anchor capture grants polygon rights but no shared income")))
		return true;
	for (AArmyUnit* Unit : Occupiers->GetUnits())
		Unit->SetActorLocation(FromFriendlyHQ(State, 0.f, 0.f, 100.f));
	Site->AdvanceCapture(20.f); // Capture rights persist; clear the deposit footprint before construction.
	if (!FindPlacement(State, BarracksIndex, Site->GetActorLocation(), Location))
		return Fail(TEXT("Captured region without an extractor must permit a barracks footprint"));
	const int32 RegionBuildBalance = Wallet->Resources;
	ACommandBuilding* RegionBarracks = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Location).Building;
	if (!Check(RegionBarracks && State->IsInBuildTerritory(BarracksIndex, 0, Location)
				&& Wallet->Resources == RegionBuildBalance - State->Content->Building(BarracksIndex)->BuildCost
				&& State->GetIncomePerSecond(Wallet) == 2,
			TEXT("Bare capture grants paid barracks placement but only baseline income")))
		return true;
	if (!Check(FCommandService::CancelBuilding(Wallet, RegionBarracks).IsAccepted() && Wallet->Resources == RegionBuildBalance,
			TEXT("Captured-region barracks cancellation refunds its unbuilt cost")))
		return true;
	return LifecycleExtractor(World, PC, State, Wallet, OtherWallet, Location, Deposit, Site, Occupiers);
}

bool FConstructionScenario::LifecycleExtractor(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, ACommandPlayerState* OtherWallet, FVector Location, ADepositSite* Deposit, ACapturePoint* Site, AArmyGroup* Occupiers)
{
	if (!FindPlacement(State, ExtractorIndex, Deposit->GetActorLocation(), Location))
		return Fail(TEXT("No free controlled-region deposit placement"));
	const FVector Requested = Location + FVector(71.f, -63.f, 0.f);
	ACommandBuilding* Extractor = FCommandService::PlaceBuilding(Wallet, ExtractorIndex, Requested).Building;
	if (!Check(Extractor && Extractor->Kind == EBuildingKind::Extractor && IsValid(Extractor->Deposit)
				&& Extractor->Deposit->Extractor == Extractor
				&& Extractor->GetActorLocation().X == Extractor->Deposit->GetActorLocation().X
				&& Extractor->GetActorLocation().Y == Extractor->Deposit->GetActorLocation().Y,
			TEXT("Off-deposit request snaps exact XY and reserves a free deposit")))
		return true;
	Deposit = Extractor->Deposit;
	if (!Check(State->GetIncomePerSecond(Wallet) == 2,
			TEXT("Unfinished extractor reserves deposit without paying income")))
		return true;
	const int32 OccupiedBalance = Wallet->Resources;
	if (!Check(!FCommandService::PlaceBuilding(Wallet, ExtractorIndex, Deposit->GetActorLocation())
				&& Wallet->Resources == OccupiedBalance,
			TEXT("Occupied deposit rejects duplicate placement without debit")))
		return true;
	Extractor->Tick(60.f);
	for (AArmyUnit* Unit : Occupiers->GetUnits())
		Unit->SetActorLocation(FromFriendlyHQ(State, 0.f, 0.f, 100.f));
	Site->AdvanceCapture(20.f);
	if (!Check(Site->ControllingTeam == 0 && !Site->bFriendlyPresent
				&& State->GetRegionController(Deposit->RegionIndex) == 0,
			TEXT("Controlled polygon retains rights after real force departure")))
		return true;
	const int32 Rate = Deposit->RatePerSecond();
	if (!Check(Rate == (Deposit->bRich ? 6 : 4) && Deposit->Remaining == (Deposit->bRich ? EconomyPolicy::RichDepositAmount : EconomyPolicy::NormalDepositAmount),
			TEXT("Deposit kind selects exact finite total and rate")))
		return true;
	const int32 IncomeBefore = Wallet->Resources, OtherBefore = OtherWallet->Resources;
	const int32 RemainingBefore = Deposit->Remaining;
	State->bVerificationIncomePaused = false;
	State->Tick(2.f);
	State->bVerificationIncomePaused = true;
	// Shared pool: both baselines (8) plus the rig (2 x Rate), split evenly whoever built it.
	if (!Check(State->GetIncomePerSecond(Wallet) == 2 + Rate / 2 && State->GetIncomePerSecond(OtherWallet) == 2 + Rate / 2
				&& Wallet->Resources == IncomeBefore + 4 + Rate && OtherWallet->Resources == OtherBefore + 4 + Rate
				&& Deposit->Remaining == RemainingBefore - Rate * 2,
			TEXT("Completed extractor pays both commanders evenly, builder or not, and the deposit drains exactly")))
		return true;
	return LifecycleDepletion(World, PC, State, Wallet, OtherWallet, Deposit, Site, Occupiers, Extractor);
}

bool FConstructionScenario::LifecycleDepletion(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, ACommandPlayerState* OtherWallet, ADepositSite* Deposit, ACapturePoint* Site, AArmyGroup* Occupiers, ACommandBuilding* Extractor)
{
	AArmyGroup* Enemy = SpawnGroup(World, nullptr, -1, HostileStaging(State));
	if (!Enemy)
		return Fail(TEXT("Hostile destruction fixture failed"));
	Enemy->SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Enemy->GetUnits())
		Unit->SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Enemy->GetUnits())
		Unit->SetActorLocation(Site->GetActorLocation() + FVector(0.f, 0.f, 100.f));
	Site->AdvanceCapture(40.f);
	if (!Check(Extractor->IsAlive() && State->GetRegionController(Deposit->RegionIndex) == 5
				&& !State->IsInBuildTerritory(BarracksIndex, 0, Site->GetActorLocation()),
			TEXT("Living extractor cannot lock anchor capture or preserve former owner's polygon rights")))
		return true;
	for (AArmyUnit* Unit : Enemy->GetUnits())
		Unit->SetActorLocation(HostileStaging(State));
	for (AArmyUnit* Unit : Occupiers->GetUnits())
		Unit->SetActorLocation(Site->GetActorLocation() + FVector(0.f, 0.f, 100.f));
	Site->AdvanceCapture(40.f);
	for (AArmyUnit* Unit : Occupiers->GetUnits())
		Unit->SetActorLocation(FromFriendlyHQ(State, 0.f, 0.f, 100.f));
	Site->AdvanceCapture(20.f);
	Deposit->Remaining = 3; // Isolate final partial payment, not claimed natural depletion duration.
	const int32 FinalBefore = Wallet->Resources, FinalOther = OtherWallet->Resources;
	State->bVerificationIncomePaused = false;
	State->Tick(2.f);
	State->Tick(2.f);
	State->bVerificationIncomePaused = true;
	if (!Check(Deposit->Remaining == 0 && State->GetIncomePerSecond(Wallet) == 2
				&& Wallet->Resources == FinalBefore + 5 + 4 && OtherWallet->Resources == FinalOther + 5 + 4,
			TEXT("Final partial payment cannot overdraw deposit; the pool splits 11 then 8, the half carrying over")))
		return true;
	Extractor->ReceiveAttack(Extractor->Health, Enemy->GetUnits()[0]);
	if (!Check(!IsValid(Deposit->Extractor) && Building.IsValid() && Building->OwningPlayerState == Wallet
				&& Site->ControllingTeam == 0 && State->GetIncomePerSecond(Wallet) == 2
				&& State->IsInBuildTerritory(BarracksIndex, 0, Site->GetActorLocation()),
			TEXT("Destroyed extractor frees deposit without changing capture rights or ownership")))
		return true;
	return LifecycleContest(World, PC, State, Wallet, Deposit, Site, Enemy);
}

bool FConstructionScenario::LifecycleContest(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet, ADepositSite* Deposit, ACapturePoint* Site, AArmyGroup* Enemy)
{
	const AMapRegion* Region = State->FindRegionAt(Deposit->GetActorLocation());
	FVector ContestLocation = Deposit->GetActorLocation();
	for (const FVector2D& Vertex : Region->Polygon)
	{
		const FVector Candidate = Deposit->GetActorLocation() * .2f + FVector(Vertex.X, Vertex.Y, 100.f) * .8f;
		if (Region->Contains(Candidate) && FVector::Dist2D(Candidate, Site->GetActorLocation()) > ACapturePoint::CaptureRadius + 100.f)
		{
			ContestLocation = Candidate;
			break;
		}
	}
	if (!Check(FVector::Dist2D(ContestLocation, Site->GetActorLocation()) > ACapturePoint::CaptureRadius,
			TEXT("Contest fixture stands inside polygon but outside capture circle")))
		return true;
	Enemy->GetUnits()[0]->SetActorLocation(ContestLocation);
	if (!Check(State->IsRegionContested(Deposit->RegionIndex, 0)
				&& !State->IsInBuildTerritory(ExtractorIndex, 0, Deposit->GetActorLocation()),
			TEXT("Enemy anywhere in region denies construction even outside anchor radius")))
		return true;
	Test->AddInfo(TEXT("Construction proof: paid snapped placement, owner isolation, research, cancellation, polygon capture/contest, builder-only finite extractor payment, depletion and freeing."));
	return true;
}
}
#endif
