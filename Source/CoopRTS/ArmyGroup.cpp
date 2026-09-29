#include "ArmyGroup.h"

#include "AIController.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CombatTarget.h"
#include "CommandGameState.h"
#include "Headquarters.h"
#include "CommandPlayerState.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogArmyOrders, Log, All);

namespace
{
	constexpr int32 InitialUnitCount = 6;
	constexpr int32 MaxUnitCount = InitialUnitCount;
	constexpr float ReinforcementSourceRadius = 450.f;
	constexpr int32 RoleCosts[] = {40, 40, 60, 60, 80, 80};

	struct FPreparedMove
	{
		AAIController* Controller = nullptr;
		FVector Goal = FVector::ZeroVector;
		FNavPathSharedPtr Path;
	};

	bool IsArenaLocation(const FVector& Location)
	{
		return !Location.ContainsNaN() && FMath::Abs(Location.X) <= 4500.0
			&& FMath::Abs(Location.Y) <= 4500.0 && FMath::Abs(Location.Z) <= 1000.0;
	}

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
		if (!IsArenaLocation(Target))
		{
			return false;
		}
		const ANavigationData* NavData = Navigation.GetNavDataForProps(Agent, Start);
		FNavLocation Projected;
		if (!NavData || !Navigation.ProjectPointToNavigation(Target, Projected,
			FVector(ProjectionRadius, ProjectionRadius, 200.0f), NavData)
			|| !IsArenaLocation(Projected.Location)
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
	static ConstructorHelpers::FObjectFinder<UArmyUnitDefinition> Frontline(TEXT("/Game/Units/DA_Frontline.DA_Frontline"));
	static ConstructorHelpers::FObjectFinder<UArmyUnitDefinition> Ranged(TEXT("/Game/Units/DA_Ranged.DA_Ranged"));
	static ConstructorHelpers::FObjectFinder<UArmyUnitDefinition> Siege(TEXT("/Game/Units/DA_Siege.DA_Siege"));
	FrontlineDefinition = Frontline.Object;
	RangedDefinition = Ranged.Object;
	SiegeDefinition = Siege.Object;
}

EArmyDoctrine AArmyGroup::GetDoctrine() const
{
	return IsValid(OwningPlayerState) ? OwningPlayerState->Doctrine : EArmyDoctrine::None;
}

FVector AArmyGroup::FormationOffset(int32 Index) const
{
	return FVector((bOpposingArmy ? -1.f : 1.f) * (1 - Index / 2) * 220.f,
		(Index % 2 ? 1.f : -1.f) * 140.f, 0.f);
}

bool AArmyGroup::SpawnUnits()
{
	if (!HasAuthority() || !Units.IsEmpty() || !FrontlineDefinition || !RangedDefinition || !SiegeDefinition
		|| (TeamIndex == 0 ? !IsValid(OwningPlayerState) || OwningPlayerState->CommanderIndex < 0
			|| OwningPlayerState->CommanderIndex >= 5
			: TeamIndex != 5 || !bOpposingArmy || IsValid(OwningPlayerState)))
	{
		return false;
	}

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
		Unit->Group = this;
		Unit->TeamIndex = TeamIndex;
		Unit->CommanderIndex = IsValid(OwningPlayerState) ? OwningPlayerState->CommanderIndex : -1;
		Unit->CompositionSlot = Index;
		Unit->ArmyIndex = ArmyIndex;
		Unit->Definition = Index < 2 ? FrontlineDefinition : Index < 4 ? RangedDefinition : SiegeDefinition;
		Unit->UnitRole = Unit->Definition->Role;
		Unit->Health = Unit->MaxHealth();
		Unit->FinishSpawning(Transform);
		Units.Add(Unit);
	}
	Destination = GetCenter();
	ForceNetUpdate();
	return true;
}

