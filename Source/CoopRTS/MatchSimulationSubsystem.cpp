#include "MatchSimulationSubsystem.h"

#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Content/MatchContent.h"
#include "DepositSite.h"
#include "EnemyCommander.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "SimulationSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	int32 TeamSlot(int32 Team) { return Team == 0 ? 0 : Team == 5 ? 1 : INDEX_NONE; }

	TArray<TSharedPtr<FJsonValue>> Position(const FVector& P)
	{
		return { MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y), MakeShared<FJsonValueNumber>(P.Z) };
	}

	void Append(FJsonObject& Object, const TCHAR* Field, const TSharedRef<FJsonObject>& Item)
	{
		TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		Object.GetField<EJson::Array>(Field)->TryGetArray(Array);
		check(Array);
		Array->Add(MakeShared<FJsonValueObject>(Item));
	}
}

bool UMatchSimulationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->WorldType == EWorldType::Game
		&& FSimulationSettings::Get().bEnabled;
}

void UMatchSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const FSimulationSettings& Settings = FSimulationSettings::Get();
	FMath::RandInit(Settings.Seed);
	FMath::SRandInit(Settings.Seed);
	StartWallTime = FPlatformTime::Seconds();
	Report = MakeShared<FJsonObject>();
	Report->SetNumberField(TEXT("schema_version"), 1);
	Report->SetStringField(TEXT("status"), TEXT("initializing"));
	Report->SetStringField(TEXT("outcome"), TEXT("none"));
	Report->SetField(TEXT("winner"), MakeShared<FJsonValueNull>());
	Report->SetNumberField(TEXT("seed"), Settings.Seed);
	Report->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Report->SetStringField(TEXT("command_line"), FCommandLine::Get());
	Report->SetStringField(TEXT("map"), UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()));
	Report->SetNumberField(TEXT("time_cap_seconds"), Settings.TimeCap);
	Report->SetNumberField(TEXT("requested_dilation"), Settings.Dilation);
	Report->SetNumberField(TEXT("snapshot_interval_seconds"), 30);
	Report->SetArrayField(TEXT("snapshots"), TArray<TSharedPtr<FJsonValue>>{});
	Report->SetArrayField(TEXT("events"), TArray<TSharedPtr<FJsonValue>>{});
	const TSharedRef<FJsonObject> Economy = MakeShared<FJsonObject>();
	Economy->SetNumberField(TEXT("baseline"), Settings.BaselineIncome);
	Economy->SetNumberField(TEXT("normal_rate"), Settings.NormalRate);
	Economy->SetNumberField(TEXT("rich_rate"), Settings.RichRate);
	Economy->SetNumberField(TEXT("normal_amount"), Settings.NormalAmount);
	Economy->SetNumberField(TEXT("rich_amount"), Settings.RichAmount);
	Report->SetObjectField(TEXT("economy"), Economy);
}

TStatId UMatchSimulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMatchSimulationSubsystem, STATGROUP_Tickables);
}

