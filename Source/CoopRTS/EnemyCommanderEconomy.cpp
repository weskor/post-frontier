#include "EnemyCommanderTurn.h"

#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
#include "Content/MatchContent.h"
#include "DepositSite.h"
#include "MapRegion.h"

namespace
{
// Preserve the complete force's rally footprint, not just its empty-force centre.
bool KeepsRalliesClear(const FJevTurn& Turn, float ClearanceSquared, const FVector& Location)
{
	for (const ACommandBuilding* Building : Turn.State->Buildings)
		if (IsValid(Building) && Building->IsAlive() && Building->OwningPlayerState == Turn.Commander
			&& IsValid(Building->ForceGroup)
			&& FVector::DistSquared2D(Location, Building->ForceGroup->GetHomeLocation()) < ClearanceSquared)
			return false;
	return true;
}

bool BlocksDeposit(const FJevTurn& Turn, float ClearanceSquared, const FVector& Location)
{
	for (const ADepositSite* Deposit : Turn.State->Deposits)
		if (IsValid(Deposit) && Deposit->Remaining > 0 && !IsValid(Deposit->Extractor)
			&& FVector::DistSquared2D(Location, Deposit->GetActorLocation()) < ClearanceSquared)
			return true;
	return false;
}

// Rings of candidate sites around Center, mirrored with the side rather than giving
// team 0 a different search bias; the first legal placement wins.
ACommandBuilding* PlaceInRegion(const FJevTurn& Turn, int32 BuildingIndex, const FVector& Center,
	const AMapRegion& Region, float ClearanceSquared)
{
	const float Orientation = Turn.Team == 5 ? -1.f : 1.f;
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			FVector Location = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * ((380.f + Ring * 160.f) * Orientation);
			Location.Z = 5.f;
			Location = Turn.State->ResolveBuildingLocation(BuildingIndex, Location, Turn.Team);
			if (Turn.State->FindRegionAt(Location) != &Region || !KeepsRalliesClear(Turn, ClearanceSquared, Location)
				|| BlocksDeposit(Turn, ClearanceSquared, Location))
				continue;
			if (ACommandBuilding* Building = FCommandService::PlaceBuilding(Turn.Commander, BuildingIndex, Location).Building)
				return Building;
		}
	return nullptr;
}

// Controlled non-main regions that are currently contested.
uint64 ContestedRegions(const FJevTurn& Turn)
{
	uint64 Contested = 0;
	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
	{
		const JevPlanner::FRegion& Region = Turn.Summary.Regions[Index];
		if (Region.bExists && !Region.bMain && Region.Controller == Turn.Team
			&& Turn.State->IsRegionContested(Index, Turn.Team))
			Contested |= uint64(1) << Index;
	}
	return Contested;
}

uint64 ProducerRegions(const FJevTurn& Turn)
{
	uint64 Producers = 0;
	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
	{
		const JevPlanner::FRegion& Region = Turn.Summary.Regions[Index];
		if (!Region.bExists || Region.bMain || Region.Controller != Turn.Team)
			continue;
		for (const ACommandBuilding* Building : Turn.Barracks)
			if (Turn.Regions[Index]->Contains(Building->GetActorLocation()))
			{
				Producers |= uint64(1) << Index;
				break;
			}
	}
	return Producers;
}

JevExecution::FEconomy EconomyInputs(const FJevTurn& Turn, bool bForwardAnchor)
{
	const UBuildingDefinition* WorkshopDefinition = Turn.Content->Building(Turn.WorkshopIndex);
	JevExecution::FEconomy Economy;
	Economy.bThreatened = Turn.Summary.bThreatened;
	Economy.bForwardAnchor = bForwardAnchor;
	Economy.bHasWorkshop = Turn.Workshop != nullptr;
	Economy.bWorkshopComplete = Turn.Workshop && Turn.Workshop->IsComplete();
	Economy.bWorkshopDefined = WorkshopDefinition != nullptr;
	Economy.bDoctrineChosen = Turn.Commander->Doctrine != EArmyDoctrine::None;
	Economy.Established = Turn.Established;
	Economy.Producers = Turn.Barracks.Num();
	Economy.Resources = Turn.Commander->Resources;
	Economy.Reserve = Turn.Reserve;
	Economy.ProducerCost = Turn.Content->Building(Turn.ProducerIndex)->BuildCost;
	Economy.WorkshopCost = WorkshopDefinition ? WorkshopDefinition->BuildCost : 0;
	Economy.ResearchCost = ACommandBuilding::ResearchCost;
	return Economy;
}
}

