#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "MatchSimulationSubsystem.h"

#include "ArenaBounds.h"
#include "CapturePoint.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
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
#include "GameFramework/GameModeBase.h"
#include "NavigationSystem.h"
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
int32 TeamSlot(int32 Team) { return Team == 0 ? 0 : Team == 5 ? 1
															  : INDEX_NONE; }

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

void PlanFields(FJsonObject& Row, const FJevPublishedPlan& Plan, int32 ForceNumber,
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
}

namespace
{
constexpr int32 DuelBudget = 120;
constexpr float DuelClearance = 1200.f;
constexpr float DuelSpacing = 160.f;
constexpr float DuelJitter = 40.f;

// Json's number storage is protected; expose mutation for encounter-owned values
// so live telemetry observations do not allocate replacement fields each frame.
class FDuelNumber : public FJsonValueNumber
{
public:
	explicit FDuelNumber(double Number) : FJsonValueNumber(Number) {}
	void Set(double Number) { Value = Number; }
};

TArray<TSharedPtr<FJsonValue>> Numbers(double Left, double Right)
{
	return { MakeShared<FDuelNumber>(Left), MakeShared<FDuelNumber>(Right) };
}

void UpdateNumbers(FJsonObject& Row, const FString& Field, double Left, double Right)
{
	const TArray<TSharedPtr<FJsonValue>>& Values = Row.GetArrayField(Field);
	StaticCastSharedPtr<FDuelNumber>(Values[0])->Set(Left);
	StaticCastSharedPtr<FDuelNumber>(Values[1])->Set(Right);
}
}

FSimulationDuelRunner::FSimulationDuelRunner()
	: Report(MakeShared<FJsonObject>())
{
	Report->SetStringField(TEXT("mode"), TEXT("duel"));
	Report->SetStringField(TEXT("status"), TEXT("initializing"));
	Report->SetStringField(TEXT("outcome"), TEXT("none"));
	Report->SetField(TEXT("winner"), MakeShared<FJsonValueNull>());
	Report->SetField(TEXT("duration"), MakeShared<FDuelNumber>(0.));
	Report->SetNumberField(TEXT("budget_per_side"), DuelBudget);
	Report->SetBoolField(TEXT("configuration_fees_included"), false);
	Report->SetArrayField(TEXT("unit_definitions"), {});
	Report->SetArrayField(TEXT("duels"), {});
}

FSimulationDuelRunner::~FSimulationDuelRunner()
{
	ClearPair();
	RestoreHeadquarters();
}