bool UMatchSimulationSubsystem::Start(ACommandGameState& State)
{
	if (GetWorld()->GetNetMode() != NM_Standalone)
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Autopilot simulation requires standalone, not a network world"));
		return false;
	}
	if (!FSimulationSettings::Get().Error.IsEmpty())
	{
		Finish(TEXT("failed"), TEXT("none"), -1, FSimulationSettings::Get().Error);
		return false;
	}
	if (!IsValid(State.Content) || !IsValid(State.FriendlyHeadquarters) || !IsValid(State.EnemyHeadquarters)
		|| !IsValid(State.Arena) || State.Regions.IsEmpty() || State.Deposits.IsEmpty() || !IsValid(State.EnemyCommander))
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Map lacks initialized match content, arena, HQs, regions, deposits or enemy wallet"));
		return false;
	}
	for (APlayerState* Player : State.PlayerArray)
		if (ACommandPlayerState* Commander = Cast<ACommandPlayerState>(Player))
			if (Commander->TeamIndex == 0 && Commander->CommanderIndex >= 0)
			{
				if (HumanCommander.IsValid())
				{
					Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Simulation requires exactly one team-0 commander"));
					return false;
				}
				HumanCommander = Commander;
			}
	if (!HumanCommander.IsValid())
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Standalone local commander was not initialized"));
		return false;
	}
	int32 EnemyPlanners = 0;
	for (TActorIterator<AEnemyCommander> It(GetWorld()); It; ++It)
	{
		if (It->TeamIndex == 5) ++EnemyPlanners;
		else
		{
			Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Map already contains an autopilot planner"));
			return false;
		}
	}
	if (EnemyPlanners != 1)
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Simulation requires exactly one normal enemy planner"));
		return false;
	}
	const FTransform Transform = FTransform::Identity;
	AEnemyCommander* Planner = GetWorld()->SpawnActorDeferred<AEnemyCommander>(AEnemyCommander::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Planner)
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Could not spawn team-0 planner"));
		return false;
	}
	Planner->TeamIndex = 0;
	Planner->Commander = HumanCommander.Get();
	Planner->FinishSpawning(Transform);
	Autopilot = Planner;
	StartWorldTime = GetWorld()->GetTimeSeconds();
	bStarted = true;
	AWorldSettings* WorldSettings = GetWorld()->GetWorldSettings();
	WorldSettings->SetAllowTimeDilation(true);
	WorldSettings->SetTimeDilation(FSimulationSettings::Get().Dilation);
	const float EffectiveDilation = WorldSettings->GetEffectiveTimeDilation();
	// Combat and production intentionally do at most one action per actor tick.
	// Increase virtual tick frequency with dilation instead of giving them coarse game deltas.
	const double FixedDelta = 1. / (60. * EffectiveDilation);
	WorldSettings->MinUndilatedFrameTime = 0.f;
	WorldSettings->MaxUndilatedFrameTime = 1.f;
	FApp::SetFixedDeltaTime(FixedDelta);
	FApp::SetUseFixedTimeStep(true);
	Report->SetNumberField(TEXT("effective_dilation"), EffectiveDilation);
	Report->SetNumberField(TEXT("fixed_undilated_delta_seconds"), FixedDelta);
	Report->SetNumberField(TEXT("target_game_delta_seconds"), 1. / 60.);
	Report->SetStringField(TEXT("status"), TEXT("running"));
	Report->SetStringField(TEXT("planner_class"), TEXT("AEnemyCommander"));
	Report->SetStringField(TEXT("planner_spawn_order"), TEXT("team5 then team0; engine actor tick order is not symmetrized"));
	Report->SetNumberField(TEXT("initial_wallet_team0"), HumanCommander->Resources);
	Report->SetNumberField(TEXT("initial_wallet_team5"), State.EnemyCommander->Resources);
	Report->SetArrayField(TEXT("hq_position_team0"), Position(State.FriendlyHeadquarters->GetActorLocation()));
	Report->SetArrayField(TEXT("hq_position_team5"), Position(State.EnemyHeadquarters->GetActorLocation()));
	PreviousHQHealth[0] = State.FriendlyHeadquarters->Health;
	PreviousHQHealth[1] = State.EnemyHeadquarters->Health;
	TArray<TSharedPtr<FJsonValue>> Regions;
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region)) continue;
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
		for (int32 Index : Region->Neighbours) Neighbours.Add(MakeShared<FJsonValueNumber>(Index));
		Row->SetArrayField(TEXT("polygon"), MoveTemp(Polygon));
		Row->SetArrayField(TEXT("neighbours"), MoveTemp(Neighbours));
		Regions.Add(MakeShared<FJsonValueObject>(Row));
		RegionOwners.Add(Region->RegionIndex, State.GetRegionController(Region->RegionIndex));
	}
	Report->SetArrayField(TEXT("regions"), MoveTemp(Regions));
	TArray<TSharedPtr<FJsonValue>> Deposits;
	for (const ADepositSite* Deposit : State.Deposits)
	{
		if (!IsValid(Deposit)) continue;
		const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("id"), Deposit->GetName());
		Row->SetNumberField(TEXT("region"), Deposit->RegionIndex);
		Row->SetBoolField(TEXT("rich"), Deposit->bRich);
		Row->SetNumberField(TEXT("initial_amount"), Deposit->Remaining);
		Row->SetNumberField(TEXT("rate"), Deposit->RatePerSecond());
		Row->SetArrayField(TEXT("position"), Position(Deposit->GetActorLocation()));
		Deposits.Add(MakeShared<FJsonValueObject>(Row));
	}
	Report->SetArrayField(TEXT("deposits"), MoveTemp(Deposits));
	TArray<TSharedPtr<FJsonValue>> Units;
	for (const UArmyUnitDefinition* Definition : State.Content->Units)
	{
		if (!IsValid(Definition)) continue;
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
	Report->SetArrayField(TEXT("unit_definitions"), MoveTemp(Units));
	Event(TEXT("match_started"));
	Observe(State);
	Snapshot(State, 0.);
	if (!Flush()) Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Cannot write initial telemetry"));
	return !bFinished;
}