namespace JevEconomy
{
ACommandBuilding* BuildNear(const FJevTurn& Turn, int32 BuildingIndex, const FVector& Center)
{
	const UBuildingDefinition* Definition = Turn.Content->Building(BuildingIndex);
	if (!Definition || !IsValid(Turn.Commander) || Turn.Commander->Resources < Definition->BuildCost)
		return nullptr;
	// 200 cm covers the six-unit formation extent plus the navigation agent margin.
	const float ClearanceSquared = FMath::Square(Definition->FootprintRadius * UE_SQRT_2 + 200.f);
	if (Definition->bRequiresDeposit)
	{
		if (!KeepsRalliesClear(Turn, ClearanceSquared, Center))
			return nullptr;
		return FCommandService::PlaceBuilding(Turn.Commander, BuildingIndex, Center).Building;
	}
	const AMapRegion* Region = Turn.State->FindRegionAt(Center);
	return Region ? PlaceInRegion(Turn, BuildingIndex, Center, *Region, ClearanceSquared) : nullptr;
}

void PlaceFirstProducer(const FJevTurn& Turn)
{
	if (!Turn.Barracks.IsEmpty())
		return;
	BuildNear(Turn, Turn.ProducerIndex, Turn.Home);
	if (Turn.Team == 5)
		Turn.State->ForceNetUpdate();
}

void BuildExtractor(FJevTurn& Turn)
{
	const UBuildingDefinition* Definition = Turn.Content->Building(Turn.ExtractorIndex);
	if (!Definition || Turn.FriendlyStrength <= 0
		|| !JevExecution::CanAfford(Turn.Commander->Resources, Definition->BuildCost, Turn.Reserve))
		return;
	Turn.EligibleDeposits.Sort([](const FJevTurn::FDepositCandidate& A, const FJevTurn::FDepositCandidate& B) {
		return A.Score > B.Score;
	});
	// A free deposit can still be blocked by another building. Try the next legal site,
	// rather than letting an unbuildable preferred deposit deadlock the entire economy.
	for (const FJevTurn::FDepositCandidate& Candidate : Turn.EligibleDeposits)
		if (BuildNear(Turn, Turn.ExtractorIndex, Candidate.Deposit->GetActorLocation()))
			break;
}

void ConfigureProduction(FJevTurn& Turn)
{
	// The damage type of each role slot's catalogue unit, the answer table the chooser reads.
	EDamageType SlotDamage[JevExecution::RoleSlots];
	for (int32 Slot = 0; Slot < JevExecution::RoleSlots; ++Slot)
	{
		const UArmyUnitDefinition* Unit = Turn.Content->Unit(Turn.Content->UnitIndexForRole(SlotRole(Slot)));
		SlotDamage[Slot] = Unit ? Unit->DamageType : EDamageType::Unset;
	}
	for (ACommandBuilding* Building : Turn.Barracks)
	{
		if (!Building->IsComplete())
			continue;
		const EUnitRole Role = Building->bForceConfigured ? Building->ProductionRole
														  : SlotRole(JevExecution::NextRoleSlot(Turn.Roles, Turn.EnemyArmor, SlotDamage));
		const bool bWasConfigured = Building->bForceConfigured;
		if (!Building->bProductionEnabled)
			FCommandService::ConfigureProduction(Turn.Commander, Building, Role, true);
		if (!bWasConfigured && Building->bForceConfigured)
			++Turn.Roles[RoleSlot(Role)];
	}
}

void BuildNext(const FJevTurn& Turn)
{
	// Prefer the safest controlled forward anchor. Never construct on a contested region.
	const int32 Anchor = JevExecution::ForwardRegion(Turn.Summary, Turn.EnemyHome, ContestedRegions(Turn), ProducerRegions(Turn));
	const FVector BuildCenter = Anchor == INDEX_NONE ? Turn.Home : Turn.Summary.Regions[Anchor].Position;
	// A rush leaves territory behind it, so its extra Barracks build at home when no forward anchor exists.
	switch (JevExecution::NextEconomyAction(EconomyInputs(Turn, Anchor != INDEX_NONE || Turn.bRush)))
	{
	case JevExecution::EEconomyAction::BuildProducer:
		BuildNear(Turn, Turn.ProducerIndex, BuildCenter);
		break;
	case JevExecution::EEconomyAction::BuildWorkshop:
		BuildNear(Turn, Turn.WorkshopIndex, Turn.Home);
		break;
	case JevExecution::EEconomyAction::Research:
		FCommandService::Research(Turn.Commander, Turn.Workshop, EArmyDoctrine::FieldRepairs);
		break;
	case JevExecution::EEconomyAction::None:
		break;
	}
}
}
