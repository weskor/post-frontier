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
#include "EnemyCommander.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "MatchSimulationJson.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "NavigationSystem.h"

using namespace MatchSimulationJson;
using namespace SimulationDuel;

namespace
{
bool ValidDefinition(const UArmyUnitDefinition& Definition, const TSet<FName>& Ids)
{
	return !(Definition.Id.IsNone() || Ids.Contains(Definition.Id) || Definition.UnitCost <= 0
		|| Definition.UnitCost > DuelBudget || Definition.MaxHealth <= 0 || Definition.AttackDamage <= 0
		|| !FMath::IsFinite(Definition.Interval) || Definition.Interval <= 0.f
		|| !FMath::IsFinite(Definition.Range) || Definition.Range <= 0.f);
}

TSharedRef<FJsonValueObject> DefinitionRow(const UArmyUnitDefinition& Definition)
{
	const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("id"), Definition.Id.ToString());
	Row->SetNumberField(TEXT("cost"), Definition.UnitCost);
	Row->SetNumberField(TEXT("capacity"), Definition.Capacity);
	Row->SetNumberField(TEXT("health"), Definition.MaxHealth);
	Row->SetNumberField(TEXT("damage"), Definition.AttackDamage);
	Row->SetNumberField(TEXT("attack_interval"), Definition.Interval);
	Row->SetNumberField(TEXT("range"), Definition.Range);
	Row->SetNumberField(TEXT("shield"), Definition.MaxShield);
	Row->SetBoolField(TEXT("support"), Definition.Role == EUnitRole::Support);
	return MakeShared<FJsonValueObject>(Row);
}

bool ReadyForDuel(ACommandGameState& State)
{
	UWorld* World = State.GetWorld();
	return World && World->GetNetMode() == NM_Standalone && State.HasAuthority()
		&& IsValid(State.Content) && IsValid(State.Arena)
		&& IsValid(State.FriendlyHeadquarters) && IsValid(State.EnemyHeadquarters)
		&& IsValid(State.EnemyCommander) && State.MatchResult == EMatchResult::Ongoing;
}

// Dense projection plus unobstructed nav rays and whole-area collision
// reject holes, walls, ledges and narrow corridors, not merely spawn points.
bool IsOpenGround(ACommandGameState& State, UNavigationSystemV1& Navigation, const ANavigationData& NavData,
	const FNavLocation& Ground)
{
	UWorld* World = State.GetWorld();
	if (World->OverlapBlockingTestByChannel(Ground.Location + FVector(0.f, 0.f, 100.f),
			FQuat::Identity, ECC_Pawn, FCollisionShape::MakeBox(FVector(DuelClearance, DuelClearance, 60.f))))
		return false;
	bool bOpen = true;
	for (float X = -DuelClearance; bOpen && X <= DuelClearance; X += 100.f)
		for (float Y = -DuelClearance; bOpen && Y <= DuelClearance; Y += 100.f)
		{
			const FVector Sample = Ground.Location + FVector(X, Y, 0.f);
			FNavLocation Projected;
			FVector Hit;
			bOpen = State.Arena->ContainsTravel(Sample)
				&& Navigation.ProjectPointToNavigation(Sample, Projected, FVector(5.f, 5.f, 30.f), &NavData)
				&& FVector::DistSquared2D(Sample, Projected.Location) <= 25.f
				&& FMath::Abs(Projected.Location.Z - Ground.Location.Z) <= 10.f
				&& !Navigation.NavigationRaycast(World, Ground.Location, Projected.Location, Hit);
		}
	return bOpen;
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
	Report->SetArrayField(TEXT("compositions"), {});
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
	if (!ReadyForDuel(InState))
	{
		Error = TEXT("Duel requires an initialized authoritative standalone map");
		return false;
	}
	State = &InState;
	TArray<TSharedPtr<FJsonValue>> Rows;
	if (!FindWallets(InState) || !CollectDefinitions(InState, Rows))
		return false;
	BuildFights(InState);
	bStarted = true;
	Random.Initialize(Seed);
	SpawnFirstSide = Seed % 2 == 0 ? 1 : 0;
	Report->SetNumberField(TEXT("seed"), Seed);
	Report->SetStringField(TEXT("map"), UWorld::RemovePIEPrefix(InState.GetWorld()->GetOutermost()->GetName()));
	Report->SetArrayField(TEXT("unit_definitions"), MoveTemp(Rows));
	IsolateWorld(InState);
	if (!FindGround() || !StartPair())
	{
		ClearPair();
		RestoreHeadquarters();
		return false;
	}
	Report->SetStringField(TEXT("status"), TEXT("running"));
	return true;
}