bool FSimulationDuelRunner::Start(ACommandGameState& InState, int32 Seed)
{
	if (bStarted)
	{
		Error = TEXT("Duel runner cannot be started twice");
		return false;
	}
	UWorld* World = InState.GetWorld();
	if (!World || World->GetNetMode() != NM_Standalone || !InState.HasAuthority()
		|| !IsValid(InState.Content) || !IsValid(InState.Arena)
		|| !IsValid(InState.FriendlyHeadquarters) || !IsValid(InState.EnemyHeadquarters)
		|| !IsValid(InState.EnemyCommander) || InState.MatchResult != EMatchResult::Ongoing)
	{
		Error = TEXT("Duel requires an initialized authoritative standalone map");
		return false;
	}
	State = &InState;
	Wallets[1] = InState.EnemyCommander;
	for (APlayerState* Player : InState.PlayerArray)
		if (ACommandPlayerState* Wallet = Cast<ACommandPlayerState>(Player))
			if (Wallet->TeamIndex == 0 && Wallet->CommanderIndex >= 0 && Wallet->CommanderIndex < 5)
			{
				Wallets[0] = Wallet;
				break;
			}
	if (!Wallets[0].IsValid())
	{
		Error = TEXT("Duel requires an initialized team-0 wallet");
		return false;
	}
	TArray<TSharedPtr<FJsonValue>> Rows;
	TSet<FName> Ids;
	for (int32 Index = 0; Index < InState.Content->Units.Num(); ++Index)
	{
		const UArmyUnitDefinition* Definition = InState.Content->Unit(Index);
		if (!IsValid(Definition))
			continue;
		if (Definition->Id.IsNone() || Ids.Contains(Definition->Id) || Definition->UnitCost <= 0
			|| Definition->UnitCost > DuelBudget || Definition->MaxHealth <= 0 || Definition->AttackDamage <= 0
			|| !FMath::IsFinite(Definition->Interval) || Definition->Interval <= 0.f
			|| !FMath::IsFinite(Definition->Range) || Definition->Range <= 0.f)
		{
			Error = TEXT("Combat catalogue contains duplicate identities or invalid duel definitions");
			return false;
		}
		Ids.Add(Definition->Id);
		Definitions.Add(Index);
		const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("id"), Definition->Id.ToString());
		Row->SetNumberField(TEXT("cost"), Definition->UnitCost);
		Row->SetNumberField(TEXT("capacity"), Definition->Capacity);
		Row->SetNumberField(TEXT("health"), Definition->MaxHealth);
		Row->SetNumberField(TEXT("damage"), Definition->AttackDamage);
		Row->SetNumberField(TEXT("attack_interval"), Definition->Interval);
		Row->SetNumberField(TEXT("range"), Definition->Range);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	if (Definitions.IsEmpty())
	{
		Error = TEXT("Combat catalogue is empty");
		return false;
	}
	bStarted = true;
	Random.Initialize(Seed);
	SpawnFirstSide = Seed % 2 == 0 ? 1 : 0;
	Report->SetNumberField(TEXT("seed"), Seed);
	Report->SetStringField(TEXT("map"), UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));
	Report->SetArrayField(TEXT("unit_definitions"), MoveTemp(Rows));
	// Remove every independent source of orders, paid production and capture.
	for (TActorIterator<AEnemyCommander> It(World); It; ++It)
		It->Destroy();
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		It->Destroy();
	for (TActorIterator<AArmyUnit> It(World); It; ++It)
	{
		if (AController* Controller = It->GetController())
			Controller->Destroy();
		It->Destroy();
	}
	for (TActorIterator<ACommandBuilding> It(World); It; ++It)
		It->Destroy();
	InState.Buildings.Reset();
	for (TActorIterator<ACapturePoint> It(World); It; ++It)
		PauseActor(**It);
	PauseActor(InState);
	if (AGameModeBase* Mode = World->GetAuthGameMode())
		PauseActor(*Mode);
	for (int32 Side = 0; Side < 2; ++Side)
		Wallets[Side]->Doctrine = EArmyDoctrine::None;
	Headquarters[0] = InState.FriendlyHeadquarters;
	Headquarters[1] = InState.EnemyHeadquarters;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		HQCollision[Side] = Headquarters[Side]->GetActorEnableCollision();
		Headquarters[Side]->SetActorEnableCollision(false);
	}
	// Unregistered HQs cannot be selected by CombatTarget; health is untouched.
	InState.FriendlyHeadquarters = nullptr;
	InState.EnemyHeadquarters = nullptr;
	if (!FindGround() || !StartPair())
	{
		ClearPair();
		RestoreHeadquarters();
		return false;
	}
	Report->SetStringField(TEXT("status"), TEXT("running"));
	return true;
}

bool FSimulationDuelRunner::FindGround()
{
	UWorld* World = State->GetWorld();
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const ANavigationData* NavData = Navigation
		? Navigation->GetNavDataForProps(GetDefault<AArmyUnit>()->GetNavAgentPropertiesRef())
		: nullptr;
	if (!Navigation || !NavData)
	{
		Error = TEXT("Duel map navigation is unavailable");
		return false;
	}
	TArray<FVector> Candidates;
	for (const AMapRegion* Region : State->Regions)
		if (IsValid(Region) && Region->RegionRole != ERegionRole::Main && IsValid(Region->Anchor))
			Candidates.Add(State->GetRegionAnchor(Region->RegionIndex));
	Candidates.Sort([](const FVector& A, const FVector& B) { return A.SizeSquared2D() < B.SizeSquared2D(); });
	for (const FVector& Candidate : Candidates)
	{
		FNavLocation Ground;
		if (!Navigation->ProjectPointToNavigation(Candidate, Ground, FVector(5.f, 5.f, 200.f), NavData)
			|| FVector::DistSquared2D(Candidate, Ground.Location) > 25.f
			|| World->OverlapBlockingTestByChannel(Ground.Location + FVector(0.f, 0.f, 100.f),
				FQuat::Identity, ECC_Pawn, FCollisionShape::MakeBox(FVector(DuelClearance, DuelClearance, 60.f))))
			continue;
		bool bOpen = true;
		// Dense projection plus unobstructed nav rays and whole-area collision
		// reject holes, walls, ledges and narrow corridors, not merely spawn points.
		for (float X = -DuelClearance; bOpen && X <= DuelClearance; X += 100.f)
			for (float Y = -DuelClearance; bOpen && Y <= DuelClearance; Y += 100.f)
			{
				const FVector Sample = Ground.Location + FVector(X, Y, 0.f);
				FNavLocation Projected;
				FVector Hit;
				bOpen = State->Arena->ContainsTravel(Sample)
					&& Navigation->ProjectPointToNavigation(Sample, Projected, FVector(5.f, 5.f, 30.f), NavData)
					&& FVector::DistSquared2D(Sample, Projected.Location) <= 25.f
					&& FMath::Abs(Projected.Location.Z - Ground.Location.Z) <= 10.f
					&& !Navigation->NavigationRaycast(World, Ground.Location, Projected.Location, Hit);
			}
		if (!bOpen)
			continue;
		Center = Ground.Location;
		const TSharedRef<FJsonObject> Geometry = MakeShared<FJsonObject>();
		Geometry->SetArrayField(TEXT("center"), Position(Center));
		Geometry->SetStringField(TEXT("site_selection"), TEXT("non-main region anchors ordered by distance from world origin"));
		Geometry->SetNumberField(TEXT("open_half_extent"), DuelClearance);
		Geometry->SetNumberField(TEXT("navigation_sample_spacing"), 100.f);
		Geometry->SetNumberField(TEXT("spawn_spacing"), DuelSpacing);
		Geometry->SetNumberField(TEXT("spawn_jitter"), DuelJitter);
		Geometry->SetNumberField(TEXT("minimum_member_separation"), DuelSpacing - 2.f * DuelJitter);
		Geometry->SetNumberField(TEXT("spawn_first_team"), SpawnFirstSide == 0 ? 0 : 5);
		Geometry->SetNumberField(TEXT("squad_center_separation"), 1000.f);
		Geometry->SetNumberField(TEXT("pursuit_radius"), AArmyGroup::PursuitRadius);
		Geometry->SetStringField(TEXT("verification"), TEXT("whole-area pawn collision; dense nav projection and center rays"));
		Report->SetObjectField(TEXT("geometry"), Geometry);
		return true;
	}
	Error = TEXT("Map has no verified obstacle-free duel area");
	return false;
}

