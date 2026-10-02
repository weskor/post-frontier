#include "ArmyGroup.h"

#include "AIController.h"
#include "ArenaBounds.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CombatTarget.h"
#include "CommandGameState.h"
#include "Content/MatchContent.h"
#include "Headquarters.h"
#include "CommandPlayerState.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Rules/TargetingPolicy.h"
#include "Rules/PursuitPolicy.h"

DEFINE_LOG_CATEGORY_STATIC(LogArmyOrders, Log, All);

namespace
{
constexpr int32 InitialUnitCount = 6;
constexpr int32 MaxUnitCount = InitialUnitCount;

struct FPreparedMove
{
	AAIController* Controller = nullptr;
	FVector Goal = FVector::ZeroVector;
	FNavPathSharedPtr Path;
};

AAIController* GetReadyController(AArmyUnit* Unit)
{
	AAIController* AI = IsValid(Unit) ? Cast<AAIController>(Unit->GetController()) : nullptr;
	UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	return Following && !Following->IsResourceLocked() && Following->IsPathFollowingAllowed() ? AI : nullptr;
}

bool PrepareMove(UNavigationSystemV1& Navigation, const FNavAgentProperties& Agent, UObject* Querier,
	UPathFollowingComponent& Following, const FVector& Start, const FVector& Target,
	FPreparedMove& Prepared, float ProjectionRadius = 35.0f)
{
	if (!AArenaBounds::IsTravelLocation(Navigation.GetWorld(), Target))
	{
		return false;
	}
	const ANavigationData* NavData = Navigation.GetNavDataForProps(Agent, Start);
	FNavLocation Projected;
	if (!NavData || !Navigation.ProjectPointToNavigation(Target, Projected, FVector(ProjectionRadius, ProjectionRadius, 200.0f), NavData)
		|| !AArenaBounds::IsTravelLocation(Navigation.GetWorld(), Projected.Location)
		|| FVector::DistSquared2D(Target, Projected.Location) > FMath::Square(ProjectionRadius))
	{
		return false;
	}
	FPathFindingQuery Query(Querier, *NavData, Start, Projected.Location);
	Query.SetAllowPartialPaths(false);
	// Match the engine's normal query setup, including Detour crowd's
	// corridor-preserving flags, before validating the synchronous path.
	Following.OnPathfindingQuery(Query);
	const FPathFindingResult Result = Navigation.FindPathSync(Agent, Query);
	if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
	{
		return false;
	}
	Prepared.Goal = Projected.Location;
	Prepared.Path = Result.Path;
	return true;
}

bool StartPreparedMove(const FPreparedMove& Move)
{
	FAIMoveRequest Request(Move.Goal);
	Request.SetAcceptanceRadius(35.0f);
	Request.SetReachTestIncludesAgentRadius(false);
	Request.SetAllowPartialPath(false);
	return Move.Controller->RequestMove(Request, Move.Path).IsValid();
}

void DestroyUnit(AArmyUnit* Unit)
{
	if (IsValid(Unit))
	{
		if (AController* Controller = Unit->GetController())
		{
			Controller->Destroy();
		}
		Unit->Destroy();
	}
}
}

AArmyGroup::AArmyGroup()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	bAlwaysRelevant = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AArmyGroup::Initialize(const FArmyGroupSpawn& Spawn)
{
	TeamIndex = Spawn.TeamIndex;
	bOpposingArmy = TeamIndex == 5;
	OwningPlayerState = Spawn.OwningPlayerState;
	ArmyIndex = Spawn.ArmyIndex;
	ProductionBuilding = Spawn.ProductionBuilding;
	ForceNumber = IsValid(ProductionBuilding) ? ProductionBuilding->ForceNumber : 0;
	HomeLocation = Spawn.HomeLocation;
}

void AArmyGroup::OnMemberDied(AArmyUnit* Unit)
{
	Units.Remove(Unit);
	ForceNetUpdate();
}

void AArmyGroup::DetachProducer()
{
	ProductionBuilding = nullptr;
}

void AArmyGroup::RollbackLastReinforcement()
{
	AArmyUnit* Candidate = Units.Pop(EAllowShrinking::No);
	DestroyUnit(Candidate);
	ForceNetUpdate();
}

void AArmyGroup::SetAssemblyLocation(const FVector& Location)
{
	HomeLocation = Location;
}

