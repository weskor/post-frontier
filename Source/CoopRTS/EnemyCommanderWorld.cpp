#include "EnemyCommanderTurn.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/OrderGraph.h"
#include "GameState/GameStateTerritory.h"
#include "Content/MatchContent.h"
#include "DepositSite.h"
#include "FailoverNode.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "Rules/FortifyPolicy.h"
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

void AddTarget(FJevTurn& Turn, AActor* Actor, bool bNode = false)
{
	const AMapRegion* Region = Turn.State->FindRegionAt(Actor->GetActorLocation());
	if (Region && ValidRegion(Region->RegionIndex))
	{
		Turn.Targets.Add({ Actor->GetUniqueID(), Region->RegionIndex, true, bNode });
		Turn.TargetActors.Add(Actor);
	}
}

// Guarded HQs: the hostile nodes are targets and gate the HQ; the team's own nodes mark regions worth defending.
void ScanGuards(FJevTurn& Turn)
{
	const ACommandGameState& State = *Turn.State;
	AHeadquarters* Hostile = Turn.Team == 5 ? State.FriendlyHeadquarters.Get() : State.EnemyHeadquarters.Get();
	const AHeadquarters* Own = Turn.Team == 5 ? State.EnemyHeadquarters.Get() : State.FriendlyHeadquarters.Get();
	if (IsValid(Own))
		for (const TWeakObjectPtr<AFailoverNode>& Node : Own->GetNodes())
			if (const AMapRegion* Region = Node.IsValid() && Node->IsAlive() ? State.FindRegionAt(Node->GetActorLocation()) : nullptr;
				Region && ValidRegion(Region->RegionIndex))
				++Turn.Summary.Regions[Region->RegionIndex].OwnNodes;
	if (!IsValid(Hostile))
		return;
	for (const TWeakObjectPtr<AFailoverNode>& Node : Hostile->GetNodes())
		if (Node.IsValid() && Node->IsAlive())
		{
			Turn.Summary.bHostileNodesStand = true;
			AddTarget(Turn, Node.Get(), true);
		}
	Turn.Summary.bHostileHqOffline = Hostile->IsOffline();
	// An immune or offline HQ cannot be damaged, so it is no structure target.
	if (Hostile->IsOnline() && !Hostile->IsImmune())
		AddTarget(Turn, Hostile);
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

// A built Drill Rig. JEV counts its own only where its main reaches the region; every rig
// still tells the planner what income it would restore or what a raid would hit.
void ScanExtractor(FJevTurn& Turn, const ADepositSite& Deposit)
{
	const ACommandBuilding& Extractor = *Deposit.Extractor;
	if (!Extractor.IsAlive() || !Extractor.IsComplete())
		return;
	JevPlanner::FRegion& Region = Turn.Summary.Regions[Deposit.RegionIndex];
	if (Extractor.OwningPlayerState != Turn.Commander)
	{
		++Region.HostileRigs;
		return;
	}
	Region.IncomeValue += Deposit.RatePerSecond();
	if (Turn.Connected & (uint64(1) << Deposit.RegionIndex))
		++Turn.Established;
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
			Out.DefenceMultiplier = FortifyPolicy::DefenceFor(Region->GetFortify(), Turn.Team, State.GetServerWorldTimeSeconds());
			Out.Position = State.GetRegionAnchor(Region->RegionIndex);
			for (int32 Neighbour : Region->Neighbours)
				if (ValidRegion(Neighbour))
					Out.Neighbours |= uint64(1) << Neighbour;
		}
	Turn.HomeRegion = State.FindRegionAt(Turn.Home);
	if (!Turn.HomeRegion || !ValidRegion(Turn.HomeRegion->RegionIndex))
		return false;
	Turn.Summary.Home = Turn.HomeRegion->RegionIndex;
	// The published mask is the one connectivity rule; refresh it so a capture since the last state tick is seen.
	GameStateTerritory::RefreshConnections(State);
	Turn.Connected = State.GetConnectedMask(Turn.Team);
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
		if (const int32 Armor = static_cast<int32>(It->GetArmorClass()); Armor >= 0 && Armor < 4)
			++Turn.EnemyArmor.Count[Armor];
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
			ScanExtractor(Turn, *Deposit);
			continue;
		}
		JevPlanner::FRegion& Region = Turn.Summary.Regions[Deposit->RegionIndex];
		Region.DepositValue += Deposit->RatePerSecond();
		if (Region.Controller != Turn.Team || !(Turn.Connected & (uint64(1) << Deposit->RegionIndex))
			|| Turn.State->IsRegionContested(Deposit->RegionIndex, Turn.Team))
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
	ScanGuards(Turn);
	Turn.Summary.Targets = Turn.Targets;
	Turn.Summary.bAdvantage = JevExecution::HasAdvantage(Turn.FriendlyStrength, Turn.EnemyStrength,
		Turn.Infantry->Capacity, State.GetIncomePerSecond(Turn.Commander), EnemyIncome(Turn));
}
}
