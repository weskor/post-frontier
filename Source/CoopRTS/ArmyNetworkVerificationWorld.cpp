// Probe snapshot of replicated armies, buildings, capture sites, regions and deposits.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "CommandPlayerController.h"
#include "Content/MatchContent.h"
#include "DepositSite.h"
#include "EngineUtils.h"
#include "ForceOrders.h"
#include "HUD/ForceBarVerification.h"
#include "Headquarters.h"
#include "Json.h"
#include "MapRegion.h"
#include "RouteIntentVerification.h"

namespace CoopRTSNetworkVerification::Probe
{
namespace
{
void ArmyOrdersJson(const AArmyGroup& Group, const TSharedPtr<FJsonObject>& Entry)
{
	TArray<TSharedPtr<FJsonValue>> Orders;
	for (const FForceOrder& Order : Group.Orders)
	{
		auto OrderEntry = Object();
		Number(OrderEntry, TEXT("forceVerb"), static_cast<int32>(Order.Verb));
		Number(OrderEntry, TEXT("targetRegionIndex"), Order.RegionIndex);
		Number(OrderEntry, TEXT("targetStructureId"), IsValid(Order.Structure) ? LifetimeId(Order.Structure) : -1);
		Orders.Add(MakeShared<FJsonValueObject>(OrderEntry));
	}
	Entry->SetArrayField(TEXT("orders"), Orders);
}

void ArmyHoldJson(const AArmyGroup& Group, const TSharedPtr<FJsonObject>& Entry)
{
	Number(Entry, TEXT("holdRegionIndex"), Group.HoldRegionIndex);
	Number(Entry, TEXT("holdPostIndex"), Group.HoldPostIndex);
	Vector(Entry, TEXT("holdPostLocation"), Group.HoldPostLocation);
	Entry->SetBoolField(TEXT("bHoldResponding"), Group.bHoldResponding);
	const AArmyUnit* Threat = Group.HoldThreat;
	const bool bLiveThreat = IsValid(Threat) && Threat->IsAlive();
	Number(Entry, TEXT("holdThreatId"), bLiveThreat ? LifetimeId(Threat) : -1);
	if (bLiveThreat)
		Vector(Entry, TEXT("holdThreatPosition"), Threat->GetActorLocation());
}

void ArmyHudJson(UWorld* World, const AArmyGroup& Group, const TSharedPtr<FJsonObject>& Entry)
{
	ACommandPlayerController* Local = Cast<ACommandPlayerController>(World->GetFirstPlayerController());
	if (!Local)
		return;
	const ACommandHUD* HUD = Cast<ACommandHUD>(Local->GetHUD());
	if (!HUD)
		return;
	FVector2D Badge;
	if (HUD->FindForceScreenPosition(&Group, Badge))
		Entry->SetArrayField(TEXT("badge"), { MakeShared<FJsonValueNumber>(Badge.X), MakeShared<FJsonValueNumber>(Badge.Y) });
	Entry->SetBoolField(TEXT("highlighted"), Local->IsForceHighlighted(&Group));
	ForceBarVerification::Snapshot(*Local, Group, Entry);
}

void ArmyUnitsJson(const ACommandGameState& State, const AArmyGroup& Group, const TSharedPtr<FJsonObject>& Entry)
{
	auto Units = TArray<TSharedPtr<FJsonValue>>();
	for (const AArmyUnit* Unit : Group.GetUnits())
	{
		if (!IsValid(Unit))
			continue;
		auto Member = Object();
		Number(Member, TEXT("actorId"), LifetimeId(Unit));
		Number(Member, TEXT("maxHealth"), Unit->MaxHealth());
		Number(Member, TEXT("slot"), Unit->GetCompositionSlot());
		Number(Member, TEXT("role"), static_cast<int32>(Unit->GetUnitRole()));
		Number(Member, TEXT("health"), Unit->GetHealth());
		Number(Member, TEXT("attacks"), Unit->AttackCount);
		Number(Member, TEXT("owner"), Unit->GetCommanderIndex());
		Number(Member, TEXT("producer"), IsValid(Group.GetProductionBuilding()) ? State.Buildings.IndexOfByKey(Group.GetProductionBuilding()) : -1);
		Vector(Member, TEXT("position"), Unit->GetActorLocation());
		Units.Add(MakeShared<FJsonValueObject>(Member));
	}
	Entry->SetArrayField(TEXT("units"), Units);
}

TSharedPtr<FJsonValue> ArmyJson(UWorld* World, const ACommandGameState& State, const AArmyGroup& Group)
{
	auto Entry = Object();
	Number(Entry, TEXT("actorId"), LifetimeId(&Group));
	Number(Entry, TEXT("owner"), IsValid(Group.GetOwningPlayerState()) ? Group.GetOwningPlayerState()->CommanderIndex : -1);
	Number(Entry, TEXT("team"), Group.GetTeamIndex());
	Number(Entry, TEXT("army"), Group.GetArmyIndex());
	Number(Entry, TEXT("forceNumber"), Group.ForceNumber);
	Number(Entry, TEXT("serial"), Group.OrderSerial);
	Number(Entry, TEXT("doctrine"), static_cast<int32>(Group.GetDoctrine()));
	Number(Entry, TEXT("forceVerb"), static_cast<int32>(Group.Verb));
	Number(Entry, TEXT("targetRegionIndex"), Group.TargetRegionIndex);
	Number(Entry, TEXT("targetStructureId"), IsValid(Group.TargetStructure) ? LifetimeId(Group.TargetStructure) : -1);
	Number(Entry, TEXT("status"), static_cast<int32>(Group.Status));
	Number(Entry, TEXT("retreatThreshold"), static_cast<int32>(Group.RetreatThreshold));
	Number(Entry, TEXT("waypointRegionIndex"), Group.WaypointRegionIndex);
	Number(Entry, TEXT("resumeCount"), Group.ResumeCount);
	Number(Entry, TEXT("marchSpeed"), Group.GetMarchSpeed());
	ArmyOrdersJson(Group, Entry);
	RouteIntentVerification::Snapshot(Group, Entry);
	Number(Entry, TEXT("producer"), IsValid(Group.GetProductionBuilding()) ? State.Buildings.IndexOfByKey(Group.GetProductionBuilding()) : -1);
	Vector(Entry, TEXT("front"), Group.Destination);
	Vector(Entry, TEXT("center"), Group.GetCenter());
	Vector(Entry, TEXT("destination"), Group.Destination);
	Vector(Entry, TEXT("home"), Group.GetHomeLocation());
	ArmyHoldJson(Group, Entry);
	ArmyHudJson(World, Group, Entry);
	ArmyUnitsJson(State, Group, Entry);
	return MakeShared<FJsonValueObject>(Entry);
}

TSharedPtr<FJsonValue> BuildingJson(const ACommandGameState& State, int32 Index, const ACommandBuilding& Building)
{
	auto Entry = Object();
	Number(Entry, TEXT("index"), Index);
	Number(Entry, TEXT("actorId"), LifetimeId(&Building));
	Number(Entry, TEXT("owner"), IsValid(Building.OwningPlayerState) ? Building.OwningPlayerState->CommanderIndex : -1);
	Number(Entry, TEXT("team"), Building.TeamIndex);
	Number(Entry, TEXT("kind"), static_cast<int32>(Building.Kind));
	Number(Entry, TEXT("health"), Building.Health);
	Number(Entry, TEXT("deposit"), State.Deposits.IndexOfByKey(Building.Deposit));
	int32 Joined, Travelling;
	Building.GetForceCounts(Joined, Travelling);
	Entry->SetBoolField(TEXT("configured"), Building.bForceConfigured);
	Number(Entry, TEXT("forceID"), IsValid(Building.ForceGroup) ? Building.ForceGroup->GetArmyIndex() : -1);
	Number(Entry, TEXT("forceNumber"), Building.ForceNumber);
	const UArmyUnitDefinition* Unit = ProductionDefinition(State, Building);
	Number(Entry, TEXT("capacity"), Unit ? Unit->Capacity : 0);
	Number(Entry, TEXT("joined"), Joined);
	Number(Entry, TEXT("travelling"), Travelling);
	Number(Entry, TEXT("unitCost"), Unit ? Unit->UnitCost : 0);
	Number(Entry, TEXT("unitTime"), Unit ? Unit->UnitDuration : 0.f);
	Number(Entry, TEXT("constructionProgress"), Building.ConstructionProgress);
	Number(Entry, TEXT("recipe"), static_cast<int32>(Building.ProductionRole));
	Number(Entry, TEXT("productionSeconds"), Building.ProductionProgressSeconds);
	const AArmyGroup* Force = Building.ForceGroup;
	Number(Entry, TEXT("forceVerb"), IsValid(Force) ? static_cast<int32>(Force->Verb) : -1);
	Number(Entry, TEXT("targetRegionIndex"), IsValid(Force) ? Force->TargetRegionIndex : INDEX_NONE);
	Number(Entry, TEXT("targetStructureId"), IsValid(Force) && IsValid(Force->TargetStructure) ? LifetimeId(Force->TargetStructure) : -1);
	Number(Entry, TEXT("status"), IsValid(Force) ? static_cast<int32>(Force->Status) : -1);
	Number(Entry, TEXT("rallyRegionIndex"), Building.RallyRegionIndex);
	Entry->SetBoolField(TEXT("enabled"), Building.bProductionEnabled);
	Entry->SetStringField(TEXT("productionState"),
		StaticEnum<EProductionState>()->GetNameStringByValue(static_cast<int64>(Building.GetProductionState())));
	Vector(Entry, TEXT("position"), Building.GetActorLocation());
	Vector(Entry, TEXT("front"), IsValid(Force) ? Force->Destination : Building.GetActorLocation());
	return MakeShared<FJsonValueObject>(Entry);
}

void BuildingsSnapshot(const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	auto Buildings = TArray<TSharedPtr<FJsonValue>>();
	for (int32 Index = 0; Index < State.Buildings.Num(); ++Index)
	{
		const ACommandBuilding* Building = State.Buildings[Index];
		if (IsValid(Building))
			Buildings.Add(BuildingJson(State, Index, *Building));
	}
	Result->SetArrayField(TEXT("buildings"), Buildings);
	if (IsValid(State.EnemyHeadquarters))
	{
		Number(Result, TEXT("enemyHQId"), LifetimeId(State.EnemyHeadquarters));
		Vector(Result, TEXT("enemyHQPosition"), State.EnemyHeadquarters->GetActorLocation());
	}
}

void SitesSnapshot(const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	auto Sites = TArray<TSharedPtr<FJsonValue>>();
	for (const ACapturePoint* Site : State.CaptureSites)
	{
		if (!IsValid(Site))
			continue;
		auto Entry = Object();
		Number(Entry, TEXT("actorId"), LifetimeId(Site));
		Number(Entry, TEXT("index"), Site->SiteIndex);
		Number(Entry, TEXT("kind"), static_cast<int32>(Site->SiteKind));
		Number(Entry, TEXT("owner"), Site->ControllingTeam);
		Number(Entry, TEXT("progress"), Site->CaptureProgress);
		Vector(Entry, TEXT("position"), Site->GetActorLocation());
		Entry->SetBoolField(TEXT("friendlyPresent"), Site->bFriendlyPresent);
		Entry->SetBoolField(TEXT("enemyPresent"), Site->bEnemyPresent);
		Sites.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("sites"), Sites);
}

TSharedPtr<FJsonValue> RegionJson(const ACommandGameState& State, const AMapRegion& Region)
{
	auto Entry = Object();
	Number(Entry, TEXT("index"), Region.RegionIndex);
	Entry->SetStringField(TEXT("name"), Region.DisplayName.ToString());
	Number(Entry, TEXT("role"), static_cast<int32>(Region.RegionRole));
	Number(Entry, TEXT("homeTeam"), Region.HomeTeam);
	Number(Entry, TEXT("controller"), State.GetRegionController(Region.RegionIndex));
	Entry->SetBoolField(TEXT("contested"), State.IsRegionContested(Region.RegionIndex, 0));
	Entry->SetBoolField(TEXT("enemyContested"), State.IsRegionContested(Region.RegionIndex, 5));
	Vector(Entry, TEXT("anchor"), State.GetRegionAnchor(Region.RegionIndex));
	TArray<TSharedPtr<FJsonValue>> Neighbours;
	for (int32 Neighbour : Region.Neighbours)
		Neighbours.Add(MakeShared<FJsonValueNumber>(Neighbour));
	Entry->SetArrayField(TEXT("neighbours"), Neighbours);
	TArray<TSharedPtr<FJsonValue>> Polygon;
	for (const FVector2D& Point : Region.Polygon)
	{
		TArray<TSharedPtr<FJsonValue>> Coordinates;
		Coordinates.Add(MakeShared<FJsonValueNumber>(Point.X));
		Coordinates.Add(MakeShared<FJsonValueNumber>(Point.Y));
		Polygon.Add(MakeShared<FJsonValueArray>(Coordinates));
	}
	Entry->SetArrayField(TEXT("polygon"), Polygon);
	return MakeShared<FJsonValueObject>(Entry);
}

void RegionsSnapshot(const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	auto Regions = TArray<TSharedPtr<FJsonValue>>();
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region))
			Regions.Add(RegionJson(State, *Region));
	Result->SetArrayField(TEXT("regions"), Regions);
}