EArmyDoctrine AArmyGroup::GetDoctrine() const
{
	return IsValid(OwningPlayerState) ? OwningPlayerState->Doctrine : EArmyDoctrine::None;
}

FVector AArmyGroup::FormationOffset(int32 Index) const
{
	if (bProducedGroup)
		return FVector((ForceCapacity / 2 - 1 - 2 * (Index / 2)) * 55.f,
			(Index % 2 ? 1.f : -1.f) * 55.f, 0.f);
	return FVector((bOpposingArmy ? -1.f : 1.f) * (1 - Index / 2) * 220.f,
		(Index % 2 ? 1.f : -1.f) * 140.f, 0.f);
}

bool AArmyGroup::SpawnUnits()
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const UMatchContent* Content = State ? State->Content.Get() : nullptr;
	if (!HasAuthority() || !Units.IsEmpty() || !Content
		|| !IsValid(OwningPlayerState) || OwningPlayerState->GetWorld() != GetWorld() || OwningPlayerState->TeamIndex != TeamIndex
		|| (TeamIndex == 0 ? OwningPlayerState->CommanderIndex < 0 || OwningPlayerState->CommanderIndex >= 5
						   : TeamIndex != 5 || !bOpposingArmy || OwningPlayerState != State->EnemyCommander))
	{
		return false;
	}
	// Fixed starting force: two of each of the first three catalogue units.
	for (int32 Index = 0; Index < InitialUnitCount; ++Index)
		if (!Content->Unit(Index / 2))
			return false;

	Units.Reserve(MaxUnitCount);
	for (int32 Index = 0; Index < InitialUnitCount; ++Index)
	{
		const FVector Offset = FormationOffset(Index);
		const FTransform Transform(FRotator::ZeroRotator, HomeLocation + Offset);
		AArmyUnit* Unit = GetWorld()->SpawnActorDeferred<AArmyUnit>(AArmyUnit::StaticClass(), Transform,
			this, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Unit)
		{
			return false;
		}
		Unit->Initialize(this, TeamIndex, IsValid(OwningPlayerState) ? OwningPlayerState->CommanderIndex : -1,
			ArmyIndex, Index, Index / 2, const_cast<UArmyUnitDefinition*>(Content->Unit(Index / 2)));
		Unit->FinishSpawning(Transform);
		Units.Add(Unit);
	}
	Destination = GetCenter();
	ForceNetUpdate();
	return true;
}

