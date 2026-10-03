#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyCombatScenario.h"

namespace ArmyCombatScenarioPrivate
{
bool FArmyCombatScenario::CheckCounterAcquisition(const ACommandGameState* State)
{
	// Run real acquisition without advancing cooldowns or hurting these fixtures.
	// Positions come from the map's HQ, and are restored before the encounter.
	FVector FriendlyPositions[6];
	FVector HostilePositions[6];
	for (int32 Index = 0; Index < 6; ++Index)
	{
		FriendlyPositions[Index] = Army->GetUnits()[Index]->GetActorLocation();
		HostilePositions[Index] = Enemy->GetUnits()[Index]->GetActorLocation();
		Army->GetUnits()[Index]->NextAttackTime = TNumericLimits<float>::Max();
	}
	AHeadquarters* HQ = State->EnemyHeadquarters;
	const FVector Anchor = HQ->GetActorLocation() + FVector(900.f, 0.f, 0.f);
	for (AArmyUnit* Unit : Army->GetUnits())
		Unit->SetActorLocation(Anchor, false, nullptr, ETeleportType::TeleportPhysics);
	if (!Check(FCommandService::SetRetreatThreshold(Army->GetOwningPlayerState(), Army.Get(), ERetreatThreshold::Never).IsAccepted()
				&& FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::Attack,
					ArmyTestSetup::RegionAt(State, Anchor))
					.IsAccepted(),
			TEXT("Targeting fixture accepts a real region Attack without casualty withdrawal")))
		return false;
	if (!Check(Army->Verb == EForceVerb::Attack && Army->Order == EArmyOrder::Attack && !Army->IsHoldingRegion(),
			TEXT("Counter and persistent-lock probes exercise local Attack combat, not shared-post Holding")))
		return false;
	bool bOk = CheckCounterPreference(Anchor);
	if (!CheckStructurePriority(HQ, bOk) || !CheckPersistentLock(State, Anchor, bOk))
		return false;
	CheckChaseBounds(bOk);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Army->GetUnits()[Index]->SetActorLocation(FriendlyPositions[Index], false, nullptr, ETeleportType::TeleportPhysics);
		Enemy->GetUnits()[Index]->SetActorLocation(HostilePositions[Index], false, nullptr, ETeleportType::TeleportPhysics);
		Army->GetUnits()[Index]->NextAttackTime = 0.f;
	}
	FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::MoveHold, ArmyTestSetup::CurrentRegion(Army.Get()));
	return bOk;
}