int32 AArmyGroup::GetReinforcementCost() const
{
	bool Occupied[InitialUnitCount] = {};
	for (const AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive() && Unit->CompositionSlot >= 0 && Unit->CompositionSlot < InitialUnitCount)
			Occupied[Unit->CompositionSlot] = true;
	int32 Cost = 0;
	for (int32 Slot = 0; Slot < InitialUnitCount; ++Slot)
		if (!Occupied[Slot]) Cost += RoleCosts[Slot];
	return Cost;
}

bool AArmyGroup::GetReinforcementSource(FVector& OutLocation, bool& bBase) const
{
	OutLocation = HomeLocation;
	bBase = true;
	const FVector Center = Units.IsEmpty() ? HomeLocation : GetCenter();
	if (FVector::DistSquared2D(Center, HomeLocation) <= FMath::Square(ReinforcementSourceRadius))
		return true;
	if (Units.IsEmpty()) return false;
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!State) return false;
	for (const ACapturePoint* Site : State->CaptureSites)
	{
		if (IsValid(Site) && Site->SiteKind == ECaptureSiteKind::Reinforcement
			&& Site->ControllingTeam == TeamIndex
			&& FVector::DistSquared2D(Center, Site->GetActorLocation()) <= FMath::Square(ReinforcementSourceRadius))
		{
			OutLocation = Site->GetActorLocation() + FVector(0.f, 0.f, HomeLocation.Z - Site->GetActorLocation().Z);
			bBase = false;
			return true;
		}
	}
	return false;
}

bool AArmyGroup::CanReinforceAtCurrentLocation() const
{
	FVector Source;
	bool bBase = false;
	return GetReinforcementSource(Source, bBase);
}

FString AArmyGroup::GetReinforcementStatus() const
{
	if (TeamIndex != 0 && TeamIndex != 5) return TEXT("INVALID ARMY");
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!State || State->MatchResult != EMatchResult::Ongoing) return TEXT("MATCH FINISHED");
	if (GetReinforcementCost() == 0) return TEXT("FULL COMPOSITION");
	FVector Source;
	bool bBase = false;
	if (!GetReinforcementSource(Source, bBase))
		return Units.IsEmpty() ? TEXT("EMPTY ARMY: REBUILD AT BASE") : TEXT("RETURN TO BASE OR CONTROLLED FORWARD SITE");
	const ACommandPlayerState* Wallet = OwningPlayerState;
	if (TeamIndex == 0 && (!IsValid(Wallet) || Wallet->CommanderIndex < 0)) return TEXT("WALLET UNAVAILABLE");
	if ((TeamIndex == 5 ? State->EnemyResources : Wallet->Resources) < GetReinforcementCost())
		return TEXT("INSUFFICIENT RESOURCES");
	return TEXT("READY");
}