TSharedPtr<FJsonValue> DepositJson(const ACommandGameState& State, int32 Index, const ADepositSite& Deposit)
{
	auto Entry = Object();
	Number(Entry, TEXT("index"), Index);
	Number(Entry, TEXT("actorId"), LifetimeId(&Deposit));
	Number(Entry, TEXT("region"), Deposit.RegionIndex);
	Number(Entry, TEXT("remaining"), Deposit.Remaining);
	Number(Entry, TEXT("rate"), Deposit.RatePerSecond());
	Entry->SetBoolField(TEXT("rich"), Deposit.bRich);
	const ACommandBuilding* Extractor = Deposit.Extractor;
	Entry->SetBoolField(TEXT("occupied"), IsValid(Extractor));
	Entry->SetBoolField(TEXT("complete"), IsValid(Extractor) && Extractor->IsComplete() && Extractor->IsAlive());
	Number(Entry, TEXT("extractor"), IsValid(Extractor) ? State.Buildings.IndexOfByKey(Deposit.Extractor) : -1);
	Number(Entry, TEXT("owner"), IsValid(Extractor) && IsValid(Extractor->OwningPlayerState) ? Extractor->OwningPlayerState->CommanderIndex : -1);
	Number(Entry, TEXT("team"), IsValid(Extractor) ? Extractor->TeamIndex : -1);
	Vector(Entry, TEXT("position"), Deposit.GetActorLocation());
	return MakeShared<FJsonValueObject>(Entry);
}