TSharedRef<FJsonObject> UMatchSimulationSubsystem::Event(const TCHAR* Kind, int32 Team)
{
	const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("kind"), Kind);
	Row->SetNumberField(TEXT("time"), bStarted ? GetWorld()->GetTimeSeconds() - StartWorldTime : 0.);
	Row->SetNumberField(TEXT("team"), Team);
	Append(*Report, TEXT("events"), Row);
	return Row;
}

void UMatchSimulationSubsystem::Observe(ACommandGameState& State)
{
	for (auto It = ObservedUnits.CreateIterator(); It; ++It)
	{
		AArmyUnit* Unit = It.Key().Get();
		if (!IsValid(Unit) || !Unit->IsAlive())
		{
			++Casualties[It.Value().TeamSlot];
			It.RemoveCurrent();
		}
	}
	for (TActorIterator<AArmyUnit> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive()) continue;
		const int32 Slot = TeamSlot(It->GetTeamIndex());
		if (Slot == INDEX_NONE) continue;
		const TWeakObjectPtr<AArmyUnit> Key(*It);
		FObservedUnit* Previous = ObservedUnits.Find(Key);
		if (!Previous)
		{
			++Produced[Slot];
			Attacks[Slot] += It->AttackCount;
			ObservedHealthLoss[Slot] += FMath::Max(0, It->MaxHealth() - It->GetHealth());
			ObservedUnits.Add(Key, { Slot, It->GetHealth(), It->AttackCount });
		}
		else
		{
			ObservedHealthLoss[Slot] += FMath::Max(0, Previous->Health - It->GetHealth());
			Attacks[Slot] += static_cast<uint32>(It->AttackCount - Previous->Attacks);
			Previous->Health = It->GetHealth();
			Previous->Attacks = It->AttackCount;
		}
	}
	for (const ACommandBuilding* Building : State.Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive()) continue;
		const int32 Slot = TeamSlot(Building->TeamIndex);
		const int32 Kind = Building->Kind == EBuildingKind::Extractor ? 0 : Building->IsProducer() ? 1 : INDEX_NONE;
		if (Slot == INDEX_NONE || Kind == INDEX_NONE) continue;
		if (!FirstPlaced[Slot][Kind])
		{
			FirstPlaced[Slot][Kind] = true;
			const TSharedRef<FJsonObject> Row = Event(Kind == 0 ? TEXT("first_extractor") : TEXT("first_barracks"), Building->TeamIndex);
			Row->SetArrayField(TEXT("position"), Position(Building->GetActorLocation()));
		}
		if (Building->IsComplete() && !FirstComplete[Slot][Kind])
		{
			FirstComplete[Slot][Kind] = true;
			Event(Kind == 0 ? TEXT("first_extractor_complete") : TEXT("first_barracks_complete"), Building->TeamIndex);
		}
	}
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region)) continue;
		const int32 Owner = State.GetRegionController(Region->RegionIndex);
		int32& Previous = RegionOwners.FindChecked(Region->RegionIndex);
		if (Owner == Previous) continue;
		const TSharedRef<FJsonObject> Row = Event(TEXT("region_control"), Owner);
		Row->SetNumberField(TEXT("region"), Region->RegionIndex);
		Row->SetNumberField(TEXT("previous_team"), Previous);
		const int32 Slot = TeamSlot(Owner);
		if (Slot != INDEX_NONE && !FirstCapture[Slot] && Region->RegionRole != ERegionRole::Main)
		{
			FirstCapture[Slot] = true;
			Event(TEXT("first_capture"), Owner)->SetNumberField(TEXT("region"), Region->RegionIndex);
		}
		Previous = Owner;
	}
	for (ADepositSite* Deposit : State.Deposits)
		if (IsValid(Deposit) && Deposit->Remaining == 0 && !Depleted.Contains(Deposit))
		{
			Depleted.Add(Deposit);
			const TSharedRef<FJsonObject> Row = Event(TEXT("deposit_depleted"), IsValid(Deposit->Extractor) ? Deposit->Extractor->TeamIndex : -1);
			Row->SetStringField(TEXT("deposit"), Deposit->GetName());
			Row->SetNumberField(TEXT("region"), Deposit->RegionIndex);
		}
	const AHeadquarters* HQs[] = { State.FriendlyHeadquarters, State.EnemyHeadquarters };
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		if (HQs[Slot]->Health < PreviousHQHealth[Slot])
		{
			const TSharedRef<FJsonObject> Row = Event(TEXT("hq_damage"), Slot == 0 ? 0 : 5);
			Row->SetNumberField(TEXT("damage"), PreviousHQHealth[Slot] - HQs[Slot]->Health);
			Row->SetNumberField(TEXT("health"), HQs[Slot]->Health);
		}
		PreviousHQHealth[Slot] = HQs[Slot]->Health;
	}
}