AArmyUnit* AArmyGroup::SpawnMember(int32 UnitIndex, const FVector& SpawnLocation, int32 CompositionSlot)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const UArmyUnitDefinition* Definition = State && State->Content ? State->Content->Unit(UnitIndex) : nullptr;
	if (!HasAuthority() || IsActorBeingDestroyed() || !State || State->MatchResult != EMatchResult::Ongoing
		|| !Definition || IsValid(ProductionBuilding) || CompositionSlot < 0
		|| !IsValid(OwningPlayerState) || OwningPlayerState->GetWorld() != GetWorld()
		|| OwningPlayerState->TeamIndex != TeamIndex
		|| (TeamIndex == 0 ? OwningPlayerState->CommanderIndex < 0 || OwningPlayerState->CommanderIndex >= 5
						   : TeamIndex != 5 || OwningPlayerState != State->EnemyCommander)
		|| !AArenaBounds::IsTravelLocation(GetWorld(), SpawnLocation))
		return nullptr;
	for (const AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive() && Unit->GetCompositionSlot() == CompositionSlot)
			return nullptr;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Ground;
	if (!Navigation || !Navigation->ProjectPointToNavigation(SpawnLocation, Ground, FVector(10.f, 10.f, 200.f))
		|| FVector::DistSquared2D(SpawnLocation, Ground.Location) > FMath::Square(10.f)
		|| !AArenaBounds::IsTravelLocation(GetWorld(), Ground.Location))
		return nullptr;
	const FTransform Transform(Ground.Location + FVector(0.f, 0.f, 65.f));
	AArmyUnit* Unit = GetWorld()->SpawnActorDeferred<AArmyUnit>(AArmyUnit::StaticClass(), Transform,
		this, nullptr, ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
	if (!Unit)
		return nullptr;
	Unit->Initialize(this, TeamIndex, OwningPlayerState->CommanderIndex, ArmyIndex,
		CompositionSlot, UnitIndex, const_cast<UArmyUnitDefinition*>(Definition), false);
	Unit->FinishSpawning(Transform);
	if (!IsValid(Unit) || !GetReadyController(Unit))
	{
		DestroyUnit(Unit);
		return nullptr;
	}
	Units.Add(Unit);
	ForceNetUpdate();
	return Unit;
}
bool AArmyGroup::SpawnReinforcement(int32 UnitIndex, const FVector& SpawnLocation)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const UArmyUnitDefinition* Definition = State && State->Content ? State->Content->Unit(UnitIndex) : nullptr;
	const int32 Capacity = Definition ? ACommandBuilding::GetForceCapacity(*Definition) : 0;
	if (!HasAuthority() || IsActorBeingDestroyed() || !State || State->MatchResult != EMatchResult::Ongoing
		|| !IsValid(ProductionBuilding) || !ProductionBuilding->IsAlive() || !ProductionBuilding->IsComplete()
		|| !ProductionBuilding->IsProducer() || !ProductionBuilding->bForceConfigured
		|| ProductionBuilding->ForceGroup != this || ProductionBuilding->ProductionUnitIndex != UnitIndex
		|| ProductionBuilding->TeamIndex != TeamIndex || ProductionBuilding->OwningPlayerState != OwningPlayerState
		|| !IsValid(OwningPlayerState) || OwningPlayerState->GetWorld() != GetWorld() || OwningPlayerState->TeamIndex != TeamIndex
		|| (TeamIndex == 0 ? OwningPlayerState->CommanderIndex < 0 || OwningPlayerState->CommanderIndex >= 5
						   : TeamIndex != 5 || OwningPlayerState != State->EnemyCommander)
		|| Capacity == 0 || !AArenaBounds::IsTravelLocation(GetWorld(), SpawnLocation))
		return false;
	uint32 Occupied = 0;
	int32 Living = 0;
	for (const AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive())
			continue;
		if (Unit->UnitIndex != UnitIndex || Unit->CompositionSlot < 0 || Unit->CompositionSlot >= Capacity)
			return false;
		Occupied |= 1u << Unit->CompositionSlot;
		++Living;
	}
	if (Living >= Capacity)
		return false;
	int32 Slot = 0;
	while (Slot < Capacity && (Occupied & (1u << Slot)))
		++Slot;
	if (Slot == Capacity)
		return false;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const double ExitDistance = FVector::DistSquared2D(SpawnLocation, ProductionBuilding->GetActorLocation());
	const float Radius = ACommandBuilding::GetFootprintRadius(*ProductionBuilding->GetDefinition());
	FNavLocation Projected;
	if (!Navigation || ExitDistance < FMath::Square(Radius + 75.f)
		|| ExitDistance > FMath::Square(Radius + 700.f)
		|| !Navigation->ProjectPointToNavigation(SpawnLocation, Projected, FVector(45.f, 45.f, 200.f))
		|| !AArenaBounds::IsTravelLocation(GetWorld(), Projected.Location)
		|| FVector::DistSquared2D(SpawnLocation, Projected.Location) > FMath::Square(45.f)
		|| FMath::Abs(SpawnLocation.Z - Projected.Location.Z) > 110.f
		|| GetWorld()->OverlapBlockingTestByChannel(Projected.Location + FVector(0.f, 0.f, 85.f),
			FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(34.f, 60.f)))
		return false;
	const FTransform Transform(FRotator::ZeroRotator, Projected.Location + FVector(0.f, 0.f, 85.f));
	AArmyUnit* Candidate = GetWorld()->SpawnActorDeferred<AArmyUnit>(AArmyUnit::StaticClass(), Transform,
		this, nullptr, ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
	if (!Candidate)
		return false;
	Candidate->Initialize(this, TeamIndex, IsValid(OwningPlayerState) ? OwningPlayerState->CommanderIndex : -1,
		ArmyIndex, Slot, UnitIndex, const_cast<UArmyUnitDefinition*>(Definition), true);
	Candidate->FinishSpawning(Transform);
	AAIController* AI = GetReadyController(Candidate);
	const bool bWasProduced = bProducedGroup;
	const int32 OldCapacity = ForceCapacity;
	bProducedGroup = true;
	ForceCapacity = Capacity;
	const FVector Rendezvous = AI ? ReinforcementTarget(*Candidate) : FVector::ZeroVector;
	FPreparedMove Move;
	Move.Controller = AI;
	if (!IsValid(Candidate) || !AI
		|| FVector::DistSquared2D(Candidate->GetActorLocation(), Transform.GetLocation()) > FMath::Square(40.f)
		|| !PrepareMove(*Navigation, Candidate->GetNavAgentPropertiesRef(), AI, *AI->GetPathFollowingComponent(),
			Candidate->GetNavAgentLocation(), Rendezvous, Move, 75.f)
		|| !StartPreparedMove(Move))
	{
		DestroyUnit(Candidate);
		bProducedGroup = bWasProduced;
		ForceCapacity = OldCapacity;
		return false;
	}
	Candidate->ReinforcementGoal = Move.Goal;
	Candidate->ReinforcementRendezvous = Rendezvous;
	Candidate->bHasReinforcementPath = true;
	Units.RemoveAll([](const TObjectPtr<AArmyUnit>& Unit) { return !IsValid(Unit) || !Unit->IsAlive(); });
	Units.Reserve(Capacity);
	Units.Add(Candidate);
	ForceNetUpdate();
	return true;
}

