#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "MatchSimulationSubsystem.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Content/MatchContent.h"
#include "DepositSite.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "MatchSimulationJson.h"
#include "SimulationSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

using namespace MatchSimulationJson;

void MatchSimulationJson::PlanFields(FJsonObject& Row, const FJevPublishedPlan& Plan, int32 ForceNumber,
	const FString& TargetStructureName, double StartWorldTime, float RemainingCommitment)
{
	Row.SetNumberField(TEXT("ticket"), Plan.TicketNumber);
	Row.SetNumberField(TEXT("force"), ForceNumber);
	Row.SetNumberField(TEXT("verb"), static_cast<uint8>(Plan.Verb));
	Row.SetNumberField(TEXT("source_region"), Plan.SourceRegionIndex);
	Row.SetNumberField(TEXT("target_region"), Plan.TargetRegionIndex);
	if (TargetStructureName.IsEmpty())
		Row.SetField(TEXT("target_structure"), MakeShared<FJsonValueNull>());
	else
		Row.SetStringField(TEXT("target_structure"), TargetStructureName);
	Row.SetNumberField(TEXT("size_band"), Plan.SizeBand);
	Row.SetNumberField(TEXT("eta_seconds"), Plan.EtaSeconds);
	Row.SetNumberField(TEXT("committed_until_seconds"), Plan.CommittedUntil - StartWorldTime);
	Row.SetNumberField(TEXT("remaining_commitment_seconds"), RemainingCommitment);
	Row.SetBoolField(TEXT("escalated"), Plan.bEscalated);
	Row.SetStringField(TEXT("memo"), Plan.Memo);
}

TSharedRef<FJsonObject> FMatchSimulation::Event(const TCHAR* Kind, int32 Team)
{
	const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("kind"), Kind);
	Row->SetNumberField(TEXT("time"), bStarted ? GetWorld()->GetTimeSeconds() - StartWorldTime : 0.);
	Row->SetNumberField(TEXT("team"), Team);
	Append(*Report, TEXT("events"), Row);
	return Row;
}

bool FMatchSimulation::Flush()
{
	const FString& Path = FSimulationSettings::Get().Output;
	if (Path.IsEmpty() || FPaths::IsRelative(Path))
		return false;
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true))
		return false;
	if (DuelRunner && bStarted)
		DuelRunner->GetReport();
	else
		Report->SetNumberField(TEXT("duration"), bStarted ? GetWorld()->GetTimeSeconds() - StartWorldTime : 0.);
	Report->SetNumberField(TEXT("wall_duration"), FPlatformTime::Seconds() - StartWallTime);
	Report->SetNumberField(TEXT("max_game_delta_seconds"), MaxGameDelta);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Report, Writer)
		|| !FFileHelper::SaveStringToFile(Json, *(Path + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		return false;
	return IFileManager::Get().Move(*Path, *(Path + TEXT(".tmp")), true, false, false, true);
}

namespace
{
TArray<TSharedPtr<FJsonValue>> DepositDefinitions(ACommandGameState& State)
{
	TArray<TSharedPtr<FJsonValue>> Deposits;
	for (const ADepositSite* Deposit : State.Deposits)
	{
		if (!IsValid(Deposit))
			continue;
		const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("id"), Deposit->GetName());
		Row->SetNumberField(TEXT("region"), Deposit->RegionIndex);
		Row->SetBoolField(TEXT("rich"), Deposit->bRich);
		Row->SetNumberField(TEXT("initial_amount"), Deposit->Remaining);
		Row->SetNumberField(TEXT("rate"), Deposit->RatePerSecond());
		Row->SetArrayField(TEXT("position"), Position(Deposit->GetActorLocation()));
		Deposits.Add(MakeShared<FJsonValueObject>(Row));
	}
	return Deposits;
}

TArray<TSharedPtr<FJsonValue>> UnitDefinitions(const UMatchContent& Content)
{
	TArray<TSharedPtr<FJsonValue>> Units;
	for (const UArmyUnitDefinition* Definition : Content.Units)
	{
		if (!IsValid(Definition))
			continue;
		const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("id"), Definition->Id.ToString());
		Row->SetNumberField(TEXT("role"), static_cast<uint8>(Definition->Role));
		Row->SetNumberField(TEXT("cost"), Definition->UnitCost);
		Row->SetNumberField(TEXT("capacity"), Definition->Capacity);
		Row->SetNumberField(TEXT("production_seconds"), Definition->UnitDuration);
		Row->SetNumberField(TEXT("health"), Definition->MaxHealth);
		Row->SetNumberField(TEXT("damage"), Definition->AttackDamage);
		Row->SetNumberField(TEXT("attack_interval"), Definition->Interval);
		Row->SetNumberField(TEXT("range"), Definition->Range);
		Units.Add(MakeShared<FJsonValueObject>(Row));
	}
	return Units;
}
}

