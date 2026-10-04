#include "ArmyGroup.h"

#include "AIController.h"
#include "ArenaBounds.h"
#include "ArmyGroupInternal.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Components/CapsuleComponent.h"
#include "Content/MatchContent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GroundHeight.h"
#include "NavigationSystem.h"

using namespace ArmyGroupInternal;

namespace
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
constexpr int32 InitialUnitCount = MaxUnitCount;
#endif

// The exit must lie a short walk outside the producer's footprint, on navigable open ground.
bool ReinforcementExitValid(UNavigationSystemV1& Navigation, const ACommandBuilding& Producer,
	const FVector& SpawnLocation, FNavLocation& Projected)
{
	UWorld* World = Navigation.GetWorld();
	const double ExitDistance = FVector::DistSquared2D(SpawnLocation, Producer.GetActorLocation());
	const float Radius = ACommandBuilding::GetFootprintRadius(*Producer.GetDefinition());
	return ExitDistance >= FMath::Square(Radius + 75.f)
		&& ExitDistance <= FMath::Square(Radius + 700.f)
		&& Navigation.ProjectPointToNavigation(SpawnLocation, Projected, FVector(45.f, 45.f, 200.f))
		&& AArenaBounds::IsTravelLocation(World, Projected.Location)
		&& FVector::DistSquared2D(SpawnLocation, Projected.Location) <= FMath::Square(45.f)
		&& FMath::Abs(SpawnLocation.Z - Projected.Location.Z) <= 110.f
		&& !World->OverlapBlockingTestByChannel(Projected.Location + FVector(0.f, 0.f, ExitProbeLift()),
			FQuat::Identity, ECC_Pawn, UnitCapsule());
}
}

int32 ArmyGroupInternal::VacantReinforcementSlot(const TArray<TObjectPtr<AArmyUnit>>& Units, int32 UnitIndex, int32 Capacity)
{
	uint32 Occupied = 0;
	int32 Living = 0;
	for (const AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive())
			continue;
		if (Unit->GetUnitIndex() != UnitIndex || Unit->GetCompositionSlot() < 0 || Unit->GetCompositionSlot() >= Capacity)
			return INDEX_NONE;
		Occupied |= 1u << Unit->GetCompositionSlot();
		++Living;
	}
	if (Living >= Capacity)
		return INDEX_NONE;
	return ArmyGroupPolicy::FirstVacantSlot(Occupied, Capacity);
}

FCollisionShape ArmyGroupInternal::UnitCapsule()
{
	const UCapsuleComponent* Capsule = GetDefault<AArmyUnit>()->GetCapsuleComponent();
	return FCollisionShape::MakeCapsule(Capsule->GetUnscaledCapsuleRadius(), Capsule->GetUnscaledCapsuleHalfHeight());
}

float ArmyGroupInternal::SpawnLift()
{
	// The capsule's half height plus a hand's width, so a recruit settles onto the floor instead of starting in it.
	return GetDefault<AArmyUnit>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 5.f;
}

float ArmyGroupInternal::ExitProbeLift()
{
	return SpawnLift() + 20.f;
}

bool AArmyGroup::HasPermittedOwner(const ACommandGameState& State) const
{
	return IsValid(OwningPlayerState) && OwningPlayerState->GetWorld() == GetWorld()
		&& ArmyGroupPolicy::OwnerPermitted(TeamIndex, OwningPlayerState->TeamIndex,
			OwningPlayerState->CommanderIndex, OwningPlayerState == State.EnemyCommander);
}

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
bool AArmyGroup::SpawnUnits()
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const UMatchContent* Content = State ? State->Content.Get() : nullptr;
	if (!HasAuthority() || !Units.IsEmpty() || !Content || !HasPermittedOwner(*State)
		|| (TeamIndex != 0 && !bOpposingArmy))
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
#endif

