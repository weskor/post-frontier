#include "CommandBuilding.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CoopAudioSubsystem.h"
#include "ObjectiveAnnouncer.h"
#include "DepositSite.h"
#include "Content/MatchContent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationSystem.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Rules/EconomyPolicy.h"

ACommandBuilding::ACommandBuilding()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick = true;
	Footprint = CreateDefaultSubobject<UBoxComponent>(TEXT("BuildingFootprint"));
	SetRootComponent(Footprint);
	Footprint->InitBoxExtent(FVector(125.f, 125.f, 65.f));
	Footprint->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Footprint->SetCollisionResponseToAllChannels(ECR_Ignore);
	Footprint->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Footprint->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Footprint->SetCanEverAffectNavigation(true);
	UNavModifierComponent* Obstacle = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavigationObstacle"));
	Obstacle->SetAreaClass(UNavArea_Null::StaticClass());
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BuildingBody"));
	Body->SetupAttachment(Footprint);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		CubeMesh = Cube.Object;
		Body->SetStaticMesh(Cube.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Materials/M_CommandUnit.M_CommandUnit"));
	if (Material.Succeeded())
	{
		CubeMaterial = Material.Object;
		Body->SetMaterial(0, Material.Object);
	}
}

const UBuildingDefinition* ACommandBuilding::GetDefinition() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return State && State->Content ? State->Content->Building(BuildingIndex) : nullptr;
}

const UArmyUnitDefinition* ACommandBuilding::GetProductionDefinition() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return State && State->Content ? State->Content->Unit(ProductionUnitIndex) : nullptr;
}

bool ACommandBuilding::IsProducer() const
{
	const UBuildingDefinition* Definition = GetDefinition();
	return Definition && Definition->bProducesForces;
}

int32 ACommandBuilding::GetProductionCost() const
{
	const UArmyUnitDefinition* Definition = GetProductionDefinition();
	return Definition ? GetUnitCost(*Definition) : 0;
}

float ACommandBuilding::GetProductionDuration() const
{
	const UArmyUnitDefinition* Definition = GetProductionDefinition();
	return Definition ? GetUnitDuration(*Definition) : 0.f;
}

int32 ACommandBuilding::MaxHealth() const
{
	const UBuildingDefinition* Definition = GetDefinition();
	return Definition ? Definition->MaxHealth : 0;
}

bool ACommandBuilding::IsStunned() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return State && ShieldPolicy::IsStunned(State->GetServerWorldTimeSeconds(), StunEndServerTime);
}

void ACommandBuilding::ApplyStun(float Seconds)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || IsActorBeingDestroyed() || !IsAlive() || !State || Seconds <= 0.f)
		return;
	StunEndServerTime = ShieldPolicy::StunEndTime(State->GetServerWorldTimeSeconds(), StunEndServerTime, Seconds);
	ForceNetUpdate();
}

bool ACommandBuilding::IsForceNumberReserved(const ACommandGameState& State, int32 Number) const
{
	for (const ACommandBuilding* Other : State.Buildings)
		if (IsValid(Other) && Other != this && !Other->IsActorBeingDestroyed() && Other->IsAlive() && Other->IsProducer()
			&& Other->TeamIndex == TeamIndex && (TeamIndex == 5 || Other->OwningPlayerState == OwningPlayerState)
			&& Other->ForceNumber == Number)
			return true;
	// A number also stays reserved while any living survivor of the force that held it remains.
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
	{
		const AArmyGroup* Group = *It;
		if (Group->IsActorBeingDestroyed() || Group->GetTeamIndex() != TeamIndex || Group->ForceNumber != Number
			|| (TeamIndex != 5 && Group->GetOwningPlayerState() != OwningPlayerState))
			continue;
		for (const AArmyUnit* Unit : Group->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive())
				return true;
	}
	return false;
}

void ACommandBuilding::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// Kind is a derived, replicated view of the definition for callers that still branch on it.
		if (const UBuildingDefinition* Definition = GetDefinition())
			Kind = Definition->GetKind();
		if (Health <= 0)
			Health = MaxHealth();
	}
	AudioPreviousHealth = Health;
	bAudioWasComplete = IsComplete();
	AudioDeploymentCount = DeploymentCount;
	AudioResearchCount = ResearchCount;
	bTerminalAudioHandled = !IsAlive();
	bAudioStateInitialized = true;
	OnRep_Appearance();
	OnRep_PlacementCommitted();
	ACommandGameState* State = HasAuthority() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!State)
		return;
	if (IsProducer() && IsAlive())
	{
		// Lowest free positive number for this owner.
		ForceNumber = 1;
		while (IsForceNumberReserved(*State, ForceNumber))
			++ForceNumber;
	}
	State->Buildings.AddUnique(this);
	InitializeRallyPoint();
	State->ForceNetUpdate();
}