FVector AArmyGroup::GetCenter() const
{
	FVector Center = FVector::ZeroVector;
	int32 Count = 0;
	for (const AArmyUnit* Unit : Units)
	{
		if (IsValid(Unit) && Unit->IsAlive() && !Unit->bReinforcing)
		{
			Center += Unit->GetActorLocation();
			++Count;
		}
	}
	return Count > 0 ? Center / Count : bAutomaticFront ? FrontLocation
														: GetActorLocation();
}

FVector AArmyGroup::ReinforcementTarget(const AArmyUnit& Unit) const
{
	// A depleted formation's member center is biased toward its occupied slots.
	// Recover its moving anchor so an empty slot does not target an existing member.
	FVector Anchor = FVector::ZeroVector;
	int32 Joined = 0;
	for (const AArmyUnit* Member : Units)
	{
		if (!IsValid(Member) || !Member->IsAlive() || Member->bReinforcing)
			continue;
		Anchor += Member->GetActorLocation() - FormationOffset(Member->CompositionSlot);
		++Joined;
	}
	if (Joined > 0)
		Anchor /= Joined;
	else
		Anchor = bAutomaticFront ? FrontLocation : GetActorLocation();
	return Anchor + FormationOffset(Unit.CompositionSlot);
}

void AArmyGroup::UpdateReinforcements()
{
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Navigation)
		return;
	for (AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive() || !Unit->bReinforcing)
			continue;
		AAIController* AI = GetReadyController(Unit);
		if (!AI)
			continue;
		Unit->Target = nullptr;
		Unit->bPursuing = false;
		const FVector Goal = ReinforcementTarget(*Unit);
		const bool bRetarget = !Unit->bHasReinforcementPath
			|| FVector::DistSquared2D(Goal, Unit->ReinforcementRendezvous) > FMath::Square(55.f)
			|| AI->GetMoveStatus() == EPathFollowingStatus::Idle;
		if (bRetarget)
		{
			FPreparedMove Move;
			Move.Controller = AI;
			if (!PrepareMove(*Navigation, Unit->GetNavAgentPropertiesRef(), AI, *AI->GetPathFollowingComponent(),
					Unit->GetNavAgentLocation(), Goal, Move, 75.f))
			{
				AI->StopMovement();
				Unit->GetCharacterMovement()->StopMovementImmediately();
				Unit->bHasReinforcementPath = false;
				continue;
			}
			Unit->ReinforcementGoal = Move.Goal;
			Unit->ReinforcementRendezvous = Goal;
			Unit->bHasReinforcementPath = true;
			if (FVector::DistSquared2D(Unit->GetNavAgentLocation(), Move.Goal) > FMath::Square(35.f)
				&& !StartPreparedMove(Move))
			{
				AI->StopMovement();
				Unit->bHasReinforcementPath = false;
				continue;
			}
		}
		const double DistanceToGoalSquared = FVector::DistSquared2D(Unit->GetNavAgentLocation(), Unit->ReinforcementGoal);
		UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(AI->GetPathFollowingComponent());
		if (Crowd)
		{
			// Predictive avoidance can stop before a vacant slot because it predicts
			// continuing through the goal into members beyond it. Use a precise final
			// approach; the validated corridor and physical capsule collisions remain.
			const float PrecisionRadius = 2.f * Unit->GetSimpleCollisionRadius() + 35.f;
			Crowd->SetCrowdObstacleAvoidance(DistanceToGoalSquared > FMath::Square(PrecisionRadius), true);
		}
		if (!Unit->bHasReinforcementPath
			|| DistanceToGoalSquared > FMath::Square(35.f)
			|| FVector::DistSquared2D(Goal, Unit->ReinforcementRendezvous) > FMath::Square(55.f)
			|| FMath::Abs(Unit->GetNavAgentLocation().Z - Unit->ReinforcementGoal.Z) > 110.f)
			continue;
		// Arrival is physical and follows a complete accepted route. Adopt current
		// intent, not the front/order that happened to exist when this recruit spawned.
		if (Crowd)
			Crowd->SetCrowdObstacleAvoidance(true, true);
		FPreparedMove Formation;
		Formation.Controller = AI;
		if (Order != EArmyOrder::Hold)
		{
			if (!PrepareMove(*Navigation, Unit->GetNavAgentPropertiesRef(), AI, *AI->GetPathFollowingComponent(),
					Unit->GetNavAgentLocation(), Destination + FormationOffset(Unit->CompositionSlot), Formation)
				|| !StartPreparedMove(Formation))
				continue;
		}
		else
			AI->StopMovement();
		Unit->bReinforcing = false;
		Unit->bHasReinforcementPath = false;
		Unit->ForceNetUpdate();
		ForceNetUpdate();
	}
}