void UMatchSimulationSubsystem::Snapshot(ACommandGameState& State, double ScheduledTime)
{
	const double Time = GetWorld()->GetTimeSeconds() - StartWorldTime;
	const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
	Row->SetNumberField(TEXT("time"), Time);
	Row->SetNumberField(TEXT("scheduled_time"), ScheduledTime);
	Row->SetNumberField(TEXT("effective_dilation"), GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation());
	TArray<TSharedPtr<FJsonValue>> Teams;
	for (int32 Slot = 0; Slot < 2; ++Slot)
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
		int32 Barracks = 0, Extractors = 0, CompleteExtractors = 0, Alive = 0, Reinforcing = 0, Controlled = 0;
		int64 Remaining = 0;
		int32 Roles[3] = {};
		TMap<int32, int32> UnitRegions;
		TArray<TSharedPtr<FJsonValue>> Buildings, Forces, OwnedRegions;
		for (const ACommandBuilding* Building : State.Buildings)
		{
			if (!IsValid(Building) || !Building->IsAlive() || Building->TeamIndex != Team) continue;
			if (Building->IsProducer()) ++Barracks;
			if (Building->Kind == EBuildingKind::Extractor)
			{
				++Extractors;
				if (Building->IsComplete()) ++CompleteExtractors;
			}
			const TSharedRef<FJsonObject> Detail = MakeShared<FJsonObject>();
			Detail->SetStringField(TEXT("id"), Building->GetName());
			Detail->SetNumberField(TEXT("kind"), static_cast<uint8>(Building->Kind));
			Detail->SetNumberField(TEXT("health"), Building->Health);
			Detail->SetNumberField(TEXT("construction_progress"), Building->ConstructionProgress);
			Detail->SetArrayField(TEXT("position"), Position(Building->GetActorLocation()));
			Detail->SetNumberField(TEXT("production_state"), static_cast<uint8>(Building->GetProductionState()));
			Detail->SetNumberField(TEXT("production_progress_seconds"), Building->ProductionProgressSeconds);
			Detail->SetNumberField(TEXT("unit_index"), Building->ProductionUnitIndex);
			Detail->SetNumberField(TEXT("goal"), static_cast<uint8>(Building->ForceGoal));
			Detail->SetNumberField(TEXT("goal_region"), Building->GoalRegionIndex);
			Detail->SetNumberField(TEXT("waypoint_region"), Building->GetGoalWaypointRegionIndex());
			Buildings.Add(MakeShared<FJsonValueObject>(Detail));
		}
		for (const AMapRegion* Region : State.Regions)
			if (IsValid(Region) && State.GetRegionController(Region->RegionIndex) == Team)
			{
				++Controlled;
				OwnedRegions.Add(MakeShared<FJsonValueNumber>(Region->RegionIndex));
			}
		for (const ADepositSite* Deposit : State.Deposits)
			if (IsValid(Deposit) && State.GetRegionController(Deposit->RegionIndex) == Team) Remaining += Deposit->Remaining;
		for (TActorIterator<AArmyUnit> It(GetWorld()); It; ++It)
		{
			if (!It->IsAlive() || It->GetTeamIndex() != Team) continue;
			++Alive;
			if (It->IsReinforcing()) ++Reinforcing;
			const int32 Role = static_cast<uint8>(It->GetUnitRole());
			if (Role < 3) ++Roles[Role];
			const AMapRegion* Region = State.FindRegionAt(It->GetActorLocation());
			++UnitRegions.FindOrAdd(Region ? Region->RegionIndex : INDEX_NONE);
		}
		for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
		{
			if (It->GetTeamIndex() != Team) continue;
			int32 Strength = 0;
			for (const AArmyUnit* Unit : It->GetUnits()) if (IsValid(Unit) && Unit->IsAlive()) ++Strength;
			if (!Strength) continue;
			const TSharedRef<FJsonObject> Detail = MakeShared<FJsonObject>();
			Detail->SetStringField(TEXT("id"), It->GetName());
			Detail->SetNumberField(TEXT("number"), It->ForceNumber);
			Detail->SetNumberField(TEXT("alive"), Strength);
			Detail->SetArrayField(TEXT("center"), Position(It->GetCenter()));
			const AMapRegion* Region = State.FindRegionAt(It->GetCenter());
			Detail->SetNumberField(TEXT("region"), Region ? Region->RegionIndex : INDEX_NONE);
			Forces.Add(MakeShared<FJsonValueObject>(Detail));
		}
		const TSharedRef<FJsonObject> Concentration = MakeShared<FJsonObject>();
		int32 Largest = 0;
		for (const auto& Pair : UnitRegions)
		{
			Concentration->SetNumberField(FString::FromInt(Pair.Key), Pair.Value);
			if (Pair.Key != INDEX_NONE) Largest = FMath::Max(Largest, Pair.Value);
		}
		Summary->SetNumberField(TEXT("barracks"), Barracks);
		Summary->SetNumberField(TEXT("extractors"), Extractors);
		Summary->SetNumberField(TEXT("completed_extractors"), CompleteExtractors);
		Summary->SetNumberField(TEXT("deposits_remaining"), static_cast<double>(Remaining));
		Summary->SetNumberField(TEXT("units_alive"), Alive);
		Summary->SetNumberField(TEXT("units_reinforcing"), Reinforcing);
		Summary->SetNumberField(TEXT("regions_controlled"), Controlled);
		Summary->SetArrayField(TEXT("region_indices"), MoveTemp(OwnedRegions));
		Summary->SetArrayField(TEXT("units_by_role"), { MakeShared<FJsonValueNumber>(Roles[0]), MakeShared<FJsonValueNumber>(Roles[1]), MakeShared<FJsonValueNumber>(Roles[2]) });
		Summary->SetObjectField(TEXT("units_by_region"), Concentration);
		Summary->SetNumberField(TEXT("largest_region_unit_share"), Alive ? static_cast<double>(Largest) / Alive : 0.);
		Summary->SetArrayField(TEXT("buildings"), MoveTemp(Buildings));
		Summary->SetArrayField(TEXT("forces"), MoveTemp(Forces));
		Teams.Add(MakeShared<FJsonValueObject>(Summary));
	}
	Row->SetArrayField(TEXT("teams"), MoveTemp(Teams));
	TArray<TSharedPtr<FJsonValue>> Deposits;
	for (const ADepositSite* Deposit : State.Deposits)
	{
		if (!IsValid(Deposit)) continue;
		const TSharedRef<FJsonObject> Detail = MakeShared<FJsonObject>();
		Detail->SetStringField(TEXT("id"), Deposit->GetName());
		Detail->SetNumberField(TEXT("region"), Deposit->RegionIndex);
		Detail->SetNumberField(TEXT("remaining"), Deposit->Remaining);
		Detail->SetNumberField(TEXT("controller"), State.GetRegionController(Deposit->RegionIndex));
		Detail->SetNumberField(TEXT("extractor_team"), IsValid(Deposit->Extractor) ? Deposit->Extractor->TeamIndex : -1);
		Deposits.Add(MakeShared<FJsonValueObject>(Detail));
	}
	Row->SetArrayField(TEXT("deposits"), MoveTemp(Deposits));
	Append(*Report, TEXT("snapshots"), Row);
}