bool FArmyCombatScenario::CheckCounterPreference(const FVector& Anchor)
{
	Enemy->GetUnits()[0]->SetActorLocation(Anchor + FVector(50.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	Enemy->GetUnits()[2]->SetActorLocation(Anchor + FVector(100.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	Enemy->GetUnits()[4]->SetActorLocation(Anchor + FVector(150.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	static_cast<AActor*>(Army.Get())->Tick(.25f);
	bool bOk = Check(Army->GetUnits()[0]->Target == Enemy->GetUnits()[2]
			&& Army->GetUnits()[2]->Target == Enemy->GetUnits()[0]
			&& Army->GetUnits()[4]->Target
			&& CombatTarget::ArmorClass(Army->GetUnits()[4]->Target) == EArmorClass::Structure,
		TEXT("Live acquisition prefers nearest Light for Kinetic, Heavy for Piercing, and Structure for Demolition"));
	Enemy->GetUnits()[4]->SetActorLocation(Anchor + FVector(75.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	static_cast<AActor*>(Army.Get())->Tick(.25f);
	bOk &= Check(Army->GetUnits()[0]->Target == Enemy->GetUnits()[2],
		TEXT("A live in-range target stays selected when a nearer matching-class enemy appears"));
	return bOk;
}

bool FArmyCombatScenario::CheckStructurePriority(AHeadquarters* HQ, bool& bOk)
{
	// A registered structure command must outrank unit counter preference.
	if (!Check(FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(),
				   EForceVerb::Attack, INDEX_NONE, HQ)
				   .IsAccepted(),
			TEXT("Explicit hostile HQ Attack is accepted")))
		return false;
	if (!Check(Army->Verb == EForceVerb::Attack && Army->Order == EArmyOrder::Attack && !Army->IsHoldingRegion(),
			TEXT("Explicit structure priority exercises the Attack phase")))
		return false;
	static_cast<AActor*>(Army.Get())->Tick(.25f);
	bOk &= Check(Army->TargetStructure == HQ && Army->GetUnits()[0]->Target == HQ,
		TEXT("Explicit structure Attack outranks an automatic unit counter lock"));
	const AArmyUnit* StructureChaser = Army->GetUnits()[0];
	const AAIController* ChaseAI = Cast<AAIController>(StructureChaser->GetController());
	const UPathFollowingComponent* ChasePath = ChaseAI ? ChaseAI->GetPathFollowingComponent() : nullptr;
	bOk &= Check(FVector::Dist2D(StructureChaser->GetActorLocation(), HQ->GetActorLocation()) > StructureChaser->WeaponRange()
			&& StructureChaser->bPursuing && ChasePath && ChasePath->GetStatus() == EPathFollowingStatus::Moving
			&& ChasePath->GetPath().IsValid(),
		TEXT("Explicit structure Attack starts a real out-of-range frontline chase"));
	if (ChasePath && ChasePath->GetPath().IsValid())
		for (const FNavPathPoint& Point : ChasePath->GetPath()->GetPathPoints())
			bOk &= Check(FVector::Dist2D(Point.Location, Army->Destination) <= Army->PursuitRadius,
				TEXT("The real structure chase is bounded along its entire navigation path"));
	return true;
}

bool FArmyCombatScenario::CheckPersistentLock(const ACommandGameState* State, const FVector& Anchor, bool& bOk)
{
	// Establish a non-counter lock through normal acquisition, not a seeded Target.
	// All Light candidates are outside the leash until the Heavy lock is real.
	const int32 Region = ArmyTestSetup::RegionAt(State, Anchor);
	if (!Check(FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(),
				   EForceVerb::Attack, Region)
				   .IsAccepted(),
			TEXT("Region Attack can replace the structure command")))
		return false;
	if (!Check(Army->Verb == EForceVerb::Attack && Army->Order == EArmyOrder::Attack && !Army->IsHoldingRegion(),
			TEXT("Non-counter lock fixture remains in the ordinary local Attack phase")))
		return false;
	AArmyUnit* Frontline = Army->GetUnits()[0];
	AArmyUnit* Heavy = Enemy->GetUnits()[0];
	AArmyUnit* Light = Enemy->GetUnits()[2];
	for (AArmyUnit* Unit : Enemy->GetUnits())
		Unit->SetActorLocation(Army->Destination + FVector(0.f, Army->PursuitRadius + 700.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
	Heavy->SetActorLocation(Frontline->GetActorLocation() + FVector(50.f, 0.f, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	static_cast<AActor*>(Army.Get())->Tick(.25f);
	bOk &= Check(Frontline->Target == Heavy, TEXT("Normal Attack acquisition establishes an eligible non-counter Heavy lock"));
	Light->SetActorLocation(Frontline->GetActorLocation() + FVector(75.f, 0.f, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	static_cast<AActor*>(Army.Get())->Tick(.25f);
	bOk &= Check(Frontline->Target == Heavy,
		TEXT("An eligible non-counter lock persists when a counter becomes available"));
	Heavy->SetActorLocation(Army->Destination + FVector(0.f, Army->PursuitRadius + 700.f, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	static_cast<AActor*>(Army.Get())->Tick(.25f);
	bOk &= Check(Frontline->Target == Light, TEXT("A lock leaving the Attack leash triggers fresh counter acquisition"));
	return true;
}

void FArmyCombatScenario::CheckChaseBounds(bool& bOk)
{
	for (const AArmyUnit* Unit : Army->GetUnits())
	{
		bOk &= Check(Unit->Target != Heavy
				&& FVector::Dist2D(Unit->GetActorLocation(), Army->Destination) <= Army->PursuitRadius + 200.f,
			TEXT("Attack invalidates a target leaving the pursuit area and keeps members inside its boundary"));
		const AAIController* AI = Cast<AAIController>(Unit->GetController());
		const UPathFollowingComponent* Path = AI ? AI->GetPathFollowingComponent() : nullptr;
		// bPursuing also denotes an idle, in-range engagement. Its last move
		// goal is not meaningful there; bound the actual chase request instead.
		if (Unit->bPursuing && Path && Path->GetStatus() == EPathFollowingStatus::Moving)
		{
			bOk &= Check(Path->GetPath().IsValid(), TEXT("A moving Attack chase owns a real navigation path"));
			if (Path->GetPath().IsValid())
				for (const FNavPathPoint& Point : Path->GetPath()->GetPathPoints())
					bOk &= Check(FVector::Dist2D(Point.Location, Army->Destination) <= Army->PursuitRadius,
						TEXT("Every point of an active Attack chase stays inside the pursuit boundary"));
		}
	}
}
}

#endif