void AArmyGroup::StopAllUnits()
{
	// Abort every old request before submitting any replacement. There are no
	// completion delegates or delayed regroup orders that can resurrect intent.
	for (AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit))
		{
			continue;
		}
		if (AAIController* AI = Cast<AAIController>(Unit->GetController()))
		{
			AI->StopMovement();
			AI->ClearFocus(EAIFocusPriority::Gameplay);
		}
		Unit->GetCharacterMovement()->StopMovementImmediately();
		Unit->Target = nullptr;
		Unit->bPursuing = false;
		Unit->bHasReinforcementPath = false;
	}
}
bool AArmyGroup::AssignFront(EFrontOrder InOrder, const FVector& InLocation)
{
	if (InOrder != EFrontOrder::Secure && InOrder != EFrontOrder::Defend
		&& InOrder != EFrontOrder::FallBack)
		return false;
	const EArmyOrder Travel = InOrder == EFrontOrder::FallBack ? EArmyOrder::Retreat : EArmyOrder::Attack;
	if (!IssueTravel(Travel, InLocation))
		return false;
	FrontOrder = InOrder;
	FrontLocation = Destination;
	bAutomaticFront = true;
	FrontMaintenanceSeconds = 0.f;
	ForceNetUpdate();
	return true;
}

bool AArmyGroup::ApplyHold()
{
	StopAllUnits();
	AttackTarget = nullptr;
	Order = EArmyOrder::Hold;
	Destination = GetCenter();
	bAutomaticFront = false;
	++OrderSerial;
	ForceNetUpdate();
	LogOrder();
	return true;
}

bool AArmyGroup::ApplyAttack(FVector InDestination, AActor* InTarget)
{
	FVector Anchor = InDestination;
	if (InTarget)
	{
		// Targeted attacks approach a firing position instead of ordering the
		// formation into the occupied target.
		FVector FromTarget = GetCenter() - InTarget->GetActorLocation();
		FromTarget.Z = 0.f;
		if (!FromTarget.Normalize())
			FromTarget = FVector(-1.f, 0.f, 0.f);
		Anchor = InTarget->GetActorLocation() + FromTarget * 650.f;
		Anchor.Z = 0.f;
	}
	if (!IssueTravel(EArmyOrder::Attack, Anchor))
		return false;
	AttackTarget = InTarget;
	ForceNetUpdate();
	return true;
}

