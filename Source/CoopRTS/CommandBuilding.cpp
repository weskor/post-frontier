#include "CommandBuilding.h"

#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "Content/MatchContent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
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

void ACommandBuilding::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// Kind is a derived, replicated view of the definition for callers that still branch on it.
		if (const UBuildingDefinition* Definition = GetDefinition()) Kind = Definition->GetKind();
	}
	OnRep_Appearance();
	if (HasAuthority())
	{
		if (Health <= 0) Health = MaxHealth();
		if (ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>())
		{
			State->Buildings.AddUnique(this);
			State->ForceNetUpdate();
		}
	}
}

void ACommandBuilding::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		if (!State || State->MatchResult != EMatchResult::Ongoing || !IsAlive()) return;
		if (!IsComplete())
		{
			const UBuildingDefinition* Definition = GetDefinition();
			const float Duration = Definition ? GetBuildDuration(*Definition) : 0.f;
			if (Duration <= 0.f) return;
			ConstructionProgress = FMath::Min(1.f, ConstructionProgress + DeltaSeconds / Duration);
			if (IsComplete())
			{
				OnRep_Appearance();
				State->RefreshTerritory();
				ForceNetUpdate();
			}
		}
		else TickProduction(DeltaSeconds);
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		// Producer locks replicate independently of Body; host and standalone never receive an OnRep, so resync here.
		if (DesiredMesh().ToSoftObjectPath() != AppliedMesh) OnRep_Appearance();
		const FColor Color = TeamIndex == 5 ? FColor::Red : FColor::Cyan;
		DrawDebugBox(GetWorld(), GetActorLocation(), Footprint->GetUnscaledBoxExtent(),
			Color, false, -1.f, 0, 2.f);
		const UBuildingDefinition* Definition = GetDefinition();
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 220.f),
			FString::Printf(TEXT("%s  %d/%d  %.0f%%"),
				Definition ? *Definition->DisplayName.ToString().ToUpper() : TEXT("BUILDING"),
				Health, MaxHealth(), ConstructionProgress * 100.f), nullptr, Color, 0.f, true);
	}
}

void ACommandBuilding::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		bProductionEnabled = false;
		if (IsValid(ForceGroup) && !ForceGroup->IsActorBeingDestroyed())
		{
			// Survivors and recruits keep their last accepted front, but never transfer.
			ForceGroup->ProductionBuilding = nullptr;
			bool bSurvivors = false;
			for (const AArmyUnit* Unit : ForceGroup->Units)
				if (IsValid(Unit) && Unit->IsAlive()) { bSurvivors = true; break; }
			if (bSurvivors) ForceGroup->ForceNetUpdate();
			else ForceGroup->Destroy();
		}
		ForceGroup = nullptr;
		if (ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>())
		{
			State->Buildings.Remove(this);
			State->RefreshTerritory();
			State->ForceNetUpdate();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ACommandBuilding::OnRep_Appearance()
{
	if (!Footprint || !Body) return;
	const UBuildingDefinition* Definition = GetDefinition();
	const float Radius = Definition ? GetFootprintRadius(*Definition) : 0.f;
	if (Radius > 0.f) Footprint->SetBoxExtent(FVector(Radius, Radius, 65.f));
	if (GetNetMode() == NM_DedicatedServer) return;
	const TSoftObjectPtr<UStaticMesh> Soft = DesiredMesh();
	AppliedMesh = Soft.ToSoftObjectPath();
	UStaticMesh* Themed = Soft.LoadSynchronous();
	UStaticMesh* Desired = Themed ? Themed : CubeMesh.Get();
	if (Desired && Body->GetStaticMesh() != Desired)
	{
		Body->SetStaticMesh(Desired);
		// SetStaticMesh keeps the previous mesh's override (and any dynamic instance); drop them so the Team slot starts from the mesh default.
		Body->EmptyOverrideMaterials();
		if (!Themed && CubeMaterial) Body->SetMaterial(0, CubeMaterial);
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
	if (!Material) Material = Body->CreateAndSetMaterialInstanceDynamic(TeamSlot);
	if (Material) Material->SetVectorParameterValue(TEXT("TeamColor"), !IsAlive() ? FLinearColor(.12f, .12f, .12f)
		: !IsComplete() ? FLinearColor(.8f, .6f, .18f) : TeamIndex == 5 ? FLinearColor(1.f, .08f, .08f)
		: FLinearColor(.04f, .5f, 1.f));
}

TSoftObjectPtr<UStaticMesh> ACommandBuilding::DesiredMesh() const
{
	const UBuildingDefinition* Definition = GetDefinition();
	if (!Definition) return nullptr;
	if (!IsComplete()) return Definition->ConstructionMesh;
	const bool bMachine = TeamIndex == 5;
	// Producers show the locked-type variant once configured; an unlisted type keeps the neutral mesh.
	if (Definition->bProducesForces && bForceConfigured)
	{
		const TArray<TSoftObjectPtr<UStaticMesh>>& Variants = bMachine ? Definition->MachineRoleMeshes : Definition->HumanRoleMeshes;
		if (Variants.IsValidIndex(ProductionUnitIndex) && !Variants[ProductionUnitIndex].IsNull()) return Variants[ProductionUnitIndex];
	}
	return bMachine ? Definition->MachineMesh : Definition->HumanMesh;
}

void ACommandBuilding::ReceiveAttack(int32 Damage, AArmyUnit* Attacker)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || IsActorBeingDestroyed() || !IsAlive() || !IsValid(Attacker) || !Attacker->IsAlive()
		|| Attacker->TeamIndex == TeamIndex || Damage <= 0 || !State || State->MatchResult != EMatchResult::Ongoing) return;
	Health = FMath::Max(0, Health - Damage);
	OnRep_Appearance();
	ForceNetUpdate();
	if (!IsAlive())
	{
		bProductionEnabled = false;
		if (ACommandGameState* MutableState = GetWorld()->GetGameState<ACommandGameState>()) MutableState->RefreshTerritory();
		Destroy();
	}
}