bool AArmyGroup::TryReinforce()
{
	if (!HasAuthority() || !FrontlineDefinition || !RangedDefinition
		|| !SiegeDefinition || GetReinforcementStatus() != TEXT("READY")) return false;
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	ACommandPlayerState* Wallet = OwningPlayerState;
	FVector Source;
	bool bBase = false;
	if ((!Wallet && TeamIndex == 0) || !State || !GetReinforcementSource(Source, bBase)) return false;
	const bool bRebuild = Units.IsEmpty();
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Navigation) return false;

	bool Occupied[InitialUnitCount] = {};
	for (const AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive() || Unit->CompositionSlot < 0
			|| Unit->CompositionSlot >= InitialUnitCount || Occupied[Unit->CompositionSlot]) return false;
		Occupied[Unit->CompositionSlot] = true;
	}
	TArray<AArmyUnit*, TInlineAllocator<InitialUnitCount>> Candidates;
	TArray<FPreparedMove, TInlineAllocator<InitialUnitCount>> Moves;
	auto Rollback = [&Candidates]()
	{
		for (AArmyUnit* Unit : Candidates) DestroyUnit(Unit);
	};
	for (int32 Slot = 0; Slot < InitialUnitCount; ++Slot)
	{
		if (Occupied[Slot]) continue;
		const FVector SpawnLocation = Source + FormationOffset(Slot);
		if (!IsArenaLocation(SpawnLocation)) { Rollback(); return false; }
		const FTransform Transform(FRotator::ZeroRotator, SpawnLocation);
		AArmyUnit* Candidate = GetWorld()->SpawnActorDeferred<AArmyUnit>(AArmyUnit::StaticClass(), Transform,
			this, nullptr, ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
		if (!Candidate) { Rollback(); return false; }
		Candidate->Group = this;
		Candidate->TeamIndex = TeamIndex;
		Candidate->ArmyIndex = ArmyIndex;
		Candidate->CommanderIndex = IsValid(OwningPlayerState) ? OwningPlayerState->CommanderIndex : -1;
		Candidate->CompositionSlot = Slot;
		Candidate->Definition = Slot < 2 ? FrontlineDefinition : Slot < 4 ? RangedDefinition : SiegeDefinition;
		Candidate->UnitRole = Candidate->Definition->Role;
		Candidate->Health = Candidate->MaxHealth();
		Candidate->FinishSpawning(Transform);
		if (!IsValid(Candidate)) { Rollback(); return false; }
		Candidates.Add(Candidate);
		const FVector Goal = (bRebuild ? HomeLocation : Destination) + FormationOffset(Slot);
		if (bRebuild || FVector::DistSquared2D(Candidate->GetNavAgentLocation(), Goal) <= FMath::Square(35.f))
		{
			FNavLocation Projected;
			if (!Navigation->ProjectPointToNavigation(Candidate->GetNavAgentLocation(), Projected,
				FVector(35.f, 35.f, 200.f))
				|| FVector::DistSquared2D(Projected.Location, Candidate->GetNavAgentLocation()) > FMath::Square(35.f))
			{
				Rollback();
				return false;
			}
		}
		else
		{
			FPreparedMove Prepared;
			Prepared.Controller = GetReadyController(Candidate);
			if (!Prepared.Controller
				|| !PrepareMove(*Navigation, Candidate->GetNavAgentPropertiesRef(), Prepared.Controller,
					*Prepared.Controller->GetPathFollowingComponent(), Candidate->GetNavAgentLocation(),
					Goal, Prepared))
			{
				Rollback();
				return false;
			}
			Moves.Add(MoveTemp(Prepared));
		}
	}
	for (const FPreparedMove& Move : Moves)
	{
		if (!StartPreparedMove(Move)) { Rollback(); return false; }
	}
	const int32 Cost = GetReinforcementCost();
	if (TeamIndex == 5)
	{
		if (State->EnemyResources < Cost) { Rollback(); return false; }
		State->EnemyResources -= Cost;
		State->ForceNetUpdate();
	}
	else if (!Wallet->TrySpend(Cost)) { Rollback(); return false; }
	for (AArmyUnit* Candidate : Candidates) Units.Add(Candidate);
	if (bRebuild)
	{
		Order = EArmyOrder::Hold;
		AttackTarget = nullptr;
		Destination = HomeLocation;
		++OrderSerial;
	}
	ForceNetUpdate();
	UE_LOG(LogArmyOrders, Display, TEXT("%s %s army=%d cost=%d balance=%d units=%d"),
		*GetName(), bRebuild ? TEXT("rebuilt") : TEXT("restored"), ArmyIndex, Cost,
		TeamIndex == 5 ? State->EnemyResources : Wallet->Resources, Units.Num());
	return true;
}

FVector AArmyGroup::GetCenter() const
{
	FVector Center = FVector::ZeroVector;
	int32 Count = 0;
	for (const AArmyUnit* Unit : Units)
	{
		if (IsValid(Unit) && Unit->IsAlive())
		{
			Center += Unit->GetActorLocation();
			++Count;
		}
	}
	return Count > 0 ? Center / Count : GetActorLocation();
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
	}
}