void FMatchSimulation::DescribeMatch(ACommandGameState& State)
{
	Report->SetStringField(TEXT("planner_class"), TEXT("AEnemyCommander"));
	Report->SetStringField(TEXT("planner_spawn_order"), TEXT("team5 then team0; engine actor tick order is not symmetrized"));
	Report->SetNumberField(TEXT("initial_wallet_team0"), HumanCommander->Resources);
	Report->SetNumberField(TEXT("initial_wallet_team5"), State.EnemyCommander->Resources);
	Report->SetArrayField(TEXT("hq_position_team0"), Position(State.FriendlyHeadquarters->GetActorLocation()));
	Report->SetArrayField(TEXT("hq_position_team5"), Position(State.EnemyHeadquarters->GetActorLocation()));
	PreviousHQHealth[0] = State.FriendlyHeadquarters->Health;
	PreviousHQHealth[1] = State.EnemyHeadquarters->Health;
	DescribeRegions(State);
	Report->SetArrayField(TEXT("deposits"), DepositDefinitions(State));
	Report->SetArrayField(TEXT("unit_definitions"), UnitDefinitions(*State.Content));
}

void FMatchSimulation::DescribeRegions(ACommandGameState& State)
{
	TArray<TSharedPtr<FJsonValue>> Regions;
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region))
			continue;
		const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("index"), Region->RegionIndex);
		Row->SetStringField(TEXT("name"), Region->DisplayName.ToString());
		Row->SetNumberField(TEXT("role"), static_cast<uint8>(Region->RegionRole));
		Row->SetNumberField(TEXT("home_team"), Region->HomeTeam);
		Row->SetArrayField(TEXT("anchor"), Position(State.GetRegionAnchor(Region->RegionIndex)));
		TArray<TSharedPtr<FJsonValue>> Polygon, Neighbours;
		for (const FVector2D& P : Region->Polygon)
			Polygon.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
				MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y) }));
		for (int32 Index : Region->Neighbours)
			Neighbours.Add(MakeShared<FJsonValueNumber>(Index));
		Row->SetArrayField(TEXT("polygon"), MoveTemp(Polygon));
		Row->SetArrayField(TEXT("neighbours"), MoveTemp(Neighbours));
		Regions.Add(MakeShared<FJsonValueObject>(Row));
		RegionOwners.Add(Region->RegionIndex, State.GetRegionController(Region->RegionIndex));
	}
	Report->SetArrayField(TEXT("regions"), MoveTemp(Regions));
}

