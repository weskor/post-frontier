#include "ArmyGroup.h"

#include "ArmyGroupInternal.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogArmyOrders, Log, All);

using namespace ArmyGroupInternal;

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
	// A dead producer takes its in-transit and waiting recruits with it; the owner gets their Power back.
	CancelRecruits();
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
	return ArmyGroupPolicy::FormationOffset(FormationShape(), Index);
}

float& AArmyGroup::PursuitRetryAt(int32 Slot)
{
	if (NextPursuitAttempts.Num() <= Slot)
		NextPursuitAttempts.SetNumZeroed(Slot + 1);
	return NextPursuitAttempts[Slot];
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
	return Count > 0 ? Center / Count : AppliedWaypoint != INDEX_NONE ? Destination
																	  : GetActorLocation();
}

void AArmyGroup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || (CombatAccumulator += DeltaSeconds) < .25f)
		return;
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
	UpdateSupply();
	TickOrders();
	UpdateCombat();
}

void AArmyGroup::SettleMatch()
{
	if (!HasAuthority())
		return;
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
	DOREPLIFETIME(AArmyGroup, Verb);
	DOREPLIFETIME(AArmyGroup, TargetRegionIndex);
	DOREPLIFETIME(AArmyGroup, TargetStructure);
	DOREPLIFETIME(AArmyGroup, Orders);
	DOREPLIFETIME(AArmyGroup, IntentRoutes);
	DOREPLIFETIME(AArmyGroup, Status);
	DOREPLIFETIME(AArmyGroup, RetreatThreshold);
	DOREPLIFETIME(AArmyGroup, MarchSpeed);
	DOREPLIFETIME(AArmyGroup, SpeedFactor);
	DOREPLIFETIME(AArmyGroup, WaypointRegionIndex);
	DOREPLIFETIME(AArmyGroup, ResumeCount);
	DOREPLIFETIME(AArmyGroup, Units);
	DOREPLIFETIME(AArmyGroup, HomeLocation);
	DOREPLIFETIME(AArmyGroup, TeamIndex);
	DOREPLIFETIME(AArmyGroup, OwningPlayerState);
	DOREPLIFETIME(AArmyGroup, ArmyIndex);
	DOREPLIFETIME(AArmyGroup, ForceNumber);
	DOREPLIFETIME(AArmyGroup, AttackTarget);
	DOREPLIFETIME(AArmyGroup, HoldRegionIndex);
	DOREPLIFETIME(AArmyGroup, HoldPostIndex);
	DOREPLIFETIME(AArmyGroup, HoldPostLocation);
	DOREPLIFETIME(AArmyGroup, bHoldResponding);
	DOREPLIFETIME(AArmyGroup, HoldThreat);
	DOREPLIFETIME(AArmyGroup, HoldThreatenedAsset);
	DOREPLIFETIME(AArmyGroup, HoldThreatKind);
	DOREPLIFETIME(AArmyGroup, ProductionBuilding);
	DOREPLIFETIME(AArmyGroup, RecruitsInTransit);
	DOREPLIFETIME(AArmyGroup, RecruitsWaiting);
	DOREPLIFETIME(AArmyGroup, bSupplyCutOff);
	DOREPLIFETIME(AArmyGroup, bFreeForce);
}
