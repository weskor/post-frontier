#include "EnemyCommander.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "DepositSite.h"
#include "MapRegion.h"
#include "Content/MatchContent.h"
#include "Headquarters.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
int32 FirstBuildingWith(const UMatchContent& Content, bool UBuildingDefinition::* Capability)
{
	for (int32 Index = 0; Index < Content.Buildings.Num(); ++Index)
		if (const UBuildingDefinition* Building = Content.Building(Index); Building && Building->*Capability)
			return Index;
	return INDEX_NONE;
}

bool HasCapability(const ACommandBuilding& Building, bool UBuildingDefinition::* Capability)
{
	const UBuildingDefinition* Definition = Building.GetDefinition();
	return Definition && Definition->*Capability;
}

bool ValidRegion(int32 Index) { return Index >= 0 && Index < ForceGoals::MaxRegions; }

// Graph distances also exclude unreachable expansion targets. Fixed storage matches the goal driver.
void Distances(const AMapRegion* const* Regions, int32 Source, int32* Distance)
{
	for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index)
		Distance[Index] = INDEX_NONE;
	if (!ValidRegion(Source) || !Regions[Source])
		return;
	int32 Queue[ForceGoals::MaxRegions];
	int32 Read = 0, Write = 0;
	Queue[Write++] = Source;
	Distance[Source] = 0;
	while (Read < Write)
	{
		const int32 Current = Queue[Read++];
		for (int32 Next = 0; Next < ForceGoals::MaxRegions; ++Next)
			if (Regions[Next] && Distance[Next] == INDEX_NONE && Regions[Current]->Neighbours.Contains(Next))
			{
				Distance[Next] = Distance[Current] + 1;
				Queue[Write++] = Next;
			}
	}
}
}

AEnemyCommander::AEnemyCommander()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
}

void AEnemyCommander::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || (EvaluateElapsed += DeltaSeconds) < 2.f)
		return;
	EvaluateElapsed = FMath::Fmod(EvaluateElapsed, 2.f);
	EvaluatePlan();
}

ACommandBuilding* AEnemyCommander::BuildNear(ACommandGameState* State, int32 BuildingIndex, const FVector& Center)
{
	const UBuildingDefinition* Definition = State->Content->Building(BuildingIndex);
	if (!Definition || !IsValid(Commander) || Commander->Resources < Definition->BuildCost)
		return nullptr;
	// Preserve the complete force's rally footprint, not just its empty-force centre.
	// 200 cm covers the six-unit formation extent plus the navigation agent margin.
	const float ClearanceSquared = FMath::Square(Definition->FootprintRadius * UE_SQRT_2 + 200.f);
	const auto KeepsRalliesClear = [&](const FVector& Location) {
		for (const ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->IsAlive() && Building->OwningPlayerState == Commander
				&& IsValid(Building->ForceGroup)
				&& FVector::DistSquared2D(Location, Building->ForceGroup->GetHomeLocation()) < ClearanceSquared)
				return false;
		return true;
	};
	if (Definition->bRequiresDeposit)
	{
		if (!KeepsRalliesClear(Center))
			return nullptr;
		FString Reason;
		return State->TryPlaceBuilding(BuildingIndex, Center, Commander, TeamIndex, Reason);
	}
	const AMapRegion* Region = State->FindRegionAt(Center);
	if (!Region)
		return nullptr;
	// Mirror the candidate set with the side, rather than giving team 0 a different search bias.
	const float Orientation = TeamIndex == 5 ? -1.f : 1.f;
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			FVector Location = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * ((380.f + Ring * 160.f) * Orientation);
			Location.Z = 5.f;
			Location = State->ResolveBuildingLocation(BuildingIndex, Location, TeamIndex);
			if (State->FindRegionAt(Location) != Region)
				continue;
			if (!KeepsRalliesClear(Location))
				continue;
			bool bBlocksDeposit = false;
			for (const ADepositSite* Deposit : State->Deposits)
				if (IsValid(Deposit) && Deposit->Remaining > 0 && !IsValid(Deposit->Extractor)
					&& FVector::DistSquared2D(Location, Deposit->GetActorLocation()) < ClearanceSquared)
				{
					bBlocksDeposit = true;
					break;
				}
			if (bBlocksDeposit)
				continue;
			FString Reason;
			if (ACommandBuilding* Building = State->TryPlaceBuilding(BuildingIndex, Location, Commander, TeamIndex, Reason))
				return Building;
		}
	return nullptr;
}

