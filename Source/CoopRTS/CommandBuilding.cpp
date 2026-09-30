#include "CommandBuilding.h"

#include "ArmyGroup.h"
#include "CommandGameState.h"
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

namespace
{
	// Constructor-only: FObjectFinder must run while a CDO is being constructed.
	UStaticMesh* FindBuildingMesh(const FString& Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/Art/Buildings/%s.%s"), *Name, *Name);
		ConstructorHelpers::FObjectFinder<UStaticMesh> Finder(*Path);
		return Finder.Succeeded() ? Finder.Object : nullptr;
	}

	void FindFactionMeshes(const TCHAR* Faction, TArray<TObjectPtr<UStaticMesh>>& OutMeshes)
	{
		static const TCHAR* const Suffixes[] = { TEXT("Barracks"), TEXT("Barracks_Frontline"), TEXT("Barracks_Ranged"),
			TEXT("Barracks_Siege"), TEXT("Outpost"), TEXT("Workshop") };
		for (const TCHAR* Suffix : Suffixes)
			OutMeshes.Add(FindBuildingMesh(FString::Printf(TEXT("SM_%s_%s"), Faction, Suffix)));
	}
}

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
	FindFactionMeshes(TEXT("Human"), HumanMeshes);
	FindFactionMeshes(TEXT("Machine"), MachineMeshes);
	static const TCHAR* const ConstructionKinds[] = { TEXT("Barracks"), TEXT("Outpost"), TEXT("Workshop") };
	for (const TCHAR* ConstructionKind : ConstructionKinds)
		ConstructionMeshes.Add(FindBuildingMesh(FString::Printf(TEXT("SM_Construction_%s"), ConstructionKind)));
}

int32 ACommandBuilding::GetBuildCost(EBuildingKind InKind)
{
	switch (InKind)
	{
	case EBuildingKind::Barracks: return 220;
	case EBuildingKind::Outpost: return 160;
	case EBuildingKind::Workshop: return 190;
	default: return 0;
	}
}

float ACommandBuilding::GetBuildDuration(EBuildingKind InKind)
{
	switch (InKind)
	{
	case EBuildingKind::Barracks: return 12.f;
	case EBuildingKind::Outpost: return 9.f;
	case EBuildingKind::Workshop: return 14.f;
	default: return 0.f;
	}
}

float ACommandBuilding::GetFootprintRadius(EBuildingKind InKind)
{
	switch (InKind)
	{
	case EBuildingKind::Barracks: return 125.f;
	case EBuildingKind::Outpost: return 95.f;
	case EBuildingKind::Workshop: return 145.f;
	default: return 0.f;
	}
}

int32 ACommandBuilding::MaxHealth() const
{
	return Kind == EBuildingKind::Outpost ? 350 : Kind == EBuildingKind::Workshop ? 400 : 500;
}

void ACommandBuilding::BeginPlay()
{
	Super::BeginPlay();
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
			ConstructionProgress = FMath::Min(1.f, ConstructionProgress + DeltaSeconds / GetBuildDuration(Kind));
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
		// Barracks role locks replicate independently of Body; host and standalone never receive an OnRep, so resync here.
		UStaticMesh* Desired = GetThemedMesh();
		if (!Desired) Desired = CubeMesh;
		if (Body->GetStaticMesh() != Desired) OnRep_Appearance();
		const FColor Color = TeamIndex == 5 ? FColor::Red : FColor::Cyan;
		DrawDebugBox(GetWorld(), GetActorLocation(), Footprint->GetUnscaledBoxExtent(),
			Color, false, -1.f, 0, 2.f);
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 220.f),
			FString::Printf(TEXT("%s  %d/%d  %.0f%%"),
				Kind == EBuildingKind::Barracks ? TEXT("BARRACKS") : Kind == EBuildingKind::Outpost ? TEXT("OUTPOST") : TEXT("WORKSHOP"),
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
	const float Radius = GetFootprintRadius(Kind);
	if (Radius > 0.f) Footprint->SetBoxExtent(FVector(Radius, Radius, 65.f));
	if (GetNetMode() == NM_DedicatedServer) return;
	UStaticMesh* Themed = GetThemedMesh();
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
		const FVector Shape = Kind == EBuildingKind::Outpost ? FVector(1.35f, 1.35f, 2.6f)
			: Kind == EBuildingKind::Workshop ? FVector(2.5f, 2.5f, 1.15f) : FVector(2.1f, 2.1f, 1.85f);
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

UStaticMesh* ACommandBuilding::GetThemedMesh() const
{
	const int32 KindIndex = static_cast<int32>(Kind);
	if (!IsComplete()) return ConstructionMeshes.IsValidIndex(KindIndex) ? ConstructionMeshes[KindIndex].Get() : nullptr;
	// Barracks: role variant once the type is permanently locked, neutral mesh before. Others: Outpost 4, Workshop 5.
	const int32 FactionIndex = Kind == EBuildingKind::Barracks
		? (bForceConfigured ? 1 + static_cast<int32>(ProductionRole) : 0)
		: 3 + KindIndex;
	const TArray<TObjectPtr<UStaticMesh>>& Meshes = TeamIndex == 5 ? MachineMeshes : HumanMeshes;
	return Meshes.IsValidIndex(FactionIndex) ? Meshes[FactionIndex].Get() : nullptr;
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
	const int32 Refund = FMath::FloorToInt(GetBuildCost(Kind) * (1.f - ConstructionProgress));
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
	if (Kind != EBuildingKind::Workshop || !IsComplete()
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
	DOREPLIFETIME(ACommandBuilding, TeamIndex);
	DOREPLIFETIME(ACommandBuilding, OutpostSite);
	DOREPLIFETIME(ACommandBuilding, OwningPlayerState);
	DOREPLIFETIME(ACommandBuilding, Health);
	DOREPLIFETIME(ACommandBuilding, ConstructionProgress);
	DOREPLIFETIME(ACommandBuilding, ProductionRole);
	DOREPLIFETIME(ACommandBuilding, bForceConfigured);
	DOREPLIFETIME(ACommandBuilding, ForceGroup);
	DOREPLIFETIME(ACommandBuilding, bProductionEnabled);
	DOREPLIFETIME(ACommandBuilding, ProductionProgressSeconds);
	DOREPLIFETIME(ACommandBuilding, FrontOrder);
	DOREPLIFETIME(ACommandBuilding, FrontLocation);
	DOREPLIFETIME(ACommandBuilding, bHasConfiguredFront);
}