bool AArmyGroup::IssueHold()
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || Units.IsEmpty() || (State && State->MatchResult != EMatchResult::Ongoing))
	{
		return false;
	}
	StopAllUnits();
	AttackTarget = nullptr;
	Order = EArmyOrder::Hold;
	Destination = GetCenter();
	++OrderSerial;
	ForceNetUpdate();
	LogOrder();
	return true;
}

bool AArmyGroup::IssueMove(FVector InDestination)
{
	return IssueTravel(EArmyOrder::Move, InDestination);
}

bool AArmyGroup::IssueAttack(FVector InDestination, AActor* InTarget)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!IsArenaLocation(InDestination) || (State && State->MatchResult != EMatchResult::Ongoing)) return false;
	FVector Anchor = InDestination;
	if (InTarget)
	{
		if (!IsValid(InTarget) || InTarget->GetWorld() != GetWorld() || !CombatTarget::IsAliveHostile(InTarget, TeamIndex))
			return false;
		// Targeted attacks approach a firing position instead of ordering the
		// formation into the occupied target.
		FVector FromTarget = GetCenter() - InTarget->GetActorLocation();
		FromTarget.Z = 0.f;
		if (!FromTarget.Normalize()) FromTarget = FVector(-1.f, 0.f, 0.f);
		Anchor = InTarget->GetActorLocation() + FromTarget * 650.f;
		Anchor.Z = 0.f;
	}
	if (!IssueTravel(EArmyOrder::Attack, Anchor)) return false;
	AttackTarget = InTarget;
	ForceNetUpdate();
	return true;
}

bool AArmyGroup::IssueRetreat()
{
	return IssueTravel(EArmyOrder::Retreat, HomeLocation);
}

bool AArmyGroup::IssueTravel(EArmyOrder NewOrder, const FVector& InDestination)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || Units.IsEmpty() || !IsArenaLocation(InDestination)
		|| (State && State->MatchResult != EMatchResult::Ongoing))
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
		AAIController* AI = GetReadyController(Unit);
		if (!AI)
		{
			return false;
		}
		const FNavAgentProperties& Agent = Unit->GetNavAgentPropertiesRef();
		const FVector Start = Unit->GetNavAgentLocation();

		if (Index == 0)
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

	StopAllUnits();
	for (const FPreparedMove& Move : Prepared)
	{
		// Submit the complete prevalidated path; no second query or delayed order.
		StartPreparedMove(Move);
	}
	Order = NewOrder;
	AttackTarget = nullptr;
	Destination = ProjectedCenter;
	++OrderSerial;
	ForceNetUpdate();
	LogOrder();
	return true;
}

void AArmyGroup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || (CombatAccumulator += DeltaSeconds) < .25f) return;
	CombatAccumulator = 0.f;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (State && State->MatchResult != EMatchResult::Ongoing) return;
	UpdateCombat();
}