void ACommandBuilding::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		if (!State || State->MatchResult != EMatchResult::Ongoing || !IsAlive())
			return;
		const bool bStunned = IsStunned();
		if (!IsComplete())
		{
			const UBuildingDefinition* Definition = GetDefinition();
			const float Duration = Definition ? GetBuildDuration(*Definition) : 0.f;
			if (Duration <= 0.f)
				return;
			if (!bStunned)
				ConstructionProgress = FMath::Min(1.f, ConstructionProgress + DeltaSeconds / Duration);
			if (IsComplete())
			{
				OnRep_Appearance();
				ForceNetUpdate();
			}
		}
		else if (!bStunned)
			TickProduction(DeltaSeconds);
		InitializeRallyPoint();
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		// Producer locks replicate independently of Body; host and standalone never receive an OnRep, so resync here.
		if (DesiredMesh().ToSoftObjectPath() != AppliedMesh)
			OnRep_Appearance();
	}
}

void ACommandBuilding::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopConstructionAudio();
	if (HasAuthority())
	{
		ReleaseDeposit();
		bProductionEnabled = false;
		if (IsValid(ForceGroup) && !ForceGroup->IsActorBeingDestroyed())
		{
			// Survivors and recruits keep their last accepted front, but never transfer.
			ForceGroup->DetachProducer();
			bool bSurvivors = false;
			for (const AArmyUnit* Unit : ForceGroup->GetUnits())
				if (IsValid(Unit) && Unit->IsAlive())
				{
					bSurvivors = true;
					break;
				}
			if (bSurvivors)
				ForceGroup->ForceNetUpdate();
			else
				ForceGroup->Destroy();
		}
		ForceGroup = nullptr;
		if (ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>())
		{
			State->Buildings.Remove(this);
			State->ForceNetUpdate();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ACommandBuilding::ReleaseDeposit()
{
	if (!HasAuthority() || !IsValid(Deposit) || Deposit->Extractor != this)
		return;
	Deposit->Extractor = nullptr;
	Deposit->ForceNetUpdate();
}

void ACommandBuilding::OnRep_Appearance()
{
	NotifyAudioState();
	if (!Footprint || !Body)
		return;
	const UBuildingDefinition* Definition = GetDefinition();
	const float Radius = Definition ? GetFootprintRadius(*Definition) : 0.f;
	if (Radius > 0.f)
		Footprint->SetBoxExtent(FVector(Radius, Radius, 65.f));
	if (GetNetMode() == NM_DedicatedServer)
		return;
	const TSoftObjectPtr<UStaticMesh> Soft = DesiredMesh();
	AppliedMesh = Soft.ToSoftObjectPath();
	UStaticMesh* Themed = Soft.LoadSynchronous();
	UStaticMesh* Desired = Themed ? Themed : CubeMesh.Get();
	if (Desired && Body->GetStaticMesh() != Desired)
	{
		Body->SetStaticMesh(Desired);
		// SetStaticMesh keeps the previous mesh's override (and any dynamic instance); drop them so the Team slot starts from the mesh default.
		Body->EmptyOverrideMaterials();
		if (!Themed && CubeMaterial)
			Body->SetMaterial(0, CubeMaterial);
	}
	if (Themed)
	{
		// Themed meshes are authored around the footprint centre with ground at z = -65.
		Body->SetRelativeLocation(FVector::ZeroVector);
		Body->SetRelativeScale3D(FVector::OneVector);
	}
	else
	{
		// Fallback cube sized from the footprint so a missing mesh still shows the building's extent.
		const FVector Shape(FMath::Max(1.f, Radius / 60.f), FMath::Max(1.f, Radius / 60.f), 1.85f);
		Body->SetRelativeLocation(FVector(0.f, 0.f, Shape.Z * 50.f - 65.f));
		Body->SetRelativeScale3D(Shape);
	}
	const int32 TeamSlot = Themed ? FMath::Max(0, Body->GetMaterialIndex(TEXT("Team"))) : 0;
	UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(TeamSlot));
	if (!Material)
		Material = Body->CreateAndSetMaterialInstanceDynamic(TeamSlot);
	if (Material)
		Material->SetVectorParameterValue(TEXT("TeamColor"), !IsAlive() ? FLinearColor(.12f, .12f, .12f) : !IsComplete() ? FLinearColor(.8f, .6f, .18f)
				: TeamIndex == 5                                                                                         ? FLinearColor(1.f, .08f, .08f)
																														 : FLinearColor(.04f, .5f, 1.f));
}

TSoftObjectPtr<UStaticMesh> ACommandBuilding::DesiredMesh() const
{
	const UBuildingDefinition* Definition = GetDefinition();
	if (!Definition)
		return nullptr;
	if (!IsComplete())
		return Definition->ConstructionMesh;
	const bool bMachine = TeamIndex == 5;
	// Producers show the locked-type variant once configured; an unlisted type keeps the neutral mesh.
	if (Definition->bProducesForces && bForceConfigured)
	{
		const TArray<TSoftObjectPtr<UStaticMesh>>& Variants = bMachine ? Definition->MachineRoleMeshes : Definition->HumanRoleMeshes;
		if (Variants.IsValidIndex(ProductionUnitIndex) && !Variants[ProductionUnitIndex].IsNull())
			return Variants[ProductionUnitIndex];
	}
	return bMachine ? Definition->MachineMesh : Definition->HumanMesh;
}

void ACommandBuilding::ReceiveAttack(int32 Damage, AArmyUnit* Attacker)
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || IsActorBeingDestroyed() || !IsAlive() || !IsValid(Attacker) || !Attacker->IsAlive()
		|| Attacker->GetTeamIndex() == TeamIndex || Damage <= 0 || !State || State->MatchResult != EMatchResult::Ongoing)
		return;
	State->NotifyRegionDamage(this, TeamIndex, Attacker);
	const DamagePolicy::FResult Result = DamagePolicy::Resolve(Damage, Attacker->GetDamageType(), 0, {});
	Health = FMath::Max(0, Health - Result.HealthLoss);
	OnRep_Appearance();
	ForceNetUpdate();
	if (!IsAlive())
	{
		if (Kind == EBuildingKind::Extractor && TeamIndex == 0)
			if (UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(this))
				Announcer->RaiseFromUnit(TEXT("drill_rig_lost"), this, TeamIndex, GetActorLocation(), Attacker);
		bProductionEnabled = false;
		ReleaseDeposit();
		FCommandBuildingTerminalSnapshot Snapshot;
		Snapshot.ConstructionProgress = ConstructionProgress;
		Snapshot.TeamIndex = TeamIndex;
		MulticastTerminalState(Snapshot);
		Destroy();
	}
}