void AEnemyCommander::EvaluatePlan()
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !State || !State->Content || State->MatchResult != EMatchResult::Ongoing
		|| (TeamIndex != 0 && TeamIndex != 5) || !IsValid(State->FriendlyHeadquarters) || !IsValid(State->EnemyHeadquarters))
		return;
	if (!IsValid(Commander) && TeamIndex == 5)
		Commander = State->EnemyCommander;
	if (!IsValid(Commander) || Commander->TeamIndex != TeamIndex)
		return;
	const UMatchContent& Content = *State->Content;
	const int32 ProducerIndex = FirstBuildingWith(Content, &UBuildingDefinition::bProducesForces);
	const int32 ExtractorIndex = FirstBuildingWith(Content, &UBuildingDefinition::bRequiresDeposit);
	const int32 WorkshopIndex = FirstBuildingWith(Content, &UBuildingDefinition::bOffersResearch);
	const int32 FrontlineIndex = Content.UnitIndexForRole(EUnitRole::Frontline);
	const int32 RangedIndex = Content.UnitIndexForRole(EUnitRole::Ranged);
	const int32 SiegeIndex = Content.UnitIndexForRole(EUnitRole::Siege);
	if (ProducerIndex < 0 || FrontlineIndex < 0 || RangedIndex < 0 || SiegeIndex < 0)
		return;
	const UArmyUnitDefinition& Infantry = *Content.Unit(FrontlineIndex);
	const FVector Home = (TeamIndex == 5 ? State->EnemyHeadquarters : State->FriendlyHeadquarters)->GetActorLocation();
	const FVector EnemyHome = (TeamIndex == 5 ? State->FriendlyHeadquarters : State->EnemyHeadquarters)->GetActorLocation();
	const AMapRegion* Regions[ForceGoals::MaxRegions] = {};
	int32 Controllers[ForceGoals::MaxRegions];
	int32 Hostiles[ForceGoals::MaxRegions] = {};
	int32 DepositValue[ForceGoals::MaxRegions] = {};
	bool Claimed[ForceGoals::MaxRegions] = {};
	for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index)
		Controllers[Index] = -1;
	for (const AMapRegion* Region : State->Regions)
		if (IsValid(Region) && ValidRegion(Region->RegionIndex))
		{
			Regions[Region->RegionIndex] = Region;
			Controllers[Region->RegionIndex] = State->GetRegionController(Region->RegionIndex);
		}
	const AMapRegion* HomeRegion = State->FindRegionAt(Home);
	if (!HomeRegion || !ValidRegion(HomeRegion->RegionIndex))
		return;
	TArray<ACommandBuilding*, TInlineAllocator<8>> Barracks;
	ACommandBuilding* Workshop = nullptr;
	int32 Roles[3] = {};
	for (ACommandBuilding* Building : State->Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive() || Building->OwningPlayerState != Commander)
			continue;
		if (Building->IsProducer())
		{
			Barracks.Add(Building);
			if (Building->bForceConfigured)
				++Roles[Building->ProductionRole == EUnitRole::Frontline ? 0 : Building->ProductionRole == EUnitRole::Ranged ? 1
																															 : 2];
		}
		if (HasCapability(*Building, &UBuildingDefinition::bOffersResearch))
			Workshop = Building;
	}
	Barracks.Sort([](const ACommandBuilding& A, const ACommandBuilding& B) { return A.ForceNumber < B.ForceNumber; });
	if (Barracks.IsEmpty())
	{
		BuildNear(State, ProducerIndex, Home);
		if (TeamIndex == 5)
		{
			State->EnemyPlan = TEXT("ESTABLISH BASE");
			State->EnemyPlanRationale = TEXT("Paid production before economy investment");
			State->ForceNetUpdate();
		}
		return;
	}
	int32 FriendlyStrength = 0, EnemyStrength = 0;
	for (TActorIterator<AArmyUnit> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive())
			continue;
		if (It->GetTeamIndex() == TeamIndex)
			++FriendlyStrength;
		else
		{
			++EnemyStrength;
			const AMapRegion* Region = State->FindRegionAt(It->GetActorLocation());
			if (Region && ValidRegion(Region->RegionIndex))
				++Hostiles[Region->RegionIndex];
		}
	}
	int32 Established = 0;
	struct FDepositCandidate
	{
		ADepositSite* Deposit;
		float Score;
	};
	TArray<FDepositCandidate, TInlineAllocator<16>> EligibleDeposits;
	for (ADepositSite* Deposit : State->Deposits)
	{
		if (!IsValid(Deposit) || !ValidRegion(Deposit->RegionIndex) || Deposit->Remaining <= 0)
			continue;
		if (IsValid(Deposit->Extractor))
		{
			if (Deposit->Extractor->IsAlive() && Deposit->Extractor->IsComplete() && Deposit->Extractor->OwningPlayerState == Commander)
				++Established;
			continue;
		}
		DepositValue[Deposit->RegionIndex] += Deposit->RatePerSecond();
		if (Controllers[Deposit->RegionIndex] != TeamIndex || State->IsRegionContested(Deposit->RegionIndex, TeamIndex))
			continue;
		const float Score = Deposit->RatePerSecond() * 3.f - FVector::Dist2D(Home, Deposit->GetActorLocation()) / 2000.f;
		EligibleDeposits.Add({ Deposit, Score });
	}
	const int32 Reserve = Infantry.UnitCost * Infantry.Capacity;
	const UBuildingDefinition* ExtractorDefinition = Content.Building(ExtractorIndex);
	if (ExtractorDefinition && FriendlyStrength > 0 && Commander->Resources >= ExtractorDefinition->BuildCost + Reserve)
	{
		EligibleDeposits.Sort([](const FDepositCandidate& A, const FDepositCandidate& B) { return A.Score > B.Score; });
		// A free deposit can still be blocked by another building. Try the next legal site,
		// rather than letting an unbuildable preferred deposit deadlock the entire economy.
		for (const FDepositCandidate& Candidate : EligibleDeposits)
			if (BuildNear(State, ExtractorIndex, Candidate.Deposit->GetActorLocation()))
				break;
	}

	ACommandBuilding* Defenders[ForceGoals::MaxRegions] = {};
	bool bThreatened = false;
	for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index)
	{
		if (!Regions[Index] || Controllers[Index] != TeamIndex || !Hostiles[Index])
			continue;
		bThreatened = true;
		float Nearest = TNumericLimits<float>::Max();
		for (ACommandBuilding* Building : Barracks)
		{
			if (!Building->IsComplete() || !IsValid(Building->ForceGroup) || Building->ForceGoal == EForceGoal::FallBack)
				continue;
			bool bAssigned = false;
			for (const ACommandBuilding* Defender : Defenders)
				if (Defender == Building)
				{
					bAssigned = true;
					break;
				}
			if (bAssigned)
				continue;
			const float Distance = FVector::DistSquared2D(Building->ForceGroup->GetCenter(), State->GetRegionAnchor(Index));
			if (Distance < Nearest)
			{
				Nearest = Distance;
				Defenders[Index] = Building;
			}
		}
	}
	const int32 EnemyTeam = TeamIndex == 5 ? 0 : 5;
	int32 EnemyIncome = 0;
	if (EnemyTeam == 5)
		EnemyIncome = State->GetEnemyIncomePerSecond();
	else
		for (APlayerState* Player : State->PlayerArray)
			if (const ACommandPlayerState* Human = Cast<ACommandPlayerState>(Player); Human && Human->TeamIndex == EnemyTeam)
				EnemyIncome += State->GetIncomePerSecond(Human);
	const bool bAdvantage = FriendlyStrength >= Infantry.Capacity && FriendlyStrength * 4 >= FMath::Max(1, EnemyStrength) * 5
		&& State->GetIncomePerSecond(Commander) >= EnemyIncome;
	for (ACommandBuilding* Building : Barracks)
	{
		if (!Building->IsComplete())
			continue;
		const EUnitRole Role = Building->bForceConfigured ? Building->ProductionRole
			: Roles[0] == 0                               ? EUnitRole::Frontline
			: Roles[1] == 0                               ? EUnitRole::Ranged
			: Roles[2] == 0                               ? EUnitRole::Siege
			: Roles[0] <= Roles[1]                        ? EUnitRole::Frontline
														  : EUnitRole::Ranged;
		const int32 UnitIndex = Building->bForceConfigured ? Building->ProductionUnitIndex
			: Role == EUnitRole::Frontline                 ? FrontlineIndex
			: Role == EUnitRole::Ranged                    ? RangedIndex
														   : SiegeIndex;
		const bool bWasConfigured = Building->bForceConfigured;
		if (!Building->bProductionEnabled)
			Building->SetProduction(UnitIndex, true);
		if (!bWasConfigured && Building->bForceConfigured)
			++Roles[Role == EUnitRole::Frontline ? 0 : Role == EUnitRole::Ranged ? 1
																				 : 2];
		if (!IsValid(Building->ForceGroup))
			continue;
		float Health = 0.f;
		int32 Living = 0;
		for (const AArmyUnit* Unit : Building->ForceGroup->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing())
			{
				Health += float(Unit->GetHealth()) / Unit->MaxHealth();
				++Living;
			}
		const bool bRecover = (Living > 0 && Health / Living < .35f)
			|| (Building->ForceGoal == EForceGoal::FallBack && (!Living || Health / Living < .8f));
		EForceGoal Goal = EForceGoal::Hold;
		int32 Target = HomeRegion->RegionIndex;
		if (bRecover)
		{
			Goal = EForceGoal::FallBack;
			Target = INDEX_NONE;
		}
		else
		{
			bool bDefend = false;
			for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index)
				if (Defenders[Index] == Building)
				{
					Target = Index;
					bDefend = true;
					break;
				}
			if (!bDefend)
			{
				const AMapRegion* Source = State->FindRegionAt(Building->ForceGroup->GetCenter());
				if (!Source)
					Source = State->FindRegionAt(Building->GetActorLocation());
				int32 Distance[ForceGoals::MaxRegions];
				Distances(Regions, Source ? Source->RegionIndex : INDEX_NONE, Distance);
				const FVector Center = Building->ForceGroup->GetCenter();
				float Best = -TNumericLimits<float>::Max();
				for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index)
				{
					if (!Regions[Index] || Regions[Index]->RegionRole == ERegionRole::Main || Controllers[Index] == TeamIndex
						|| Distance[Index] == INDEX_NONE || Claimed[Index])
						continue;
					const float Score = 8.f + DepositValue[Index] * 2.f - Distance[Index] * 5.f - Hostiles[Index] * 4.f
						- FVector::DistSquared2D(Center, State->GetRegionAnchor(Index)) / FMath::Square(4000.f)
						- (Controllers[Index] == EnemyTeam ? 3.f : 0.f)
						+ (Building->ForceGoal == EForceGoal::Expand && Building->GoalRegionIndex == Index ? 4.f : 0.f);
					if (Score > Best)
					{
						Best = Score;
						Target = Index;
						Goal = EForceGoal::Expand;
					}
				}
				// Preserve the goal driver's own casualty-refill state; do not reissue identical Assault goals.
				if ((!bThreatened && bAdvantage) || Goal == EForceGoal::Hold)
				{
					Goal = EForceGoal::Assault;
					Target = INDEX_NONE;
				}
				else
					Claimed[Target] = true;
			}
		}
		if (Building->ForceGoal != Goal || (Target != INDEX_NONE && Building->GoalRegionIndex != Target)
			|| !Building->HasConfiguredFront())
			Building->SetGoal(Goal, Target);
	}

	// Prefer the safest controlled forward anchor. Never construct on a contested region.
	FVector BuildCenter = Home;
	float BestForward = -TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index)
	{
		if (!Regions[Index] || Regions[Index]->RegionRole == ERegionRole::Main || Controllers[Index] != TeamIndex
			|| State->IsRegionContested(Index, TeamIndex))
			continue;
		bool bHasProducer = false;
		for (const ACommandBuilding* Building : Barracks)
			if (Regions[Index]->Contains(Building->GetActorLocation()))
			{
				bHasProducer = true;
				break;
			}
		if (bHasProducer)
			continue;
		const FVector Anchor = State->GetRegionAnchor(Index);
		const float Score = -FVector::Dist2D(Anchor, EnemyHome);
		if (Score > BestForward)
		{
			BestForward = Score;
			BuildCenter = Anchor;
		}
	}
	const UBuildingDefinition* WorkshopDefinition = Content.Building(WorkshopIndex);
	if (!bThreatened && Established > 0 && Barracks.Num() < 3 && BestForward > -TNumericLimits<float>::Max()
		&& Commander->Resources >= Content.Building(ProducerIndex)->BuildCost + Reserve)
		BuildNear(State, ProducerIndex, BuildCenter);
	else if (!Workshop && Established > 0 && WorkshopDefinition && Commander->Resources >= WorkshopDefinition->BuildCost + Reserve)
		BuildNear(State, WorkshopIndex, Home);
	else if (Workshop && Workshop->IsComplete() && Commander->Doctrine == EArmyDoctrine::None
		&& Commander->Resources >= ACommandBuilding::ResearchCost + Reserve)
		Workshop->TryResearch(EArmyDoctrine::FieldRepairs);
	if (TeamIndex == 5)
	{
		State->EnemyPlan = bThreatened ? TEXT("DEFEND REGIONS") : bAdvantage ? TEXT("ASSAULT HQ")
																			 : TEXT("EXPAND TERRITORY");
		State->EnemyPlanRationale = TEXT("Region goals, private paid production, forward construction and producer-scoped recovery");
		State->ForceNetUpdate();
	}
}