bool AArmyGroup::IssueTravel(EArmyOrder NewOrder, const FVector& InDestination)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || (Units.IsEmpty() && !IsValid(ProductionBuilding) && !bProducedGroup)
		|| !AArenaBounds::IsTravelLocation(GetWorld(), InDestination) || (State && State->MatchResult != EMatchResult::Ongoing))
	{
		return false;
	}

	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Navigation)
	{
		return false;
	}

	TArray<FPreparedMove, TInlineAllocator<MaxUnitCount>> Prepared;
	FVector ProjectedCenter = FVector::ZeroVector;

	// Validate the entire command without touching the current order or paths.
	// In particular, partial paths never count as accepting the user's target.
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		AArmyUnit* Unit = Units[Index];
		if (!IsValid(Unit) || !Unit->IsAlive() || Unit->bReinforcing)
			continue;
		AAIController* AI = GetReadyController(Unit);
		if (!AI)
		{
			return false;
		}
		const FNavAgentProperties& Agent = Unit->GetNavAgentPropertiesRef();
		const FVector Start = Unit->GetNavAgentLocation();

		if (Prepared.IsEmpty())
		{
			FPreparedMove CenterMove;
			if (!PrepareMove(*Navigation, Agent, AI, *AI->GetPathFollowingComponent(),
					Start, InDestination, CenterMove, 75.0f))
			{
				return false;
			}
			ProjectedCenter = CenterMove.Goal;
		}

		FPreparedMove Move;
		Move.Controller = AI;
		// Limit horizontal projection so a wall cannot collapse several slots
		// onto the same edge, or pull an out-of-arena slot back into bounds.
		if (!PrepareMove(*Navigation, Agent, AI, *AI->GetPathFollowingComponent(),
				Start, ProjectedCenter + FormationOffset(Unit->CompositionSlot), Move))
		{
			return false;
		}
		Prepared.Add(MoveTemp(Move));
	}
	if (Prepared.IsEmpty())
	{
		// A wiped/assembling force still owns its intent. Validate a complete route
		// from assembly rather than inventing a member or accepting a partial path.
		const FNavAgentProperties& Agent = GetDefault<AArmyUnit>()->GetNavAgentPropertiesRef();
		const ANavigationData* NavData = Navigation->GetNavDataForProps(Agent, HomeLocation);
		FNavLocation Projected;
		if (!NavData || !Navigation->ProjectPointToNavigation(InDestination, Projected, FVector(75.f, 75.f, 200.f), NavData) || !AArenaBounds::IsTravelLocation(GetWorld(), Projected.Location)
			|| FVector::DistSquared2D(InDestination, Projected.Location) > FMath::Square(75.f))
			return false;
		FPathFindingQuery Query(this, *NavData, HomeLocation, Projected.Location);
		Query.SetAllowPartialPaths(false);
		const FPathFindingResult Result = Navigation->FindPathSync(Agent, Query);
		if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
			return false;
		ProjectedCenter = Projected.Location;
	}

	StopAllUnits();
	for (const FPreparedMove& Move : Prepared)
	{
		// Submit the complete prevalidated path; no second query or delayed order.
		StartPreparedMove(Move);
	}
	Order = NewOrder;
	AttackTarget = nullptr;
	Destination = ProjectedCenter;
	bAutomaticFront = false;
	++OrderSerial;
	ForceNetUpdate();
	LogOrder();
	return true;
}

void AArmyGroup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || (CombatAccumulator += DeltaSeconds) < .25f)
		return;
	const float Elapsed = CombatAccumulator;
	CombatAccumulator = 0.f;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	Units.RemoveAll([](const TObjectPtr<AArmyUnit>& Unit) { return !IsValid(Unit) || !Unit->IsAlive(); });
	if (Units.IsEmpty() && bProducedGroup && !IsValid(ProductionBuilding))
	{
		Destroy();
		return;
	}
	if (!State || State->MatchResult != EMatchResult::Ongoing)
		return;
	UpdateReinforcements();
	if (bAutomaticFront && !Units.IsEmpty() && (FrontMaintenanceSeconds += Elapsed) >= 2.f)
	{
		FrontMaintenanceSeconds = 0.f;
		const bool bDisplaced = FVector::DistSquared2D(GetCenter(), FrontLocation) > FMath::Square(170.f);
		bool bMovingOrEngaged = false;
		for (AArmyUnit* Unit : Units)
		{
			if (!IsValid(Unit) || !Unit->IsAlive() || Unit->bReinforcing)
				continue;
			const AAIController* AI = Cast<AAIController>(Unit->GetController());
			if (Unit->bPursuing || (AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle))
			{
				bMovingOrEngaged = true;
				break;
			}
		}
		if (!bMovingOrEngaged)
		{
			if (IsValid(ProductionBuilding) && ProductionBuilding->HasConfiguredFront()
				&& (FrontOrder != ProductionBuilding->FrontOrder
					|| !FrontLocation.Equals(ProductionBuilding->FrontLocation, 1.f)))
				AssignFront(ProductionBuilding->FrontOrder, ProductionBuilding->FrontLocation);
			else if (bDisplaced)
				AssignFront(FrontOrder, FrontLocation);
		}
	}
	UpdateCombat();
}

