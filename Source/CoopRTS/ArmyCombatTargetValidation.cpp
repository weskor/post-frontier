#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyCombatScenario.h"

namespace ArmyCombatScenarioPrivate
{
bool FArmyCombatScenario::RejectUnregisteredTargets()
{
	ACommandGameState* State = Army->GetWorld()->GetGameState<ACommandGameState>();
	const uint32 Serial = Army->OrderSerial;
	const FVector Destination = Army->Destination;
	if (!CheckDetachedTarget(Serial, Destination) || !CheckUnregisteredHeadquarters(State, Serial, Destination))
		return false;
	return CheckUnregisteredBuilding(State, Serial, Destination);
}

bool FArmyCombatScenario::Rejected(AActor* Target, uint32 Serial, const FVector& Destination)
{
	const FCommandResult Result = FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::Attack, INDEX_NONE, Target);
	return !Result.IsAccepted() && Army->OrderSerial == Serial
		&& Army->Destination == Destination && !Army->TargetStructure;
}

bool FArmyCombatScenario::CheckDetachedTarget(uint32 Serial, const FVector& Destination)
{
	AArmyUnit* Detached = nullptr;
	for (AArmyUnit* Unit : Enemy->GetUnits())
		if (IsValid(Unit) && Unit->IsAlive())
		{
			Detached = Unit;
			break;
		}
	if (!Check(Detached != nullptr, TEXT("A living hostile is available for membership validation")))
		return false;
	Enemy->OnMemberDied(Detached); // Membership fixture: health and the back-pointer remain live.
	const bool bUnitRejected = Rejected(Detached, Serial, Destination);
	Detached->Destroy();
	if (!Check(bUnitRejected, TEXT("A live unit absent from its group rejects Attack without changing accepted intent")))
		return false;
	return true;
}

bool FArmyCombatScenario::CheckUnregisteredHeadquarters(ACommandGameState* State, uint32 Serial, const FVector& Destination)
{
	AHeadquarters* HQ = State->EnemyHeadquarters;
	State->EnemyHeadquarters = nullptr;
	const bool bHQRejected = Rejected(HQ, Serial, Destination);
	State->EnemyHeadquarters = HQ;
	if (!Check(bHQRejected, TEXT("An unregistered live hostile HQ rejects Attack without changing accepted intent")))
		return false;
	return true;
}

bool FArmyCombatScenario::CheckUnregisteredBuilding(ACommandGameState* State, uint32 Serial, const FVector& Destination)
{
	const FTransform Transform(State->EnemyHeadquarters->GetActorLocation() + FVector(900.f, 0.f, 65.f));
	ACommandBuilding* Building = Army->GetWorld()->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(),
		Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Check(Building != nullptr, TEXT("A hostile building registration fixture spawns")))
		return false;
	Building->BuildingIndex = ArmyTestSetup::WorkshopIndex;
	Building->TeamIndex = 5;
	Building->OwningPlayerState = State->EnemyCommander;
	Building->FinishSpawning(Transform);
	State->Buildings.Remove(Building);
	const bool bBuildingRejected = Building->IsAlive() && Rejected(Building, Serial, Destination);
	Building->Destroy();
	return Check(bBuildingRejected, TEXT("An unregistered live hostile building rejects Attack without changing accepted intent"));
}
}

#endif