bool FSimulationDuelRunner::StartPair()
{
	Elapsed = 0.;
	LastDamageElapsed = -1.;
	Current = MakeShared<FJsonObject>();
	const int32 Count = Definitions.Num();
	const int32 Indices[2] = { Definitions[PairIndex / Count], Definitions[PairIndex % Count] };
	StallTimeout = FMath::Max(30., 10. * FMath::Max(State->Content->Unit(Indices[0])->Interval, State->Content->Unit(Indices[1])->Interval));
	Current->SetNumberField(TEXT("stall_timeout_seconds"), StallTimeout);
	Current->SetField(TEXT("no_damage_seconds"), MakeShared<FDuelNumber>(0.));
	Current->SetNumberField(TEXT("spawn_first_team"), SpawnFirstSide == 0 ? 0 : 5);
	TArray<TSharedPtr<FJsonValue>> Spawns[2];
	const float Angle = Random.FRandRange(0.f, 2.f * PI);
	const FVector Forward(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
	const FVector Across(-Forward.Y, Forward.X, 0.f);
	Current->SetNumberField(TEXT("spawn_angle_radians"), Angle);
	for (int32 Order = 0; Order < 2; ++Order)
	{
		const int32 Side = (SpawnFirstSide + Order) % 2;
		const UArmyUnitDefinition* Definition = State->Content->Unit(Indices[Side]);
		Initial[Side] = DuelBudget / Definition->UnitCost;
		Spent[Side] = Initial[Side] * Definition->UnitCost;
		Survivors[Side] = Initial[Side];
		Damage[Side] = Attacks[Side] = 0;
		Current->SetStringField(Side == 0 ? TEXT("left") : TEXT("right"), Definition->Id.ToString());
		const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Initial[Side])));
		const int32 Rows = FMath::DivideAndRoundUp(Initial[Side], Columns);
		AArmyGroup* Group = nullptr;
		for (int32 Index = 0; Index < Initial[Side]; ++Index)
		{
			const float Sign = Side == 0 ? -1.f : 1.f;
			const float X = Sign * (500.f + ((Index % Columns) - (Columns - 1) * .5f) * DuelSpacing);
			const float Y = ((Index / Columns) - (Rows - 1) * .5f) * DuelSpacing;
			const FVector Spawn = Center + Forward * (X + Random.FRandRange(-DuelJitter, DuelJitter))
				+ Across * (Y + Random.FRandRange(-DuelJitter, DuelJitter));
			// Six-slot legacy formations are separate groups, never a squad cap.
			if (Index % 6 == 0)
			{
				const FTransform Transform(Spawn);
				Group = State->GetWorld()->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
					Wallets[Side]->GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Group)
				{
					Error = TEXT("Could not spawn duel group");
					return false;
				}
				Group->Initialize({ Side == 0 ? 0 : 5, Wallets[Side].Get(), Index / 6, nullptr, Spawn });
				Group->FinishSpawning(Transform);
				Groups.Add(Group);
			}
			AArmyUnit* Unit = Group->SpawnMember(Indices[Side], Spawn, Index % 6);
			if (!Unit)
			{
				Error = TEXT("Could not spawn a joined duel member on verified ground");
				return false;
			}
			Members.Add({ Unit, Side, Unit->GetHealth(), Unit->AttackCount });
			Spawns[Side].Add(MakeShared<FJsonValueArray>(Position(Unit->GetActorLocation())));
		}
	}
	const AMapRegion* TargetRegion = State->FindRegionAt(Center);
	for (const TWeakObjectPtr<AArmyGroup>& Group : Groups)
		if (!TargetRegion
			|| !FCommandService::SetRetreatThreshold(Group->GetOwningPlayerState(), Group.Get(), ERetreatThreshold::Never)
			|| !FCommandService::IssueForceOrder(Group->GetOwningPlayerState(), Group.Get(), EForceVerb::Attack, TargetRegion->RegionIndex))
		{
			Error = TEXT("Duel group rejected its real Attack order");
			return false;
		}
	Current->SetArrayField(TEXT("spent"), Numbers(Spent[0], Spent[1]));
	Current->SetArrayField(TEXT("initial_units"), Numbers(Initial[0], Initial[1]));
	Current->SetArrayField(TEXT("survivors"), Numbers(Survivors[0], Survivors[1]));
	Current->SetArrayField(TEXT("survivor_power"), Numbers(Spent[0], Spent[1]));
	Current->SetArrayField(TEXT("damage_dealt"), Numbers(0, 0));
	Current->SetArrayField(TEXT("attacks"), Numbers(0, 0));
	Current->SetArrayField(TEXT("spawn_positions_left"), MoveTemp(Spawns[0]));
	Current->SetArrayField(TEXT("spawn_positions_right"), MoveTemp(Spawns[1]));
	Current->SetField(TEXT("duration"), MakeShared<FDuelNumber>(0.));
	Current->SetStringField(TEXT("outcome"), TEXT("running"));
	Current->SetField(TEXT("winner"), MakeShared<FJsonValueNull>());
	Report->SetObjectField(TEXT("current_duel"), Current.ToSharedRef());
	return true;
}