bool UMatchSimulationSubsystem::Flush()
{
	const FString& Path = FSimulationSettings::Get().Output;
	if (Path.IsEmpty() || FPaths::IsRelative(Path)) return false;
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)) return false;
	Report->SetNumberField(TEXT("duration"), bStarted ? GetWorld()->GetTimeSeconds() - StartWorldTime : 0.);
	Report->SetNumberField(TEXT("wall_duration"), FPlatformTime::Seconds() - StartWallTime);
	Report->SetNumberField(TEXT("max_game_delta_seconds"), MaxGameDelta);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Report, Writer)
		|| !FFileHelper::SaveStringToFile(Json, *(Path + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
	return IFileManager::Get().Move(*Path, *(Path + TEXT(".tmp")), true, false, false, true);
}

void UMatchSimulationSubsystem::Finish(const TCHAR* Status, const TCHAR* Outcome, int32 Winner, const FString& Error)
{
	if (bFinished) return;
	bFinished = true;
	Report->SetStringField(TEXT("status"), Status);
	Report->SetStringField(TEXT("outcome"), Outcome);
	if (Winner == 0 || Winner == 5) Report->SetNumberField(TEXT("winner"), Winner);
	else Report->SetField(TEXT("winner"), MakeShared<FJsonValueNull>());
	if (!Error.IsEmpty()) Report->SetStringField(TEXT("error"), Error);
	Event(TEXT("match_finished"))->SetStringField(TEXT("outcome"), Outcome);
	const bool bWritten = Flush();
	const bool bSuccess = FCString::Strcmp(Status, TEXT("complete")) == 0 && bWritten;
	UE_LOG(LogTemp, Display, TEXT("Simulation finished status=%s outcome=%s written=%d error=%s"), Status, Outcome, bWritten, *Error);
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1, TEXT("MatchSimulation"));
}

