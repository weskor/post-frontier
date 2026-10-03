#include "EnemyCommanderTurn.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/OrderGraph.h"
#include "Content/MatchContent.h"
#include "DepositSite.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
using JevExecution::ValidRegion;

bool HasCapability(const ACommandBuilding& Building, bool UBuildingDefinition::* Capability)
{
	const UBuildingDefinition* Definition = Building.GetDefinition();
	return Definition && Definition->*Capability;
}

int32 RoleSlot(EUnitRole Role)
{
	return Role == EUnitRole::Frontline ? 0 : Role == EUnitRole::Ranged ? 1
																		: 2;
}

void AddTarget(FJevTurn& Turn, AActor* Actor)
{
	const AMapRegion* Region = Turn.State->FindRegionAt(Actor->GetActorLocation());
	if (Region && ValidRegion(Region->RegionIndex))
	{
		Turn.Targets.Add({ Actor->GetUniqueID(), Region->RegionIndex, true });
		Turn.TargetActors.Add(Actor);
	}
}

int32 EnemyIncome(const FJevTurn& Turn)
{
	const int32 EnemyTeam = Turn.Team == 5 ? 0 : 5;
	if (EnemyTeam == 5)
		return Turn.State->GetEnemyIncomePerSecond();
	int32 Income = 0;
	for (APlayerState* Player : Turn.State->PlayerArray)
		if (const ACommandPlayerState* Human = Cast<ACommandPlayerState>(Player); Human && Human->TeamIndex == EnemyTeam)
			Income += Turn.State->GetIncomePerSecond(Human);
	return Income;
}
}

namespace JevWorld
{
bool SummariseRegions(FJevTurn& Turn)
{
	ACommandGameState& State = *Turn.State;
	Turn.Summary.Team = Turn.Team;
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region) && ValidRegion(Region->RegionIndex))
		{
			Turn.Regions[Region->RegionIndex] = Region;
			JevPlanner::FRegion& Out = Turn.Summary.Regions[Region->RegionIndex];
			Out.bExists = true;
			Out.bMain = Region->RegionRole == ERegionRole::Main;
			Out.Controller = State.GetRegionController(Region->RegionIndex);
			Out.Position = State.GetRegionAnchor(Region->RegionIndex);
			for (int32 Neighbour : Region->Neighbours)
				if (ValidRegion(Neighbour))
					Out.Neighbours |= uint64(1) << Neighbour;
		}
	Turn.HomeRegion = State.FindRegionAt(Turn.Home);
	if (!Turn.HomeRegion || !ValidRegion(Turn.HomeRegion->RegionIndex))
		return false;
	const AMapRegion* EnemyMain = State.FindRegionAt(Turn.EnemyHome);
	Turn.Summary.EnemyHome = EnemyMain ? EnemyMain->RegionIndex : INDEX_NONE;
	return true;
}

void ScanBuildings(FJevTurn& Turn)
{
	for (ACommandBuilding* Building : Turn.State->Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive() || Building->OwningPlayerState != Turn.Commander)
			continue;
		if (Building->IsProducer())
		{
			Turn.Barracks.Add(Building);
			if (Building->bForceConfigured)
				++Turn.Roles[RoleSlot(Building->ProductionRole)];
		}
		if (HasCapability(*Building, &UBuildingDefinition::bOffersResearch))
			Turn.Workshop = Building;
	}
	Turn.Barracks.Sort([](const ACommandBuilding& A, const ACommandBuilding& B) { return A.ForceNumber < B.ForceNumber; });
}

void ScanForces(FJevTurn& Turn)
{
	for (TActorIterator<AArmyGroup> It(Turn.World); It; ++It)
		if (It->GetOwningPlayerState() == Turn.Commander && It->GetAliveCount() > 0)
			Turn.Forces.Add(*It);
	Turn.Forces.Sort([](const AArmyGroup& A, const AArmyGroup& B) { return A.ForceNumber < B.ForceNumber; });
	for (const AArmyGroup* Force : Turn.Forces)
	{
		const int32 Source = ForceOrderGraph::SourceRegion(*Force, *Turn.State);
		if (ValidRegion(Source))
			Turn.ForceRegions |= uint64(1) << Source;
	}
}

void ScanUnits(FJevTurn& Turn)
{
	for (TActorIterator<AArmyUnit> It(Turn.World); It; ++It)
	{
		if (!It->IsAlive())
			continue;
		if (It->GetTeamIndex() == Turn.Team)
		{
			++Turn.FriendlyStrength;
			continue;
		}
		++Turn.EnemyStrength;
		const AMapRegion* Region = Turn.State->FindRegionAt(It->GetActorLocation());
		if (Region && ValidRegion(Region->RegionIndex))
			++Turn.Summary.Regions[Region->RegionIndex].Hostiles;
		for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
			if ((Turn.ForceRegions & (uint64(1) << Index)) && Turn.State->IsDamagingRegion(**It, Index, Turn.Team))
				Turn.Summary.Regions[Index].bAttacked = true;
	}
}

void ScanDeposits(FJevTurn& Turn)
{
	for (ADepositSite* Deposit : Turn.State->Deposits)
	{
		if (!IsValid(Deposit) || !ValidRegion(Deposit->RegionIndex) || Deposit->Remaining <= 0)
			continue;
		if (IsValid(Deposit->Extractor))
		{
			if (Deposit->Extractor->IsAlive() && Deposit->Extractor->IsComplete()
				&& Deposit->Extractor->OwningPlayerState == Turn.Commander)
				++Turn.Established;
			continue;
		}
		JevPlanner::FRegion& Region = Turn.Summary.Regions[Deposit->RegionIndex];
		Region.DepositValue += Deposit->RatePerSecond();
		if (Region.Controller != Turn.Team || Turn.State->IsRegionContested(Deposit->RegionIndex, Turn.Team))
			continue;
		const float Score = JevExecution::DepositScore(Deposit->RatePerSecond(), FVector::Dist2D(Turn.Home, Deposit->GetActorLocation()));
		Turn.EligibleDeposits.Add({ Deposit, Score });
	}
}

void Finish(FJevTurn& Turn)
{
	ACommandGameState& State = *Turn.State;
	Turn.Summary.bThreatened = JevExecution::IsThreatened(Turn.Summary);
	for (ACommandBuilding* Building : State.Buildings)
		if (IsValid(Building) && Building->IsAlive() && Building->TeamIndex != Turn.Team)
			AddTarget(Turn, Building);
	AddTarget(Turn, Turn.Team == 5 ? State.FriendlyHeadquarters.Get() : State.EnemyHeadquarters.Get());
	Turn.Summary.Targets = Turn.Targets;
	Turn.Summary.bAdvantage = JevExecution::HasAdvantage(Turn.FriendlyStrength, Turn.EnemyStrength,
		Turn.Infantry->Capacity, State.GetIncomePerSecond(Turn.Commander), EnemyIncome(Turn));
}
}