void DepositsSnapshot(const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	auto Deposits = TArray<TSharedPtr<FJsonValue>>();
	for (int32 Index = 0; Index < State.Deposits.Num(); ++Index)
	{
		const ADepositSite* Deposit = State.Deposits[Index];
		if (IsValid(Deposit))
			Deposits.Add(DepositJson(State, Index, *Deposit));
	}
	Result->SetArrayField(TEXT("deposits"), Deposits);
}
}

void ArmiesSnapshot(UWorld* World, const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	auto Armies = TArray<TSharedPtr<FJsonValue>>();
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		Armies.Add(ArmyJson(World, State, **It));
	Result->SetArrayField(TEXT("armies"), Armies);
}

void StructuresSnapshot(const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	BuildingsSnapshot(State, Result);
	SitesSnapshot(State, Result);
	RegionsSnapshot(State, Result);
	DepositsSnapshot(State, Result);
	Number(Result, TEXT("friendlyHQ"), IsValid(State.FriendlyHeadquarters) ? State.FriendlyHeadquarters->Health : -1);
	Number(Result, TEXT("enemyHQ"), IsValid(State.EnemyHeadquarters) ? State.EnemyHeadquarters->Health : -1);
	if (IsValid(State.FriendlyHeadquarters))
		Vector(Result, TEXT("friendlyHQPosition"), State.FriendlyHeadquarters->GetActorLocation());
	if (IsValid(State.EnemyHeadquarters))
		Vector(Result, TEXT("enemyHQPosition"), State.EnemyHeadquarters->GetActorLocation());
}
}
#endif