bool FSimulationDuelRunner::FindWallets(ACommandGameState& InState)
{
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
	return true;
}

bool FSimulationDuelRunner::CollectDefinitions(ACommandGameState& InState, TArray<TSharedPtr<FJsonValue>>& Rows)
{
	TSet<FName> Ids;
	for (int32 Index = 0; Index < InState.Content->Units.Num(); ++Index)
	{
		const UArmyUnitDefinition* Definition = InState.Content->Unit(Index);
		if (!IsValid(Definition))
			continue;
		if (!ValidDefinition(*Definition, Ids))
		{
			Error = TEXT("Combat catalogue contains duplicate identities or invalid duel definitions");
			return false;
		}
		Ids.Add(Definition->Id);
		Definitions.Add(Index);
		Rows.Add(DefinitionRow(*Definition));
	}
	if (Definitions.IsEmpty())
	{
		Error = TEXT("Combat catalogue is empty");
		return false;
	}
	return true;
}

void FSimulationDuelRunner::IsolateWorld(ACommandGameState& InState)
{
	UWorld* World = InState.GetWorld();
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
			|| !IsOpenGround(*State, *Navigation, *NavData, Ground))
			continue;
		Center = Ground.Location;
		RecordGeometry();
		return true;
	}
	Error = TEXT("Map has no verified obstacle-free duel area");
	return false;
}

void FSimulationDuelRunner::RecordGeometry()
{
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
}

bool FSimulationDuelRunner::IssueAttackOrders()
{
	const AMapRegion* TargetRegion = State->FindRegionAt(Center);
	for (const TWeakObjectPtr<AArmyGroup>& Group : Groups)
		if (!TargetRegion
			|| !FCommandService::SetRetreatThreshold(Group->GetOwningPlayerState(), Group.Get(), ERetreatThreshold::Never)
			|| !FCommandService::IssueForceOrder(Group->GetOwningPlayerState(), Group.Get(), EForceVerb::Attack, TargetRegion->RegionIndex))
		{
			Error = TEXT("Duel group rejected its real Attack order");
			return false;
		}
	return true;
}

void FSimulationDuelRunner::PublishPair(TArray<TSharedPtr<FJsonValue>> Spawns[2])
{
	Current->SetArrayField(TEXT("spent"), Numbers(Spent[0], Spent[1]));
	Current->SetArrayField(TEXT("initial_units"), Numbers(Initial[0], Initial[1]));
	Current->SetArrayField(TEXT("survivors"), Numbers(Survivors[0], Survivors[1]));
	Current->SetArrayField(TEXT("survivor_power"), Numbers(Spent[0], Spent[1]));
	Current->SetArrayField(TEXT("damage_dealt"), Numbers(0, 0));
	Current->SetArrayField(TEXT("shield_damage_dealt"), Numbers(0, 0));
	Current->SetArrayField(TEXT("attacks"), Numbers(0, 0));
	Current->SetArrayField(TEXT("spawn_positions_left"), MoveTemp(Spawns[0]));
	Current->SetArrayField(TEXT("spawn_positions_right"), MoveTemp(Spawns[1]));
	Current->SetField(TEXT("duration"), MakeShared<FDuelNumber>(0.));
	Current->SetStringField(TEXT("outcome"), TEXT("running"));
	Current->SetField(TEXT("winner"), MakeShared<FJsonValueNull>());
	Report->SetObjectField(TEXT("current_duel"), Current.ToSharedRef());
}
#endif