void FSimulationDuelRunner::Observe()
{
	Survivors[0] = Survivors[1] = 0;
	for (FMember& Member : Members)
	{
		if (AArmyUnit* Unit = Member.Unit.Get())
		{
			const int32 Health = Unit->GetHealth();
			const int32 RemovedHealth = FMath::Max(0, Member.Health - Health);
			Damage[1 - Member.Side] += RemovedHealth;
			if (RemovedHealth > 0)
				LastDamageElapsed = Elapsed;
			Attacks[Member.Side] += Unit->AttackCount - Member.Attacks;
			Member.Health = Health;
			Member.Attacks = Unit->AttackCount;
			Survivors[Member.Side] += Unit->IsAlive() ? 1 : 0;
		}
		else if (Member.Health > 0)
			Error = TEXT("Live duel member disappeared without an observed combat death");
	}
	if (LastDamageElapsed < 0. && (Attacks[0] > 0 || Attacks[1] > 0))
		LastDamageElapsed = Elapsed;
}

void FSimulationDuelRunner::UpdateRow() const
{
	if (!Current.IsValid() || !Error.IsEmpty())
		return;
	static const FString SurvivorsField(TEXT("survivors"));
	static const FString PowerField(TEXT("survivor_power"));
	static const FString DamageField(TEXT("damage_dealt"));
	static const FString AttacksField(TEXT("attacks"));
	static const FString DurationField(TEXT("duration"));
	static const FString NoDamageField(TEXT("no_damage_seconds"));
	UpdateNumbers(*Current, SurvivorsField, Survivors[0], Survivors[1]);
	UpdateNumbers(*Current, PowerField,
		Survivors[0] * (Spent[0] / Initial[0]), Survivors[1] * (Spent[1] / Initial[1]));
	UpdateNumbers(*Current, DamageField, Damage[0], Damage[1]);
	UpdateNumbers(*Current, AttacksField, Attacks[0], Attacks[1]);
	StaticCastSharedPtr<FDuelNumber>(Current->GetField<EJson::Number>(DurationField))->Set(Elapsed);
	StaticCastSharedPtr<FDuelNumber>(Current->GetField<EJson::Number>(NoDamageField))->Set(LastDamageElapsed < 0. ? 0. : Elapsed - LastDamageElapsed);
	StaticCastSharedPtr<FDuelNumber>(Report->GetField<EJson::Number>(DurationField))->Set(TotalElapsed);
}