bool ACommandBuilding::TrySpend(int32 Cost)
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || IsActorBeingDestroyed() || !IsAlive() || !State || State->MatchResult != EMatchResult::Ongoing || Cost <= 0)
		return false;
	return IsValid(OwningPlayerState) && OwningPlayerState->GetWorld() == GetWorld()
		&& OwningPlayerState->TeamIndex == TeamIndex
		&& OwningPlayerState->TrySpend(Cost);
}

void ACommandBuilding::NotifyPlacementCommitted()
{
	if (!HasAuthority() || IsActorBeingDestroyed() || !IsAlive() || PlacementCommittedServerTime >= 0.)
		return;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
		return;
	PlacementCommittedServerTime = State->GetServerWorldTimeSeconds();
	OnRep_PlacementCommitted();
	ForceNetUpdate();
}

void ACommandBuilding::OnRep_PlacementCommitted()
{
	if (!bAudioStateInitialized || bPlacementAudioObserved || PlacementCommittedServerTime < 0.)
		return;
	bPlacementAudioObserved = true;
	if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
	{
		const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		if (IsAlive() && !bTerminalAudioHandled && (HasAuthority() || (State && PlacementCommittedServerTime >= State->GetAudioLiveStartServerTime())))
		{
			Audio->PlayStructure(ECoopAudioEvent::Place, TeamIndex, GetActorLocation(), this);
		}
	}
	NotifyAudioState();
}

void ACommandBuilding::StopConstructionAudio()
{
	if (!bConstructionAudioRunning)
		return;
	bConstructionAudioRunning = false;
	if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
		Audio->StopConstruction(this);
}

void ACommandBuilding::NotifyTerminalAudio()
{
	if (bTerminalAudioHandled)
		return;
	bTerminalAudioHandled = true;
	StopConstructionAudio();
	if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
	{
		Audio->PlayStructure(bTerminalCancelled ? ECoopAudioEvent::Cancel : ECoopAudioEvent::Destroyed,
			TeamIndex, GetActorLocation(), this);
	}
}