void AArmyGroup::UpdateCombat()
{
	if (Order == EArmyOrder::Retreat)
		return;
	if (Units.IsEmpty())
	{
		if (AttackTarget)
		{
			AttackTarget = nullptr;
			ForceNetUpdate();
		}
		return;
	}
	TArray<AArmyUnit*, TInlineAllocator<32>> Enemies;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
	{
		if (It->TeamIndex == TeamIndex)
			continue;
		for (AArmyUnit* Enemy : It->Units)
			if (IsValid(Enemy) && Enemy->IsAlive())
				Enemies.Add(Enemy);
	}
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	AHeadquarters* HostileHQ = State ? (TeamIndex == 5 ? State->FriendlyHeadquarters.Get() : State->EnemyHeadquarters.Get()) : nullptr;
	const TArray<TObjectPtr<ACommandBuilding>>* HostileBuildings = State ? &State->Buildings : nullptr;
	if (AttackTarget && (!CombatTarget::IsAliveHostile(AttackTarget.Get(), TeamIndex) || ((FrontOrder != EFrontOrder::Secure || !bAutomaticFront) && FVector::DistSquared2D(AttackTarget->GetActorLocation(), Destination) > FMath::Square(PursuitRadius))))
	{
		AttackTarget = nullptr;
		ForceNetUpdate();
	}

	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		AArmyUnit* Unit = Units[Index];
		if (!IsValid(Unit) || !Unit->IsAlive() || Unit->bReinforcing)
			continue;
		auto Permitted = [this, Unit](AActor* Enemy) {
			if (!CombatTarget::IsAliveHostile(Enemy, TeamIndex))
				return false;
			const float Distance = FVector::DistSquared2D(Unit->GetActorLocation(), Enemy->GetActorLocation());
			if (Order != EArmyOrder::Attack)
				return Distance <= FMath::Square(Unit->WeaponRange());
			const bool bNearAnchor = FVector::DistSquared2D(Enemy->GetActorLocation(), Destination)
					<= FMath::Square(PursuitRadius)
				&& FVector::DistSquared2D(Unit->GetActorLocation(), Destination) <= FMath::Square(PursuitRadius);
			const bool bEnRoute = bAutomaticFront && FrontOrder == EFrontOrder::Secure
				&& FVector::DistSquared2D(Unit->GetActorLocation(), Destination) > FMath::Square(PursuitRadius)
				&& Distance <= FMath::Square(800.f)
				&& FVector::Dist2D(Enemy->GetActorLocation(), Destination)
					<= FVector::Dist2D(Unit->GetActorLocation(), Destination) + 250.f;
			return (bNearAnchor && Distance <= FMath::Square(1450.f)) || bEnRoute;
		};
		AActor* Chosen = Permitted(AttackTarget.Get()) ? AttackTarget.Get()
			: Permitted(Unit->Target.Get())            ? Unit->Target.Get()
													   : nullptr;
		if (!Chosen)
		{
			FTargetSelection Selection;
			int32 CandidateIndex = 0;
			auto Consider = [&](AActor* Candidate) {
				if (!Permitted(Candidate))
					return;
				const int32 IndexInSelection = CandidateIndex++;
				Selection.Consider(Unit->GetDamageType(), IndexInSelection, CombatTarget::ArmorClass(Candidate),
					FVector::DistSquared2D(Unit->GetActorLocation(), Candidate->GetActorLocation()));
				if (Selection.Index == IndexInSelection)
					Chosen = Candidate;
			};
			for (AArmyUnit* Enemy : Enemies)
				Consider(Enemy);
			Consider(HostileHQ);
			if (HostileBuildings)
				for (ACommandBuilding* Building : *HostileBuildings)
					Consider(Building);
		}
		const bool bTargetChanged = Unit->Target != Chosen;
		if (Unit->Target != Chosen)
		{
			Unit->Target = Chosen;
			Unit->ForceNetUpdate();
		}
		AAIController* AI = Cast<AAIController>(Unit->GetController());
		if (!Chosen)
		{
			if (Unit->bPursuing && AI)
			{
				Unit->bPursuing = false;
				AI->MoveToLocation(Destination + FormationOffset(Unit->CompositionSlot), 35.f, false, true, false, false);
			}
			continue;
		}
		if (Order == EArmyOrder::Attack && AI)
		{
			const bool bEnRoute = bAutomaticFront && FrontOrder == EFrontOrder::Secure
				&& FVector::DistSquared2D(Unit->GetActorLocation(), Destination) > FMath::Square(PursuitRadius);
			const FVector PursuitAnchor = bEnRoute ? Unit->GetActorLocation() : Destination;
			const bool bActivePursuit = Unit->bPursuing && AI->GetMoveStatus() != EPathFollowingStatus::Idle;
			const FPursuitDecision Decision = PursuitPolicy::Evaluate(Unit->GetActorLocation(),
				Chosen->GetActorLocation(), Unit->WeaponRange(), PursuitAnchor, PursuitRadius,
				Destination.Z, bActivePursuit, bTargetChanged, Unit->PursuitGoal);
			if (bTargetChanged || Decision.bInRange || !bActivePursuit)
				Unit->bPursuing = false;
			if (Decision.bInRange)
			{
				if (AI->GetMoveStatus() != EPathFollowingStatus::Idle)
				{
					AI->StopMovement();
					Unit->GetCharacterMovement()->StopMovementImmediately();
				}
			}
			else if (Decision.bIssueMove)
			{
				const FVector& Goal = Decision.Goal;
				UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
				FPreparedMove Pursuit;
				Pursuit.Controller = AI;
				if (Navigation && PrepareMove(*Navigation, Unit->GetNavAgentPropertiesRef(), AI, *AI->GetPathFollowingComponent(), Unit->GetNavAgentLocation(), Goal, Pursuit, 75.f))
				{
					bool bWithinBounds = true;
					for (const FNavPathPoint& Point : Pursuit.Path->GetPathPoints())
					{
						if (FVector::DistSquared2D(Point.Location, PursuitAnchor) > FMath::Square(PursuitRadius))
						{
							bWithinBounds = false;
							break;
						}
					}
					if (bWithinBounds && StartPreparedMove(Pursuit))
					{
						Unit->bPursuing = true;
						Unit->PursuitGoal = Pursuit.Goal;
					}
				}
			}
		}
		Unit->FireAt(Chosen);
	}
}