TSharedRef<FJsonObject> FSimulationDuelRunner::GetReport() const
{
	UpdateRow();
	return Report;
}

void FSimulationDuelRunner::Tick(float DeltaTime, float TimeCap)
{
	if (!bStarted || bComplete || !Error.IsEmpty())
		return;
	if (!State.IsValid() || !Wallets[0].IsValid() || !Wallets[1].IsValid()
		|| !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f || !FMath::IsFinite(TimeCap) || TimeCap <= 0.f)
	{
		Error = TEXT("Duel lost its world/wallet or received invalid elapsed time/cap");
		return;
	}
	if (Elapsed == 0.)
	{
		Current->SetNumberField(TEXT("time_cap_seconds"), TimeCap);
		Report->SetNumberField(TEXT("time_cap_seconds"), TimeCap);
	}
	Elapsed += DeltaTime;
	TotalElapsed += DeltaTime;
	Observe();
	if (!Error.IsEmpty())
		return;
	const bool bWiped = Survivors[0] == 0 || Survivors[1] == 0;
	const bool bStalled = !bWiped && LastDamageElapsed >= 0. && Elapsed - LastDamageElapsed >= StallTimeout;
	if (!bWiped && !bStalled && Elapsed < TimeCap)
		return;
	UpdateRow();
	if (bStalled)
	{
		Current->SetStringField(TEXT("outcome"), TEXT("stalled"));
		Report->SetObjectField(TEXT("invalid_duel"), Current.ToSharedRef());
		Report->RemoveField(TEXT("current_duel"));
		Report->SetStringField(TEXT("status"), TEXT("failed"));
		Report->SetStringField(TEXT("outcome"), TEXT("stalled"));
		Error = FString::Printf(TEXT("Invalid stalled duel %s vs %s: no effective HP removed for %.3f game seconds (timeout %.3f) while both sides live"),
			*Current->GetStringField(TEXT("left")), *Current->GetStringField(TEXT("right")),
			Elapsed - LastDamageElapsed, StallTimeout);
		Report->SetStringField(TEXT("error"), Error);
		ClearPair();
		RestoreHeadquarters();
		return;
	}
	Current->SetStringField(TEXT("outcome"), bWiped ? TEXT("wiped") : TEXT("time_cap"));
	const int32 Winner = bWiped && (Survivors[0] > 0 || Survivors[1] > 0)
		? (Survivors[0] > 0 ? 0 : 5)
		: INDEX_NONE;
	if (Winner != INDEX_NONE)
		Current->SetNumberField(TEXT("winner"), Winner);
	Append(*Report, TEXT("duels"), Current.ToSharedRef());
	ClearPair();
	++PairIndex;
	if (PairIndex == Definitions.Num() * Definitions.Num())
	{
		bComplete = true;
		Report->RemoveField(TEXT("current_duel"));
		Report->SetStringField(TEXT("status"), TEXT("complete"));
		Report->SetStringField(TEXT("outcome"), TEXT("matrix_complete"));
		RestoreHeadquarters();
	}
	else if (!StartPair())
	{
		ClearPair();
		RestoreHeadquarters();
	}
}

void FSimulationDuelRunner::ClearPair()
{
	for (const FMember& Member : Members)
		if (AArmyUnit* Unit = Member.Unit.Get())
		{
			if (AController* Controller = Unit->GetController())
				Controller->Destroy();
			Unit->Destroy();
		}
	Members.Reset();
	for (const TWeakObjectPtr<AArmyGroup>& Group : Groups)
		if (Group.IsValid())
			Group->Destroy();
	Groups.Reset();
}

void FSimulationDuelRunner::PauseActor(AActor& Actor)
{
	PausedActors.Add({ &Actor, Actor.IsActorTickEnabled() });
	Actor.SetActorTickEnabled(false);
}

void FSimulationDuelRunner::RestoreHeadquarters()
{
	if (!State.IsValid())
		return;
	if (Headquarters[0].IsValid())
		State->FriendlyHeadquarters = Headquarters[0].Get();
	if (Headquarters[1].IsValid())
		State->EnemyHeadquarters = Headquarters[1].Get();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (Headquarters[Side].IsValid())
			Headquarters[Side]->SetActorEnableCollision(HQCollision[Side]);
		Headquarters[Side].Reset();
	}
	for (const FPausedActor& Paused : PausedActors)
		if (Paused.Actor.IsValid())
			Paused.Actor->SetActorTickEnabled(Paused.bTickEnabled);
	PausedActors.Reset();
}