void UMatchSimulationSubsystem::Tick(float DeltaTime)
{
	if (bFinished || !GetWorld()->HasBegunPlay()) return;
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Requested world is not a command match"));
		return;
	}
	if (!bStarted)
	{
		Start(*State);
		return;
	}
	if (!HumanCommander.IsValid() || !Autopilot.IsValid() || !IsValid(State->EnemyCommander)
		|| !IsValid(State->FriendlyHeadquarters) || !IsValid(State->EnemyHeadquarters))
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Required match actor disappeared before completion"));
		return;
	}
	MaxGameDelta = FMath::Max(MaxGameDelta, DeltaTime);
	Observe(*State);
	const double Time = GetWorld()->GetTimeSeconds() - StartWorldTime;
	if (State->MatchResult != EMatchResult::Ongoing)
	{
		const int32 Winner = State->MatchResult == EMatchResult::Victory ? 0 : 5;
		if ((Winner == 0 ? State->EnemyHeadquarters->Health : State->FriendlyHeadquarters->Health) > 0)
		{
			Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Terminal match result without destroyed HQ"));
			return;
		}
		Snapshot(*State, Time);
		Finish(TEXT("complete"), TEXT("hq_destroyed"), Winner);
		return;
	}
	if (Time >= FSimulationSettings::Get().TimeCap)
	{
		// A same-frame HQ death must be an outcome, not a time-cap draw, even before GameMode's tick.
		Snapshot(*State, Time);
		const int32 Winner = State->FriendlyHeadquarters->Health <= 0 ? 5 : State->EnemyHeadquarters->Health <= 0 ? 0 : -1;
		Finish(TEXT("complete"), Winner == -1 ? TEXT("time_cap") : TEXT("hq_destroyed"), Winner);
		return;
	}
	if (Time >= NextSnapshot)
	{
		Snapshot(*State, NextSnapshot);
		// Never fabricate past states if a coarse frame skipped a sampling boundary.
		NextSnapshot = (FMath::FloorToDouble(Time / 30.) + 1.) * 30.;
		if (!Flush()) Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Cannot persist telemetry checkpoint"));
	}
}

void UMatchSimulationSubsystem::Deinitialize()
{
	if (Report.IsValid() && !bFinished)
	{
		Report->SetStringField(TEXT("status"), TEXT("interrupted"));
		Report->SetStringField(TEXT("error"), TEXT("World ended before a natural match result"));
		Flush();
	}
	Super::Deinitialize();
}