bool ACommandBuilding::TrySpend(int32 Cost)
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || IsActorBeingDestroyed() || !IsAlive() || !State || State->MatchResult != EMatchResult::Ongoing || Cost <= 0) return false;
	if (TeamIndex == 5 && !OwningPlayerState && State->EnemyResources >= Cost)
	{
		State->EnemyResources -= Cost;
		State->ForceNetUpdate();
		return true;
	}
	return TeamIndex == 0 && IsValid(OwningPlayerState) && OwningPlayerState->GetWorld() == GetWorld()
		&& OwningPlayerState->TrySpend(Cost);
}

bool ACommandBuilding::CancelConstruction()
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || IsActorBeingDestroyed() || !IsAlive() || IsComplete() || !State || State->MatchResult != EMatchResult::Ongoing) return false;
	const UBuildingDefinition* Definition = GetDefinition();
	const int32 Refund = Definition ? EconomyPolicy::CancellationRefund(GetBuildCost(*Definition), ConstructionProgress) : 0;
	if (TeamIndex == 5)
	{
		State->EnemyResources = static_cast<int32>(FMath::Min<int64>(MAX_int32,
			static_cast<int64>(State->EnemyResources) + Refund));
		State->ForceNetUpdate();
	}
	else if (IsValid(OwningPlayerState)) OwningPlayerState->AddResources(Refund);
	Destroy();
	return true;
}


bool ACommandBuilding::TryResearch(EArmyDoctrine Choice)
{
	const UBuildingDefinition* Definition = GetDefinition();
	if (!Definition || !Definition->bOffersResearch || !IsComplete()
		|| (Choice != EArmyDoctrine::SiegeOptics && Choice != EArmyDoctrine::FieldRepairs
			&& Choice != EArmyDoctrine::EntrenchedFrontline)) return false;
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State || (TeamIndex == 5 ? State->EnemyDoctrine != EArmyDoctrine::None
		: !IsValid(OwningPlayerState) || OwningPlayerState->Doctrine != EArmyDoctrine::None)) return false;
	if (!TrySpend(ResearchCost)) return false;
	if (TeamIndex == 5) { State->EnemyDoctrine = Choice; State->ForceNetUpdate(); }
	else if (!OwningPlayerState->TryChooseDoctrine(Choice))
	{
		OwningPlayerState->AddResources(ResearchCost);
		return false;
	}
	return true;
}

void ACommandBuilding::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACommandBuilding, Kind);
	DOREPLIFETIME(ACommandBuilding, BuildingIndex);
	DOREPLIFETIME(ACommandBuilding, TeamIndex);
	DOREPLIFETIME(ACommandBuilding, OutpostSite);
	DOREPLIFETIME(ACommandBuilding, OwningPlayerState);
	DOREPLIFETIME(ACommandBuilding, Health);
	DOREPLIFETIME(ACommandBuilding, ConstructionProgress);
	DOREPLIFETIME(ACommandBuilding, ProductionRole);
	DOREPLIFETIME(ACommandBuilding, ProductionUnitIndex);
	DOREPLIFETIME(ACommandBuilding, bForceConfigured);
	DOREPLIFETIME(ACommandBuilding, ForceGroup);
	DOREPLIFETIME(ACommandBuilding, bProductionEnabled);
	DOREPLIFETIME(ACommandBuilding, ProductionProgressSeconds);
	DOREPLIFETIME(ACommandBuilding, FrontOrder);
	DOREPLIFETIME(ACommandBuilding, FrontLocation);
	DOREPLIFETIME(ACommandBuilding, bHasConfiguredFront);
}