FMatchSimulation::FMatchSimulation(UWorld* InWorld)
	: World(InWorld)
{
	const FSimulationSettings& Settings = FSimulationSettings::Get();
	FMath::RandInit(Settings.Seed);
	FMath::SRandInit(Settings.Seed);
	StartWallTime = FPlatformTime::Seconds();
	Report = MakeShared<FJsonObject>();
	Report->SetNumberField(TEXT("schema_version"), 1);
	if (Settings.bDuel)
		Report->SetStringField(TEXT("mode"), TEXT("duel"));
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

TStatId FMatchSimulation::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FMatchSimulation, STATGROUP_Tickables);
}

bool FMatchSimulation::Start(ACommandGameState& State)
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
	if (FSimulationSettings::Get().bDuel)
	{
		DuelRunner = MakeUnique<FSimulationDuelRunner>();
		const TSharedPtr<FJsonObject> Metadata = Report;
		if (!DuelRunner->Start(State, FSimulationSettings::Get().Seed))
		{
			Finish(TEXT("failed"), TEXT("none"), -1, DuelRunner->GetError());
			return false;
		}
		Report = DuelRunner->GetReport();
		Report->Values.Append(Metadata->Values);
		StartWorldTime = GetWorld()->GetTimeSeconds();
		bStarted = true;
		AWorldSettings* WorldSettings = GetWorld()->GetWorldSettings();
		WorldSettings->SetAllowTimeDilation(true);
		WorldSettings->SetTimeDilation(FSimulationSettings::Get().Dilation);
		const float EffectiveDilation = WorldSettings->GetEffectiveTimeDilation();
		const double FixedDelta = 1. / (60. * EffectiveDilation);
		WorldSettings->MinUndilatedFrameTime = 0.f;
		WorldSettings->MaxUndilatedFrameTime = 1.f;
		FApp::SetFixedDeltaTime(FixedDelta);
		FApp::SetUseFixedTimeStep(true);
		Report->SetNumberField(TEXT("effective_dilation"), EffectiveDilation);
		Report->SetNumberField(TEXT("fixed_undilated_delta_seconds"), FixedDelta);
		Report->SetNumberField(TEXT("target_game_delta_seconds"), 1. / 60.);
		Report->SetStringField(TEXT("status"), TEXT("running"));
		Event(TEXT("matrix_started"));
		if (!Flush())
			Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Cannot write initial duel telemetry"));
		return !bFinished;
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
		if (It->TeamIndex == 5)
			++EnemyPlanners;
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
	Report->SetArrayField(TEXT("deposits"), MoveTemp(Deposits));
	TArray<TSharedPtr<FJsonValue>> Units;
	for (const UArmyUnitDefinition* Definition : State.Content->Units)
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
	Report->SetArrayField(TEXT("unit_definitions"), MoveTemp(Units));
	Event(TEXT("match_started"));
	Observe(State);
	Snapshot(State, 0.);
	if (!Flush())
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Cannot write initial telemetry"));
	return !bFinished;
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

void FMatchSimulation::Observe(ACommandGameState& State)
{
	// Retained accepted transitions include tickets created and released between
	// snapshots, and retain creation verbs even if a ticket has already escalated.
	while (ObservedPlanHistory < State.EnemyPlanHistory.Num())
	{
		const auto& Entry = State.EnemyPlanHistory[ObservedPlanHistory++];
		const TSharedRef<FJsonObject> Row = Event(Entry.bEscalation ? TEXT("plan_escalated") : TEXT("plan_created"), 5);
		Row->SetNumberField(TEXT("time"), FMath::Max(0., Entry.TimeSeconds - StartWorldTime));
		PlanFields(*Row, Entry.Plan, Entry.ForceNumber, Entry.TargetStructureName, StartWorldTime, Entry.Plan.RemainingCommitment);
	}
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
		if (!It->IsAlive())
			continue;
		const int32 Slot = TeamSlot(It->GetTeamIndex());
		if (Slot == INDEX_NONE)
			continue;
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
		if (!IsValid(Building) || !Building->IsAlive())
			continue;
		const int32 Slot = TeamSlot(Building->TeamIndex);
		const int32 Kind = Building->Kind == EBuildingKind::Extractor ? 0 : Building->IsProducer() ? 1
																								   : INDEX_NONE;
		if (Slot == INDEX_NONE || Kind == INDEX_NONE)
			continue;
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
		if (!IsValid(Region))
			continue;
		const int32 Owner = State.GetRegionController(Region->RegionIndex);
		int32& Previous = RegionOwners.FindChecked(Region->RegionIndex);
		if (Owner == Previous)
			continue;
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
			if (!IsValid(Building) || !Building->IsAlive() || Building->TeamIndex != Team)
				continue;
			if (Building->IsProducer())
				++Barracks;
			if (Building->Kind == EBuildingKind::Extractor)
			{
				++Extractors;
				if (Building->IsComplete())
					++CompleteExtractors;
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
			const AArmyGroup* Force = Building->ForceGroup;
			Detail->SetNumberField(TEXT("verb"), IsValid(Force) ? static_cast<uint8>(Force->Verb) : -1);
			Detail->SetNumberField(TEXT("status"), IsValid(Force) ? static_cast<uint8>(Force->Status) : -1);
			Detail->SetNumberField(TEXT("target_region"), IsValid(Force) ? Force->TargetRegionIndex : INDEX_NONE);
			Detail->SetNumberField(TEXT("waypoint_region"), IsValid(Force) ? Force->WaypointRegionIndex : INDEX_NONE);
			Buildings.Add(MakeShared<FJsonValueObject>(Detail));
		}
		for (const AMapRegion* Region : State.Regions)
			if (IsValid(Region) && State.GetRegionController(Region->RegionIndex) == Team)
			{
				++Controlled;
				OwnedRegions.Add(MakeShared<FJsonValueNumber>(Region->RegionIndex));
			}
		for (const ADepositSite* Deposit : State.Deposits)
			if (IsValid(Deposit) && State.GetRegionController(Deposit->RegionIndex) == Team)
				Remaining += Deposit->Remaining;
		for (TActorIterator<AArmyUnit> It(GetWorld()); It; ++It)
		{
			if (!It->IsAlive() || It->GetTeamIndex() != Team)
				continue;
			++Alive;
			if (It->IsReinforcing())
				++Reinforcing;
			const int32 Role = static_cast<uint8>(It->GetUnitRole());
			if (Role < 3)
				++Roles[Role];
			const AMapRegion* Region = State.FindRegionAt(It->GetActorLocation());
			++UnitRegions.FindOrAdd(Region ? Region->RegionIndex : INDEX_NONE);
		}
		for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
		{
			if (It->GetTeamIndex() != Team)
				continue;
			int32 Strength = 0;
			for (const AArmyUnit* Unit : It->GetUnits())
				if (IsValid(Unit) && Unit->IsAlive())
					++Strength;
			if (!Strength)
				continue;
			const TSharedRef<FJsonObject> Detail = MakeShared<FJsonObject>();
			Detail->SetStringField(TEXT("id"), It->GetName());
			Detail->SetNumberField(TEXT("number"), It->ForceNumber);
			Detail->SetNumberField(TEXT("alive"), Strength);
			Detail->SetArrayField(TEXT("center"), Position(It->GetCenter()));
			const AMapRegion* Region = State.FindRegionAt(It->GetCenter());
			Detail->SetNumberField(TEXT("region"), Region ? Region->RegionIndex : INDEX_NONE);
			const ACommandBuilding* Producer = It->GetProductionBuilding();
			Detail->SetBoolField(TEXT("orphan"), !IsValid(Producer) || !Producer->IsAlive());
			Detail->SetNumberField(TEXT("verb"), static_cast<uint8>(It->Verb));
			Detail->SetNumberField(TEXT("status"), static_cast<uint8>(It->Status));
			Detail->SetNumberField(TEXT("target_region"), It->TargetRegionIndex);
			if (IsValid(It->TargetStructure))
				Detail->SetStringField(TEXT("target_structure"), It->TargetStructure->GetName());
			else
				Detail->SetField(TEXT("target_structure"), MakeShared<FJsonValueNull>());
			Detail->SetNumberField(TEXT("waypoint_region"), It->WaypointRegionIndex);
			Detail->SetNumberField(TEXT("resume_count"), It->ResumeCount);
			Forces.Add(MakeShared<FJsonValueObject>(Detail));
		}
		const TSharedRef<FJsonObject> Concentration = MakeShared<FJsonObject>();
		int32 Largest = 0;
		for (const auto& Pair : UnitRegions)
		{
			Concentration->SetNumberField(FString::FromInt(Pair.Key), Pair.Value);
			if (Pair.Key != INDEX_NONE)
				Largest = FMath::Max(Largest, Pair.Value);
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
	Row->SetArrayField(TEXT("deposits"), MoveTemp(Deposits));
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
	Row->SetArrayField(TEXT("enemy_plans"), MoveTemp(Plans));
	Append(*Report, TEXT("snapshots"), Row);
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

void FMatchSimulation::Finish(const TCHAR* Status, const TCHAR* Outcome, int32 Winner, const FString& Error)
{
	if (bFinished)
		return;
	bFinished = true;
	Report->SetStringField(TEXT("status"), Status);
	Report->SetStringField(TEXT("outcome"), Outcome);
	if (Winner == 0 || Winner == 5)
		Report->SetNumberField(TEXT("winner"), Winner);
	else
		Report->SetField(TEXT("winner"), MakeShared<FJsonValueNull>());
	if (!Error.IsEmpty())
		Report->SetStringField(TEXT("error"), Error);
	Event(DuelRunner ? TEXT("matrix_finished") : TEXT("match_finished"))->SetStringField(TEXT("outcome"), Outcome);
	const bool bWritten = Flush();
	const bool bSuccess = FCString::Strcmp(Status, TEXT("complete")) == 0 && bWritten;
	UE_LOG(LogTemp, Display, TEXT("Simulation finished status=%s outcome=%s written=%d error=%s"), Status, Outcome, bWritten, *Error);
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1, TEXT("MatchSimulation"));
}

void FMatchSimulation::Tick(float DeltaTime)
{
	if (bFinished || !GetWorld()->HasBegunPlay())
		return;
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Requested world is not a command match"));
		return;
	}
	if (!bStarted)
	{
		// Let the chosen map's navigation finish its initial asynchronous build.
		if (FSimulationSettings::Get().bDuel && GetWorld()->GetTimeSeconds() < 3.f)
			return;
		Start(*State);
		return;
	}
	if (DuelRunner)
	{
		MaxGameDelta = FMath::Max(MaxGameDelta, DeltaTime);
		static const FString DuelsField(TEXT("duels"));
		const int32 CompletedBefore = Report->GetArrayField(DuelsField).Num();
		DuelRunner->Tick(DeltaTime, FSimulationSettings::Get().TimeCap);
		if (!DuelRunner->GetError().IsEmpty())
		{
			const FString Outcome = Report->GetStringField(TEXT("outcome"));
			Finish(TEXT("failed"), *Outcome, -1, DuelRunner->GetError());
			return;
		}
		if (DuelRunner->IsComplete())
		{
			Finish(TEXT("complete"), TEXT("matrix_complete"), -1);
			return;
		}
		const double Time = GetWorld()->GetTimeSeconds() - StartWorldTime;
		if (Report->GetArrayField(DuelsField).Num() != CompletedBefore || Time >= NextSnapshot)
		{
			NextSnapshot = (FMath::FloorToDouble(Time / 30.) + 1.) * 30.;
			if (!Flush())
				Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Cannot persist duel checkpoint"));
		}
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
		const int32 Winner = State->FriendlyHeadquarters->Health <= 0 ? 5 : State->EnemyHeadquarters->Health <= 0 ? 0
																												  : -1;
		Finish(TEXT("complete"), Winner == -1 ? TEXT("time_cap") : TEXT("hq_destroyed"), Winner);
		return;
	}
	if (Time >= NextSnapshot)
	{
		Snapshot(*State, NextSnapshot);
		// Never fabricate past states if a coarse frame skipped a sampling boundary.
		NextSnapshot = (FMath::FloorToDouble(Time / 30.) + 1.) * 30.;
		if (!Flush())
			Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Cannot persist telemetry checkpoint"));
	}
}

FMatchSimulation::~FMatchSimulation()
{
	if (Report.IsValid() && !bFinished)
	{
		Report->SetStringField(TEXT("status"), TEXT("interrupted"));
		Report->SetStringField(TEXT("error"), TEXT("World ended before a natural match result"));
		Flush();
	}
	DuelRunner.Reset();
}

namespace CoopRTSMatchSimulation
{
namespace
{
TMap<UWorld*, TUniquePtr<FMatchSimulation>> Matches;
FDelegateHandle InitializeHandle, CleanupHandle;

void InitializeWorld(UWorld* World, const UWorld::InitializationValues)
{
	if (World->WorldType == EWorldType::Game && FSimulationSettings::Get().bEnabled)
		Matches.Add(World, MakeUnique<FMatchSimulation>(World));
}

void CleanupWorld(UWorld* World, bool, bool)
{
	Matches.Remove(World);
}
}

void Start()
{
	InitializeHandle = FWorldDelegates::OnPreWorldInitialization.AddStatic(&InitializeWorld);
	CleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&CleanupWorld);
}

void Stop()
{
	FWorldDelegates::OnPreWorldInitialization.Remove(InitializeHandle);
	FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
	Matches.Empty();
}
}
#endif