AArmyUnit* AArmyGroup::SpawnMember(int32 UnitIndex, const FVector& SpawnLocation, int32 CompositionSlot)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const UArmyUnitDefinition* Definition = State && State->Content ? State->Content->Unit(UnitIndex) : nullptr;
	if (!HasAuthority() || IsActorBeingDestroyed() || !State || State->MatchResult != EMatchResult::Ongoing
		|| !Definition || IsValid(ProductionBuilding) || CompositionSlot < 0 || CompositionSlot >= MaxUnitCount
		|| !HasPermittedOwner(*State)
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
	const FTransform Transform(Ground.Location + FVector(0.f, 0.f, SpawnLift()));
	AArmyUnit* Unit = GetWorld()->SpawnActorDeferred<AArmyUnit>(AArmyUnit::StaticClass(), Transform,
		this, nullptr, ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
	if (!Unit)
		return nullptr;
	Unit->Initialize(this, TeamIndex, OwningPlayerState->CommanderIndex, ArmyIndex,
		CompositionSlot, UnitIndex, const_cast<UArmyUnitDefinition*>(Definition));
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

int32 AArmyGroup::NextArmyIndex(const UWorld& World)
{
	int32 Index = 0;
	for (const ULevel* Level : World.GetLevels())
	{
		if (!Level)
			continue;
		for (const AActor* Actor : Level->Actors)
			if (const AArmyGroup* Group = Cast<AArmyGroup>(Actor); IsValid(Group))
				Index = FMath::Max(Index, Group->GetArmyIndex() + 1);
	}
	return Index;
}

AArmyGroup* AArmyGroup::SpawnFreeForce(UWorld& World, ACommandPlayerState& Owner, const FVector& Anchor,
	TConstArrayView<int32> UnitIndices, int32 InForceNumber, float InSpeedFactor)
{
	ACommandGameState* State = World.GetGameState<ACommandGameState>();
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	// JEV's waves and a human commander's emergency force (HqHoldPolicy) are the free forces.
	const bool bJev = State && &Owner == State->EnemyCommander;
	const bool bHuman = Owner.TeamIndex == 0 && Owner.CommanderIndex >= 0;
	if (!State || !Navigation || State->MatchResult != EMatchResult::Ongoing || !(bJev || bHuman)
		|| UnitIndices.IsEmpty() || UnitIndices.Num() > MaxUnitCount)
		return nullptr;
	const FTransform Transform(Anchor);
	// A human force is owned by its commander's controller, like a produced one: that is what lets the commander order it.
	AActor* ControllerOwner = bHuman ? Owner.GetOwner() : nullptr;
	AArmyGroup* Group = World.SpawnActorDeferred<AArmyGroup>(StaticClass(), Transform, ControllerOwner, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Group)
		return nullptr;
	Group->Initialize({ bJev ? 5 : 0, &Owner, bJev ? -1 : NextArmyIndex(World), nullptr, Anchor });
	Group->ForceNumber = InForceNumber;
	Group->SpeedFactor = InSpeedFactor;
	Group->FinishSpawning(Transform);
	// Each member takes the nearest navigable point around the anchor, ring by ring, that no
	// earlier member occupies and where a unit fits.
	constexpr float RingSpacing = 170.f;
	constexpr float MemberSpacing = 130.f;
	TArray<FVector, TInlineAllocator<MaxUnitCount>> Placed;
	const auto Place = [&](int32 Slot) {
		for (int32 Ring = 0; Ring < 6; ++Ring)
			for (int32 Step = 0, Steps = FMath::Max(1, 6 * Ring); Step < Steps; ++Step)
			{
				const float Angle = 2.f * PI * Step / Steps;
				const FVector Candidate = Anchor + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (Ring * RingSpacing);
				FNavLocation Ground;
				if (!Navigation->ProjectPointToNavigation(Candidate, Ground, FVector(60.f, 60.f, 200.f))
					|| Placed.ContainsByPredicate([&](const FVector& Other) {
						   return FVector::DistSquared2D(Other, Ground.Location) < FMath::Square(MemberSpacing);
					   }))
					continue;
				if (Group->SpawnMember(UnitIndices[Slot], Ground.Location, Slot))
				{
					Placed.Add(Ground.Location);
					return;
				}
			}
	};
	for (int32 Slot = 0; Slot < UnitIndices.Num(); ++Slot)
		Place(Slot);
	if (Group->Units.IsEmpty())
	{
		Group->Destroy();
		return nullptr;
	}
	Group->Destination = Group->GetCenter();
	Group->ForceNetUpdate();
	return Group;
}

bool AArmyGroup::CanAcceptRecruit(const ACommandGameState* State, int32 UnitIndex, int32 Capacity) const
{
	return HasAuthority() && !IsActorBeingDestroyed() && State && State->MatchResult == EMatchResult::Ongoing
		&& IsValid(ProductionBuilding) && ProductionBuilding->IsAlive() && ProductionBuilding->IsComplete()
		&& ProductionBuilding->IsProducer() && ProductionBuilding->bForceConfigured
		&& ProductionBuilding->ForceGroup == this && ProductionBuilding->ProductionUnitIndex == UnitIndex
		&& ProductionBuilding->TeamIndex == TeamIndex && ProductionBuilding->OwningPlayerState == OwningPlayerState
		&& HasPermittedOwner(*State) && Capacity != 0;
}

bool AArmyGroup::SpawnReinforcement(int32 UnitIndex, const FVector& SpawnLocation)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const UArmyUnitDefinition* Definition = State && State->Content ? State->Content->Unit(UnitIndex) : nullptr;
	const int32 Capacity = Definition ? ACommandBuilding::GetForceCapacity(*Definition) : 0;
	if (!CanAcceptRecruit(State, UnitIndex, Capacity) || !AArenaBounds::IsTravelLocation(GetWorld(), SpawnLocation))
		return false;
	const int32 Slot = VacantReinforcementSlot(Units, UnitIndex, Capacity);
	if (Slot == INDEX_NONE)
		return false;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (!Navigation || !ReinforcementExitValid(*Navigation, *ProductionBuilding, SpawnLocation, Projected))
		return false;
	return SpawnJoined(*Definition, UnitIndex, Slot, Projected.Location) != nullptr;
}

AArmyUnit* AArmyGroup::SpawnJoined(const UArmyUnitDefinition& Definition, int32 UnitIndex, int32 Slot, const FVector& Ground)
{
	const FTransform Transform(FRotator::ZeroRotator, GroundHeight::Snap(*GetWorld(), Ground) + FVector(0.f, 0.f, SpawnLift()));
	AArmyUnit* Candidate = GetWorld()->SpawnActorDeferred<AArmyUnit>(AArmyUnit::StaticClass(), Transform,
		this, nullptr, ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
	if (!Candidate)
		return nullptr;
	Candidate->Initialize(this, TeamIndex, IsValid(OwningPlayerState) ? OwningPlayerState->CommanderIndex : -1,
		ArmyIndex, Slot, UnitIndex, const_cast<UArmyUnitDefinition*>(&Definition));
	Candidate->FinishSpawning(Transform);
	if (!IsValid(Candidate) || !GetReadyController(Candidate)
		|| FVector::DistSquared2D(Candidate->GetActorLocation(), Transform.GetLocation()) > FMath::Square(40.f))
	{
		DestroyUnit(Candidate);
		return nullptr;
	}
	bProducedGroup = true;
	ForceCapacity = ACommandBuilding::GetForceCapacity(Definition);
	Units.RemoveAll([](const TObjectPtr<AArmyUnit>& Unit) { return !IsValid(Unit) || !Unit->IsAlive(); });
	Units.Reserve(ForceCapacity);
	Units.Add(Candidate);
	ForceNetUpdate();
	return Candidate;
}