namespace
{
struct FTeamCounts
{
	int32 Barracks = 0;
	int32 Extractors = 0;
	int32 CompleteExtractors = 0;
	int32 Alive = 0;
	int32 Reinforcing = 0;
	int32 Controlled = 0;
	int32 Roles[3] = {};
	int64 Remaining = 0;
	TMap<int32, int32> UnitRegions;
};

TSharedRef<FJsonObject> BuildingDetail(const ACommandBuilding& Building)
{
	const TSharedRef<FJsonObject> Detail = MakeShared<FJsonObject>();
	Detail->SetStringField(TEXT("id"), Building.GetName());
	Detail->SetNumberField(TEXT("kind"), static_cast<uint8>(Building.Kind));
	Detail->SetNumberField(TEXT("health"), Building.Health);
	Detail->SetNumberField(TEXT("construction_progress"), Building.ConstructionProgress);
	Detail->SetArrayField(TEXT("position"), Position(Building.GetActorLocation()));
	Detail->SetNumberField(TEXT("production_state"), static_cast<uint8>(Building.GetProductionState()));
	Detail->SetNumberField(TEXT("production_progress_seconds"), Building.ProductionProgressSeconds);
	Detail->SetNumberField(TEXT("unit_index"), Building.ProductionUnitIndex);
	const AArmyGroup* Force = Building.ForceGroup;
	Detail->SetNumberField(TEXT("verb"), IsValid(Force) ? static_cast<uint8>(Force->Verb) : -1);
	Detail->SetNumberField(TEXT("status"), IsValid(Force) ? static_cast<uint8>(Force->Status) : -1);
	Detail->SetNumberField(TEXT("target_region"), IsValid(Force) ? Force->TargetRegionIndex : INDEX_NONE);
	Detail->SetNumberField(TEXT("waypoint_region"), IsValid(Force) ? Force->WaypointRegionIndex : INDEX_NONE);
	return Detail;
}

TArray<TSharedPtr<FJsonValue>> CountBuildings(ACommandGameState& State, int32 Team, FTeamCounts& Counts)
{
	TArray<TSharedPtr<FJsonValue>> Buildings;
	for (const ACommandBuilding* Building : State.Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive() || Building->TeamIndex != Team)
			continue;
		if (Building->IsProducer())
			++Counts.Barracks;
		if (Building->Kind == EBuildingKind::Extractor)
		{
			++Counts.Extractors;
			if (Building->IsComplete())
				++Counts.CompleteExtractors;
		}
		Buildings.Add(MakeShared<FJsonValueObject>(BuildingDetail(*Building)));
	}
	return Buildings;
}

TArray<TSharedPtr<FJsonValue>> CountTerritory(ACommandGameState& State, int32 Team, FTeamCounts& Counts)
{
	TArray<TSharedPtr<FJsonValue>> OwnedRegions;
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region) && State.GetRegionController(Region->RegionIndex) == Team)
		{
			++Counts.Controlled;
			OwnedRegions.Add(MakeShared<FJsonValueNumber>(Region->RegionIndex));
		}
	for (const ADepositSite* Deposit : State.Deposits)
		if (IsValid(Deposit) && State.GetRegionController(Deposit->RegionIndex) == Team)
			Counts.Remaining += Deposit->Remaining;
	return OwnedRegions;
}

void CountUnits(UWorld& World, ACommandGameState& State, int32 Team, FTeamCounts& Counts)
{
	for (TActorIterator<AArmyUnit> It(&World); It; ++It)
	{
		if (!It->IsAlive() || It->GetTeamIndex() != Team)
			continue;
		++Counts.Alive;
		if (It->IsReinforcing())
			++Counts.Reinforcing;
		const int32 Role = static_cast<uint8>(It->GetUnitRole());
		if (Role < 3)
			++Counts.Roles[Role];
		const AMapRegion* Region = State.FindRegionAt(It->GetActorLocation());
		++Counts.UnitRegions.FindOrAdd(Region ? Region->RegionIndex : INDEX_NONE);
	}
}