void AArmyGroup::UpdateCombat()
{
	if (Order == EArmyOrder::Retreat) return;
	if (Units.IsEmpty())
	{
		if (AttackTarget) { AttackTarget = nullptr; ForceNetUpdate(); }
		return;
	}
	TArray<AArmyUnit*, TInlineAllocator<32>> Enemies;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
	{
		if (It->TeamIndex == TeamIndex) continue;
		for (AArmyUnit* Enemy : It->Units)
			if (IsValid(Enemy) && Enemy->IsAlive()) Enemies.Add(Enemy);
	}
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	AHeadquarters* HostileHQ = State ? (TeamIndex == 5 ? State->FriendlyHeadquarters.Get() : State->EnemyHeadquarters.Get()) : nullptr;
	if (!CombatTarget::IsAliveHostile(AttackTarget.Get(), TeamIndex)
		|| FVector::DistSquared2D(AttackTarget->GetActorLocation(), Destination) > FMath::Square(PursuitRadius))
	{
		if (AttackTarget) { AttackTarget = nullptr; ForceNetUpdate(); }
	}

	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		AArmyUnit* Unit = Units[Index];
		if (!IsValid(Unit) || !Unit->IsAlive()) continue;
		auto Permitted = [this, Unit](AActor* Enemy)
		{
			if (!CombatTarget::IsAliveHostile(Enemy, TeamIndex)) return false;
			const float Distance = FVector::DistSquared2D(Unit->GetActorLocation(), Enemy->GetActorLocation());
			return Order == EArmyOrder::Attack
				? FVector::DistSquared2D(Enemy->GetActorLocation(), Destination) <= FMath::Square(PursuitRadius)
					&& FVector::DistSquared2D(Unit->GetActorLocation(), Destination) <= FMath::Square(PursuitRadius)
					&& Distance <= FMath::Square(1450.f)
				: Distance <= FMath::Square(Unit->WeaponRange());
		};
		AActor* Chosen = Permitted(Unit->Target.Get()) ? Unit->Target.Get() : nullptr;
		if (!Chosen && Permitted(AttackTarget.Get())) Chosen = AttackTarget.Get();
		if (!Chosen)
		{
			float Best = TNumericLimits<float>::Max();
			for (AArmyUnit* Enemy : Enemies)
			{
				if (!Permitted(Enemy)) continue;
				const float Distance = FVector::DistSquared2D(Unit->GetActorLocation(), Enemy->GetActorLocation());
				if (Distance < Best) { Best = Distance; Chosen = Enemy; }
			}
			if (Permitted(HostileHQ))
			{
				const float Distance = FVector::DistSquared2D(Unit->GetActorLocation(), HostileHQ->GetActorLocation());
				if (Distance < Best) Chosen = HostileHQ;
			}
		}
		if (Unit->Target != Chosen) { Unit->Target = Chosen; Unit->ForceNetUpdate(); }
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
		const float Distance = FVector::Dist2D(Unit->GetActorLocation(), Chosen->GetActorLocation());
		if (Order == EArmyOrder::Attack && AI)
		{
			if (Distance <= Unit->WeaponRange())
			{
				if (AI->GetMoveStatus() != EPathFollowingStatus::Idle)
				{
					AI->StopMovement();
					Unit->GetCharacterMovement()->StopMovementImmediately();
				}
				Unit->bPursuing = true;
				Unit->PursuitGoal = Unit->GetActorLocation();
			}
			else
			{
				FVector Direction = Unit->GetActorLocation() - Chosen->GetActorLocation();
				Direction.Z = 0.f;
				Direction.Normalize();
				FVector Goal = Chosen->GetActorLocation() + Direction * (Unit->WeaponRange() * .82f);
				Goal.Z = Destination.Z;
				const FVector Offset = Goal - Destination;
				if (Offset.Size2D() > PursuitRadius) Goal = Destination + Offset.GetSafeNormal2D() * PursuitRadius;
				if (!Unit->bPursuing || FVector::DistSquared2D(Goal, Unit->PursuitGoal) > FMath::Square(130.f))
				{
					UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
					FPreparedMove Pursuit;
					Pursuit.Controller = AI;
					if (Navigation && PrepareMove(*Navigation, Unit->GetNavAgentPropertiesRef(), AI,
						*AI->GetPathFollowingComponent(), Unit->GetNavAgentLocation(), Goal, Pursuit, 75.f))
					{
						bool bWithinBounds = true;
						for (const FNavPathPoint& Point : Pursuit.Path->GetPathPoints())
						{
							if (FVector::DistSquared2D(Point.Location, Destination) > FMath::Square(PursuitRadius))
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
		}
		Unit->FireAt(Chosen);
	}
}

void AArmyGroup::SettleMatch()
{
	if (!HasAuthority()) return;
	StopAllUnits();
	AttackTarget = nullptr;
	Order = EArmyOrder::Hold;
	Destination = GetCenter();
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
	DOREPLIFETIME(AArmyGroup, AttackTarget);
}