void AArmyGroup::SettleMatch()
{
	if (!HasAuthority())
		return;
	StopAllUnits();
	AttackTarget = nullptr;
	Order = EArmyOrder::Hold;
	Destination = GetCenter();
	bAutomaticFront = false;
	++OrderSerial;
	ForceNetUpdate();
}

void AArmyGroup::LogOrder() const
{
	UE_LOG(LogArmyOrders, Display, TEXT("%s accepted %s serial=%u center=%s destination=%s units=%d"),
		*GetName(), *StaticEnum<EArmyOrder>()->GetNameStringByValue(static_cast<int64>(Order)),
		OrderSerial, *GetCenter().ToCompactString(), *Destination.ToCompactString(), Units.Num());
}

void AArmyGroup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		StopAllUnits();
		for (AArmyUnit* Unit : Units)
		{
			DestroyUnit(Unit);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AArmyGroup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArmyGroup, Order);
	DOREPLIFETIME(AArmyGroup, Destination);
	DOREPLIFETIME(AArmyGroup, OrderSerial);
	DOREPLIFETIME(AArmyGroup, Units);
	DOREPLIFETIME(AArmyGroup, HomeLocation);
	DOREPLIFETIME(AArmyGroup, TeamIndex);
	DOREPLIFETIME(AArmyGroup, OwningPlayerState);
	DOREPLIFETIME(AArmyGroup, ArmyIndex);
	DOREPLIFETIME(AArmyGroup, ForceNumber);
	DOREPLIFETIME(AArmyGroup, AttackTarget);
	DOREPLIFETIME(AArmyGroup, FrontOrder);
	DOREPLIFETIME(AArmyGroup, FrontLocation);
	DOREPLIFETIME(AArmyGroup, bAutomaticFront);
	DOREPLIFETIME(AArmyGroup, ProductionBuilding);
}