TSharedRef<FJsonObject> ForceDetail(AArmyGroup& Force, int32 Strength, ACommandGameState& State)
{
	const TSharedRef<FJsonObject> Detail = MakeShared<FJsonObject>();
	Detail->SetStringField(TEXT("id"), Force.GetName());
	Detail->SetNumberField(TEXT("number"), Force.ForceNumber);
	Detail->SetNumberField(TEXT("alive"), Strength);
	Detail->SetArrayField(TEXT("center"), Position(Force.GetCenter()));
	const AMapRegion* Region = State.FindRegionAt(Force.GetCenter());
	Detail->SetNumberField(TEXT("region"), Region ? Region->RegionIndex : INDEX_NONE);
	const ACommandBuilding* Producer = Force.GetProductionBuilding();
	Detail->SetBoolField(TEXT("orphan"), !IsValid(Producer) || !Producer->IsAlive());
	Detail->SetNumberField(TEXT("verb"), static_cast<uint8>(Force.Verb));
	Detail->SetNumberField(TEXT("status"), static_cast<uint8>(Force.Status));
	Detail->SetNumberField(TEXT("target_region"), Force.TargetRegionIndex);
	if (IsValid(Force.TargetStructure))
		Detail->SetStringField(TEXT("target_structure"), Force.TargetStructure->GetName());
	else
		Detail->SetField(TEXT("target_structure"), MakeShared<FJsonValueNull>());
	Detail->SetNumberField(TEXT("waypoint_region"), Force.WaypointRegionIndex);
	Detail->SetNumberField(TEXT("resume_count"), Force.ResumeCount);
	return Detail;
}

TArray<TSharedPtr<FJsonValue>> ForceRows(UWorld& World, ACommandGameState& State, int32 Team)
{
	TArray<TSharedPtr<FJsonValue>> Forces;
	for (TActorIterator<AArmyGroup> It(&World); It; ++It)
	{
		if (It->GetTeamIndex() != Team)
			continue;
		int32 Strength = 0;
		for (const AArmyUnit* Unit : It->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive())
				++Strength;
		if (Strength)
			Forces.Add(MakeShared<FJsonValueObject>(ForceDetail(**It, Strength, State)));
	}
	return Forces;
}

TArray<TSharedPtr<FJsonValue>> SnapshotDeposits(ACommandGameState& State)
{
	TArray<TSharedPtr<FJsonValue>> Deposits;
	for (const ADepositSite* Deposit : State.Deposits)
	{
		if (!IsValid(Deposit))
			continue;
		const TSharedRef<FJsonObject> Detail = MakeShared<FJsonObject>();
		Detail->SetStringField(TEXT("id"), Deposit->GetName());
		Detail->SetNumberField(TEXT("region"), Deposit->RegionIndex);
		Detail->SetNumberField(TEXT("remaining"), Deposit->Remaining);
		Detail->SetNumberField(TEXT("controller"), State.GetRegionController(Deposit->RegionIndex));
		Detail->SetNumberField(TEXT("extractor_team"), IsValid(Deposit->Extractor) ? Deposit->Extractor->TeamIndex : -1);
		Deposits.Add(MakeShared<FJsonValueObject>(Detail));
	}
	return Deposits;
}

TArray<TSharedPtr<FJsonValue>> SnapshotPlans(ACommandGameState& State, double StartWorldTime, float WorldTime)
{
	TArray<TSharedPtr<FJsonValue>> Plans;
	Plans.Reserve(State.EnemyPlans.Num());
	for (const FJevPublishedPlan& Plan : State.EnemyPlans)
	{
		const TSharedRef<FJsonObject> Detail = MakeShared<FJsonObject>();
		PlanFields(*Detail, Plan, Plan.ForceNumber,
			IsValid(Plan.TargetStructure) ? Plan.TargetStructure->GetName() : FString(), StartWorldTime,
			FMath::Max(0.f, Plan.CommittedUntil - WorldTime));
		Plans.Add(MakeShared<FJsonValueObject>(Detail));
	}
	return Plans;
}
}