void ACommandBuilding::NotifyAudioState()
{
	if (!bAudioStateInitialized)
		return;
	if (bTerminalAudioHandled)
	{
		Health = 0;
		bProductionEnabled = false;
		StopConstructionAudio();
		return;
	}
	const bool bComplete = IsComplete();
	const bool bDamaged = Health < AudioPreviousHealth;
	const bool bCompleted = !bAudioWasComplete && bComplete;
	const bool bDied = AudioPreviousHealth > 0 && !IsAlive();
	AudioPreviousHealth = Health;
	bAudioWasComplete = bComplete;
	UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this);
	if (bDamaged && !bTerminalCancelled && Audio)
	{
		Audio->PlayUnit(ECoopAudioEvent::Impact, TeamIndex, EUnitRole::Frontline, GetActorLocation(), this);
	}
	if (bDied)
	{
		NotifyTerminalAudio();
		return;
	}
	if (bComplete || !IsAlive())
		StopConstructionAudio();
	else if (PlacementCommittedServerTime >= 0. && !bConstructionAudioRunning && Audio)
	{
		bConstructionAudioRunning = true;
		Audio->StartConstruction(this, TeamIndex);
	}
	if (bCompleted && IsAlive() && Audio)
	{
		Audio->PlayStructure(ECoopAudioEvent::Complete, TeamIndex, GetActorLocation(), this);
	}
}

void ACommandBuilding::MulticastTerminalState_Implementation(const FCommandBuildingTerminalSnapshot& Snapshot)
{
	bTerminalCancelled = Snapshot.bCancelled;
	Health = Snapshot.Health;
	ConstructionProgress = Snapshot.ConstructionProgress;
	TeamIndex = Snapshot.TeamIndex;
	bProductionEnabled = false;
	OnRep_Appearance();
	NotifyTerminalAudio();
}

void ACommandBuilding::OnRep_DeploymentCount()
{
	if (!bAudioStateInitialized)
		return;
	const uint32 PreviousCount = AudioDeploymentCount;
	AudioDeploymentCount = DeploymentCount;
	if (bTerminalAudioHandled)
		return;
	if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
	{
		for (uint32 Count = PreviousCount; Count < DeploymentCount; ++Count)
		{
			Audio->PlayStructure(ECoopAudioEvent::Deploy, TeamIndex, GetActorLocation(), this);
		}
	}
}

void ACommandBuilding::OnRep_ResearchCount()
{
	if (!bAudioStateInitialized)
		return;
	const uint32 PreviousCount = AudioResearchCount;
	AudioResearchCount = ResearchCount;
	if (bTerminalAudioHandled)
		return;
	if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
	{
		for (uint32 Count = PreviousCount; Count < ResearchCount; ++Count)
		{
			Audio->PlayStructure(ECoopAudioEvent::Research, TeamIndex, GetActorLocation(), this);
		}
	}
}

void ACommandBuilding::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACommandBuilding, Kind);
	DOREPLIFETIME(ACommandBuilding, BuildingIndex);
	DOREPLIFETIME(ACommandBuilding, TeamIndex);
	DOREPLIFETIME(ACommandBuilding, Deposit);
	DOREPLIFETIME(ACommandBuilding, OwningPlayerState);
	DOREPLIFETIME(ACommandBuilding, ForceNumber);
	DOREPLIFETIME(ACommandBuilding, Health);
	DOREPLIFETIME(ACommandBuilding, ConstructionProgress);
	DOREPLIFETIME(ACommandBuilding, ProductionRole);
	DOREPLIFETIME(ACommandBuilding, ProductionUnitIndex);
	DOREPLIFETIME(ACommandBuilding, bForceConfigured);
	DOREPLIFETIME(ACommandBuilding, ForceGroup);
	DOREPLIFETIME(ACommandBuilding, bProductionEnabled);
	DOREPLIFETIME(ACommandBuilding, ProductionProgressSeconds);
	DOREPLIFETIME(ACommandBuilding, RallyRegionIndex);
	DOREPLIFETIME(ACommandBuilding, PlacementCommittedServerTime);
	DOREPLIFETIME(ACommandBuilding, DeploymentCount);
	DOREPLIFETIME(ACommandBuilding, ResearchCount);
	DOREPLIFETIME(ACommandBuilding, StunEndServerTime);
}