TSharedRef<FJsonObject> FMatchSimulation::SnapshotTeam(ACommandGameState& State, int32 Slot) const
{
	const int32 Team = Slot == 0 ? 0 : 5;
	const ACommandPlayerState* Commander = Slot == 0 ? HumanCommander.Get() : State.EnemyCommander.Get();
	const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetNumberField(TEXT("team"), Team);
	Summary->SetNumberField(TEXT("wallet"), Commander->Resources);
	Summary->SetNumberField(TEXT("income_per_second"), State.GetIncomePerSecond(Commander));
	Summary->SetNumberField(TEXT("hq_health"), Slot == 0 ? State.FriendlyHeadquarters->Health : State.EnemyHeadquarters->Health);
	Summary->SetNumberField(TEXT("units_produced"), Produced[Slot]);
	Summary->SetNumberField(TEXT("casualties_observed"), Casualties[Slot]);
	Summary->SetNumberField(TEXT("unit_health_loss_observed"), static_cast<double>(ObservedHealthLoss[Slot]));
	Summary->SetNumberField(TEXT("attacks_observed"), static_cast<double>(Attacks[Slot]));
	FTeamCounts Counts;
	TArray<TSharedPtr<FJsonValue>> Buildings = CountBuildings(State, Team, Counts);
	TArray<TSharedPtr<FJsonValue>> OwnedRegions = CountTerritory(State, Team, Counts);
	CountUnits(*GetWorld(), State, Team, Counts);
	TArray<TSharedPtr<FJsonValue>> Forces = ForceRows(*GetWorld(), State, Team);
	const TSharedRef<FJsonObject> Concentration = MakeShared<FJsonObject>();
	int32 Largest = 0;
	for (const auto& Pair : Counts.UnitRegions)
	{
		Concentration->SetNumberField(FString::FromInt(Pair.Key), Pair.Value);
		if (Pair.Key != INDEX_NONE)
			Largest = FMath::Max(Largest, Pair.Value);
	}
	Summary->SetNumberField(TEXT("barracks"), Counts.Barracks);
	Summary->SetNumberField(TEXT("extractors"), Counts.Extractors);
	Summary->SetNumberField(TEXT("completed_extractors"), Counts.CompleteExtractors);
	Summary->SetNumberField(TEXT("deposits_remaining"), static_cast<double>(Counts.Remaining));
	Summary->SetNumberField(TEXT("units_alive"), Counts.Alive);
	Summary->SetNumberField(TEXT("units_reinforcing"), Counts.Reinforcing);
	Summary->SetNumberField(TEXT("regions_controlled"), Counts.Controlled);
	Summary->SetArrayField(TEXT("region_indices"), MoveTemp(OwnedRegions));
	Summary->SetArrayField(TEXT("units_by_role"), { MakeShared<FJsonValueNumber>(Counts.Roles[0]), MakeShared<FJsonValueNumber>(Counts.Roles[1]), MakeShared<FJsonValueNumber>(Counts.Roles[2]) });
	Summary->SetObjectField(TEXT("units_by_region"), Concentration);
	Summary->SetNumberField(TEXT("largest_region_unit_share"), Counts.Alive ? static_cast<double>(Largest) / Counts.Alive : 0.);
	Summary->SetArrayField(TEXT("buildings"), MoveTemp(Buildings));
	Summary->SetArrayField(TEXT("forces"), MoveTemp(Forces));
	return Summary;
}

void FMatchSimulation::Snapshot(ACommandGameState& State, double ScheduledTime)
{
	const float WorldTime = GetWorld()->GetTimeSeconds();
	const double Time = WorldTime - StartWorldTime;
	const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
	Row->SetNumberField(TEXT("time"), Time);
	Row->SetNumberField(TEXT("scheduled_time"), ScheduledTime);
	Row->SetNumberField(TEXT("effective_dilation"), GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation());
	TArray<TSharedPtr<FJsonValue>> Teams;
	for (int32 Slot = 0; Slot < 2; ++Slot)
		Teams.Add(MakeShared<FJsonValueObject>(SnapshotTeam(State, Slot)));
	Row->SetArrayField(TEXT("teams"), MoveTemp(Teams));
	Row->SetArrayField(TEXT("deposits"), SnapshotDeposits(State));
	Row->SetArrayField(TEXT("enemy_plans"), SnapshotPlans(State, StartWorldTime, WorldTime));
	Append(*Report, TEXT("snapshots"), Row);
}
#endif
